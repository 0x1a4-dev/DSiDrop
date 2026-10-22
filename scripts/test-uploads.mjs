import { registerTypescript } from './test-support/typescript.mjs';
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, mkdir, readdir, readFile, rm, stat, writeFile } from "node:fs/promises";
import path from "node:path";

const project = process.cwd();
const cache = path.join(project, ".cache");
await mkdir(cache, { recursive: true });
const directory = await mkdtemp(path.join(cache, "uploads-test-"));
const previousRomsDir = process.env.ROMS_DIR;
process.env.ROMS_DIR = path.join(directory, "roms");
const { require, restore: restoreTypescript } = registerTypescript();
const filesystem = require("node:fs/promises");
const service = require(path.join(project, "src/lib/server/uploads.ts"));
const library = path.join(process.env.ROMS_DIR, "others");
const endpoint = "http://local/api/library/others";
const originalFetch = globalThis.fetch;

function payload(seed = 1) {
  const bytes = Buffer.alloc(16_384, seed);
  bytes.fill(0, 0, 512);
  bytes.write("TEST HOMEBRW", 0, "ascii");
  bytes.writeUInt32LE(512, 0x20); bytes.writeUInt32LE(256, 0x2c);
  bytes.writeUInt32LE(768, 0x30); bytes.writeUInt32LE(256, 0x3c);
  return bytes;
}
function multipart(files, fields = [], options = {}) {
  const boundary = "dsidrop-test-boundary";
  const chunks = [];
  for (const [name, bytes, field = "files"] of files) chunks.push(Buffer.from(
    `--${boundary}\r\nContent-Disposition: form-data; name="${field}"; filename="${name}"\r\nContent-Type: application/octet-stream\r\n\r\n`), bytes, Buffer.from("\r\n"));
  for (const [name, value] of fields) chunks.push(Buffer.from(`--${boundary}\r\nContent-Disposition: form-data; name="${name}"\r\n\r\n${value}\r\n`));
  chunks.push(Buffer.from(`--${boundary}--\r\n`));
  return new Request(options.url ?? endpoint, { method: "POST", headers: { "Content-Type": `multipart/form-data; boundary=${boundary}`, ...options.headers }, body: Buffer.concat(chunks) });
}
const md5 = (bytes) => createHash("md5").update(bytes).digest("hex");
function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return ((crc ^ 0xffffffff) >>> 0).toString(16).padStart(8, "0");
}
const reject = async (request, status) => assert.rejects(service.receiveUploads(request), (error) => error.status === status);
const noParts = async () => assert((await readdir(library)).every((name) => !name.endsWith(".part")), "No orphaned staged files");
const deleteRequest = (id, options = {}) => new Request(`${options.url ?? endpoint}?id=${encodeURIComponent(id)}`, { method: "DELETE", headers: options.headers });
const missingFile = async (file) => assert.rejects(stat(file), (error) => error.code === "ENOENT");

try {
  const empty = await service.uploadedDataset();
  assert.equal(empty.meta.id, "others"); assert.equal(empty.roms.length, 0);
  const firstBytes = payload();
  const firstId = md5(firstBytes);
  const first = await service.receiveUploads(multipart([["Synthetic Homebrew.nds", firstBytes]]));
  assert.equal(first.uploaded, 1); assert.equal(first.duplicates, 0);
  assert.equal(first.dataset.meta.total, 1); assert.equal(first.dataset.roms[0].m, firstId);
  const local = await service.getUploadedFile(firstId);
  assert.equal(local.name, "Synthetic Homebrew.nds");
  assert.deepEqual(await readFile(local.file), firstBytes);
  const duplicate = await service.receiveUploads(multipart([["Renamed.nds", firstBytes], ["Again.nds", firstBytes]]));
  assert.equal(duplicate.uploaded, 0); assert.equal(duplicate.duplicates, 2);
  assert.equal((await service.getUploadedFile(firstId)).name, "Synthetic Homebrew.nds", "A duplicate preserves its existing name");
  await rm(local.file);
  const restored = await service.receiveUploads(multipart([["Replacement name.nds", firstBytes]]));
  assert.equal(restored.uploaded, 1); assert.equal(restored.dataset.roms.length, 1);
  assert.equal((await service.getUploadedFile(firstId)).name, "Synthetic Homebrew.nds", "Restoring a missing file retains the saved title");
  const corrupted = firstBytes.subarray(0, firstBytes.length - 1);
  await writeFile(local.file, corrupted);
  await assert.rejects(service.getUploadedFile(firstId), (error) => error.status === 409);
  await reject(multipart([["Do not overwrite.nds", firstBytes]]), 409);
  assert.deepEqual(await readFile(local.file), corrupted, "A conflicting existing file is never overwritten");
  await writeFile(local.file, firstBytes);

  const requests = Array.from({ length: 8 }, (_, i) => service.receiveUploads(multipart([[`Concurrent ${i}.nds`, payload(i + 2)], ["Shared.nds", firstBytes]])));
  const concurrent = await Promise.all(requests);
  assert.equal(concurrent.reduce((sum, result) => sum + result.uploaded, 0), 8);
  assert.equal(concurrent.reduce((sum, result) => sum + result.duplicates, 0), 8);
  assert.equal((await service.uploadedDataset()).roms.length, 9, "Concurrent commits preserve every entry");
  await noParts();

  const appFixture = await readFile(path.join(project, "public/downloads/DSiDrop.nds"));
  const appResult = await service.receiveUploads(multipart([["DSiDrop.nds", appFixture]]));
  assert.equal(appResult.uploaded, 1, "The actual DSi homebrew build passes NDS header validation");
  const unicodeBytes = payload(77);
  await service.receiveUploads(multipart([["🎮.nds", unicodeBytes]]));

  const direct = await service.uploadFileResponse(new Request(`http://local/api/library/file?id=${firstId}`));
  assert.equal(direct.headers.get("Content-Length"), String(firstBytes.length));
  assert.deepEqual(Buffer.from(await direct.arrayBuffer()), firstBytes);
  const head = await service.uploadFileResponse(new Request(`http://local/api/library/file?id=${firstId}`), true);
  assert.equal(head.body, null); assert.equal(head.status, 200);
  for (const [range, expected] of [["bytes=731-1200", firstBytes.subarray(731, 1201)], ["bytes=731-", firstBytes.subarray(731)], ["bytes=-13", firstBytes.subarray(-13)]]) {
    const response = await service.uploadFileResponse(new Request(`http://local/api/library/file?id=${firstId}`, { headers: { Range: range } }));
    assert.equal(response.status, 206); assert.deepEqual(Buffer.from(await response.arrayBuffer()), expected);
  }
  const invalidRange = await service.uploadFileResponse(new Request(`http://local/api/library/file?id=${firstId}`, { headers: { Range: "bytes=999999-" } }));
  assert.equal(invalidRange.status, 416); assert.equal(invalidRange.headers.get("Content-Range"), `bytes */${firstBytes.length}`);
  await assert.rejects(service.getUploadedFile("../not-a-game"), (error) => error.status === 400);
  await assert.rejects(service.getUploadedFile("f".repeat(32)), (error) => error.status === 404);

  await reject(multipart([["Archive.zip", firstBytes]]), 415);
  await reject(multipart([["../Escape.nds", firstBytes]]), 400);
  await reject(multipart([["Not a ROM.nds", Buffer.alloc(1024)]]), 415);
  const outside = payload(22); outside.writeUInt32LE(outside.length + 1, 0x20);
  await reject(multipart([["Bad sections.nds", outside]]), 415);
  const overlap = payload(23); overlap.writeUInt32LE(512, 0x30);
  await reject(multipart([["Overlap.nds", overlap]]), 415);
  await reject(multipart([["Wrong field.nds", firstBytes, "rom"]]), 400);
  await reject(multipart([], [["unexpected", "field"]]), 400);
  await reject(multipart([]), 400);
  const oversized = multipart([["Too big.nds", firstBytes]]);
  oversized.headers.set("Content-Length", String(service.MAX_UPLOAD_BATCH_SIZE + 1024 * 1024 + 1));
  await reject(oversized, 413);
  await reject(multipart(Array.from({ length: 21 }, () => ["Too many.nds", firstBytes])), 413);
  const exactBatch = await service.receiveUploads(multipart(Array.from({ length: 20 }, () => ["Twenty.nds", firstBytes])));
  assert.equal(exactBatch.duplicates, 20, "The advertised 20-file limit is accepted");
  const crossOrigin = multipart([["Cross Origin.nds", firstBytes]]);
  crossOrigin.headers.set("Origin", "https://unrelated.invalid");
  await reject(crossOrigin, 403);
  for (const [host, origin, forwarded] of [
    ["127.0.0.1:3001", "http://127.0.0.1:3001", "http"],
    ["192.168.1.5:3000", "http://192.168.1.5:3000", "http"],
    ["dsidrop.local", "https://dsidrop.local", "https, http"],
  ]) {
    const accepted = await service.receiveUploads(multipart([["Same host.nds", firstBytes]], [], {
      url: "http://localhost:3001/api/library/others", headers: { Host: host, Origin: origin, "X-Forwarded-Proto": forwarded },
    }));
    assert.equal(accepted.duplicates, 1, "Browser-facing Host and forwarded scheme authorize the matching origin");
  }
  for (const origin of ["http://foreign.invalid:3001", "https://127.0.0.1:3001", "http://127.0.0.1:3002", "null"]) {
    await reject(multipart([["Rejected origin.nds", firstBytes]], [], {
      url: "http://localhost:3001/api/library/others", headers: { Host: "127.0.0.1:3001", Origin: origin, "X-Forwarded-Proto": "http" },
    }), 403);
  }
  await reject(multipart([["Malformed host.nds", firstBytes]], [], {
    headers: { Host: "local@foreign.invalid", Origin: "http://foreign.invalid" },
  }), 403);
  await reject(new Request(endpoint, { method: "POST", headers: { "Content-Type": "multipart/form-data; boundary=bad" }, body: "broken" }), 400);

  const atomicBytes = payload(42);
  await reject(multipart([["Do not commit.nds", atomicBytes], ["Invalid.nds", Buffer.alloc(512)]]), 415);
  assert(!(await service.uploadedDataset()).roms.some((rom) => rom.m === md5(atomicBytes)));
  await noParts();

  const abort = new AbortController();
  let sentHeader = false;
  const slow = new ReadableStream({
    async pull(controller) {
      if (!sentHeader) {
        sentHeader = true;
        controller.enqueue(Buffer.from('--slow\r\nContent-Disposition: form-data; name="files"; filename="Canceled.nds"\r\n\r\n'));
      } else {
        await new Promise((resolve) => setTimeout(resolve, 3));
        controller.enqueue(payload(60));
      }
    },
  });
  const canceled = new Request(endpoint, { method: "POST", headers: { "Content-Type": "multipart/form-data; boundary=slow" }, body: slow, duplex: "half", signal: abort.signal });
  const cancelResult = service.receiveUploads(canceled);
  setTimeout(() => abort.abort(), 25);
  await assert.rejects(cancelResult, (error) => error.status === 499);
  await noParts();

  globalThis.fetch = async () => { throw new Error("Unexpected upstream request for uploaded ROM"); };
  const dsi = require(path.join(project, "src/lib/server/dsi.ts"));
  assert.equal(dsi.dsiSystem("others"), "others");
  const allCatalog = await dsi.catalog(new URLSearchParams("system=others&q=🎮"));
  assert(allCatalog.includes(`${md5(unicodeBytes)}\tNDS upload ${md5(unicodeBytes).slice(0, 8)}\t`), "A Unicode-only upload still has a valid printable DSi title");
  const catalog = await dsi.catalog(new URLSearchParams("system=others&q=synthetic"));
  assert(catalog.startsWith("RFCATALOG1\t1\t0\t1\n")); assert(catalog.includes(firstId));
  await assert.rejects(dsi.prepare("a".repeat(8), "others"), (error) => error.status === 400);
  const job = await dsi.prepare(firstId, "others");
  for (let i = 0; i < 200 && !["ready", "error"].includes(job.state); i++) await new Promise((resolve) => setTimeout(resolve, 5));
  assert.equal(job.state, "ready", job.message); assert.equal(job.size, firstBytes.length);
  assert.equal(job.file, local.file); assert(job.name.endsWith(".nds"));
  assert.equal(job.crc, crc32(firstBytes));
  const dsiRange = await dsi.gameFile(new Request(`http://local/api/dsi/file?system=others&id=${firstId}`, { headers: { Range: "bytes=4093-" } }));
  assert.equal(dsiRange.status, 206); assert.equal(dsiRange.headers.get("X-ROM-CRC32"), job.crc);
  assert.equal(dsiRange.headers.get("X-ROM-System"), "others");
  assert.deepEqual(Buffer.from(await dsiRange.arrayBuffer()), firstBytes.subarray(4093));

  const sentinel = path.join(directory, "outside.nds");
  await writeFile(sentinel, "Keep outside files");
  for (const id of ["", "../outside", "../" + firstId, firstId.toUpperCase(), "f".repeat(33)])
    await assert.rejects(service.deleteUploaded(deleteRequest(id)), (error) => error.status === 400);
  for (const headers of [
    { Origin: "http://foreign.invalid" },
    { Host: "local@foreign.invalid", Origin: "http://foreign.invalid" },
    { Host: "127.0.0.1:3001", Origin: "http://127.0.0.1:3002" },
  ]) await assert.rejects(service.deleteUploaded(deleteRequest(firstId, { headers })), (error) => error.status === 403);
  assert.equal(await readFile(sentinel, "utf8"), "Keep outside files");
  await assert.rejects(service.deleteUploaded(deleteRequest("f".repeat(32))), (error) => error.status === 404);

  const originalRename = filesystem.rename;
  let failedPublish = false;
  filesystem.rename = async (source, destination) => {
    if (!failedPublish && destination === path.join(library, "library.json") && path.basename(source).startsWith(".manifest-")) {
      failedPublish = true;
      throw Object.assign(new Error("Simulated manifest permission failure"), { code: "EPERM" });
    }
    return originalRename(source, destination);
  };
  try { await assert.rejects(service.deleteUploaded(deleteRequest(firstId)), (error) => error.status === 409); }
  finally { filesystem.rename = originalRename; }
  assert(failedPublish);
  assert((await service.uploadedDataset()).roms.some((rom) => rom.m === firstId));
  assert.deepEqual(await readFile(local.file), firstBytes);
  await noParts();
  const originalRm = filesystem.rm;
  let failedRemoval = false;
  filesystem.rm = async (file, options) => {
    if (!failedRemoval && path.basename(file).startsWith(".delete-")) {
      failedRemoval = true;
      throw Object.assign(new Error("Simulated active file download"), { code: "EBUSY" });
    }
    return originalRm(file, options);
  };
  try { await assert.rejects(service.deleteUploaded(deleteRequest(firstId)), (error) => error.status === 409); }
  finally { filesystem.rm = originalRm; }
  assert(failedRemoval);
  assert((await service.uploadedDataset()).roms.some((rom) => rom.m === firstId));
  assert.deepEqual(await readFile(local.file), firstBytes);
  await noParts();

  const deleted = await service.deleteUploaded(deleteRequest(firstId, { url: "http://localhost:3001/api/library/others",
    headers: { Host: "127.0.0.1:3001", Origin: "http://127.0.0.1:3001", "X-Forwarded-Proto": "http" } }));
  assert.equal(deleted.deleted, firstId);
  assert(!deleted.dataset.roms.some((rom) => rom.m === firstId));
  await missingFile(local.file);
  await assert.rejects(service.getUploadedFile(firstId), (error) => error.status === 404);
  await assert.rejects(service.uploadFileResponse(new Request(`http://local/api/library/file?id=${firstId}`)), (error) => error.status === 404);
  await assert.rejects(dsi.prepare(firstId, "others"), (error) => error.status === 404);
  await assert.rejects(dsi.getJob(firstId, "others"), (error) => error.status === 404);
  await assert.rejects(dsi.gameFile(new Request(`http://local/api/dsi/file?system=others&id=${firstId}`)), (error) => error.status === 404);
  assert(!(await dsi.catalog(new URLSearchParams("system=others"))).includes(firstId));
  delete require.cache[require.resolve(path.join(project, "src/lib/server/uploads.ts"))];
  const reopened = require(path.join(project, "src/lib/server/uploads.ts"));
  assert(!(await reopened.uploadedDataset()).roms.some((rom) => rom.m === firstId), "Deletion survives reloading the library service");
  await noParts();

  const concurrentBytes = payload(91);
  const concurrentId = md5(concurrentBytes);
  await service.receiveUploads(multipart([["Delete concurrently.nds", concurrentBytes]]));
  const removals = await Promise.allSettled([service.deleteUploaded(deleteRequest(concurrentId)), service.deleteUploaded(deleteRequest(concurrentId))]);
  assert.equal(removals.filter((result) => result.status === "fulfilled").length, 1);
  assert(removals.some((result) => result.status === "rejected" && result.reason.status === 404));
  await missingFile(path.join(library, `${concurrentId}.nds`));
  await service.receiveUploads(multipart([["Reupload concurrently.nds", concurrentBytes]]));
  await Promise.all([service.deleteUploaded(deleteRequest(concurrentId)), service.receiveUploads(multipart([["Reupload concurrently.nds", concurrentBytes]]))]);
  const afterConcurrent = await service.uploadedDataset();
  if (afterConcurrent.roms.some((rom) => rom.m === concurrentId)) assert.deepEqual(await readFile((await service.getUploadedFile(concurrentId)).file), concurrentBytes);
  else await missingFile(path.join(library, `${concurrentId}.nds`));
  await noParts();
  const absentBytes = payload(92);
  const absentId = md5(absentBytes);
  await service.receiveUploads(multipart([["Payload missing.nds", absentBytes]]));
  await rm((await service.getUploadedFile(absentId)).file);
  assert.equal((await service.deleteUploaded(deleteRequest(absentId))).deleted, absentId, "An already-missing payload can be removed from the library");
  if (process.argv.includes("--large")) {
    const largeRequest = (size, filename) => {
      const boundary = "large-stream";
      const zero = Buffer.alloc(1024 * 1024);
      let remaining = size;
      let stage = 0;
      return new Request(endpoint, { method: "POST", duplex: "half", headers: { "Content-Type": `multipart/form-data; boundary=${boundary}` }, body: new ReadableStream({
        pull(controller) {
          if (stage === 0) { stage++; controller.enqueue(Buffer.from(`--${boundary}\r\nContent-Disposition: form-data; name="files"; filename="${filename}"\r\n\r\n`)); return; }
          if (stage === 1) { stage++; const header = payload(88); remaining -= header.length; controller.enqueue(header); return; }
          if (remaining) { const length = Math.min(remaining, zero.length); remaining -= length; controller.enqueue(zero.subarray(0, length)); return; }
          if (stage === 2) { stage++; controller.enqueue(Buffer.from(`\r\n--${boundary}--\r\n`)); controller.close(); }
        },
      }) });
    };
    const large = await service.receiveUploads(largeRequest(service.MAX_UPLOAD_SIZE, "Exactly512MiB.nds"));
    assert.equal(large.uploaded, 1);
    assert(large.dataset.roms.some((rom) => rom.s === service.MAX_UPLOAD_SIZE));
    await reject(largeRequest(service.MAX_UPLOAD_SIZE + 1, "Over512MiB.nds"), 413);
    await noParts();
    console.log("PASS: actual streamed 512 MiB accepted; 512 MiB + 1 byte rejected and cleaned.");
  }
  console.log("PASS: streamed uploads, validation, ranges, cancellation, serialized deletion, origin/path checks, rollback, file cleanup and invalidation of prepared DSi uploads.");
} finally {
  globalThis.fetch = originalFetch;
  restoreTypescript();
  if (previousRomsDir === undefined) delete process.env.ROMS_DIR; else process.env.ROMS_DIR = previousRomsDir;
  const absolute = path.resolve(directory);
  assert(absolute.startsWith(path.resolve(cache) + path.sep) && path.basename(absolute).startsWith("uploads-test-"));
  await rm(absolute, { recursive: true, force: true });
}
