import { registerTypescript } from './test-support/typescript.mjs';
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, mkdir, writeFile, readFile, access, readdir, rm } from "node:fs/promises";
import path from "node:path";
import { deflateRawSync } from "node:zlib";

const project = process.cwd();
await mkdir(path.join(project, ".cache"), { recursive: true });
const root = await mkdtemp(path.join(project, ".cache", "dsi-preparation-"));
const originalFetch = globalThis.fetch;
const originalRomsDir = process.env.ROMS_DIR;
const { require, restore: restoreTypescript } = registerTypescript();

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function zip(fileName, bytes) {
  const name = Buffer.from(fileName);
  const compressed = deflateRawSync(bytes);
  const local = Buffer.alloc(30);
  local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4); local.writeUInt16LE(8, 8);
  local.writeUInt32LE(crc32(bytes), 14); local.writeUInt32LE(compressed.length, 18);
  local.writeUInt32LE(bytes.length, 22); local.writeUInt16LE(name.length, 26);
  const cd = Buffer.alloc(46);
  cd.writeUInt32LE(0x02014b50, 0); cd.writeUInt16LE(20, 4); cd.writeUInt16LE(20, 6);
  cd.writeUInt16LE(8, 10); cd.writeUInt32LE(crc32(bytes), 16);
  cd.writeUInt32LE(compressed.length, 20); cd.writeUInt32LE(bytes.length, 24); cd.writeUInt16LE(name.length, 28);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10);
  end.writeUInt32LE(cd.length + name.length, 12); end.writeUInt32LE(local.length + name.length + compressed.length, 16);
  return Buffer.concat([local, name, compressed, cd, name, end]);
}

try {
  process.env.ROMS_DIR = path.join(root, "roms");
  process.chdir(root);
  await mkdir("public/data", { recursive: true });
  const payload = Buffer.alloc(16384);
  for (let i = 0; i < payload.length; i++) payload[i] = i % 251;
  const archive = zip("Synthetic Homebrew.nds", payload);
  const id = createHash("md5").update(archive).digest("hex");
  const wrongId = "1".repeat(32);
  const invalidZip = Buffer.from("not a zip archive");
  const invalidId = createHash("md5").update(invalidZip).digest("hex");
  const makeRom = (m) => ({ m, n: `Synthetic ${m}`, t: "Synthetic Homebrew", r: "Europe", s: archive.length, c: null });
  await writeFile("public/data/nds.json", JSON.stringify({
    meta: { id: "nds", ext: "zip", arch: "zip", out: "nds", download: "https://test.invalid" },
    roms: [makeRom(id), makeRom(wrongId), makeRom(invalidId)],
  }));
  const gbaId = crc32(payload).toString(16).padStart(8, "0");
  const badGbaId = gbaId === "00000000" ? "11111111" : "00000000";
  await writeFile("public/data/gba.json", JSON.stringify({
    meta: { id: "gba", ext: "gba", arch: null, out: "gba", checksum: "crc32", download: "https://test.invalid/gba" },
    roms: [makeRom(gbaId), makeRom(badGbaId)],
  }));
  let upstreamCalls = 0;
  globalThis.fetch = async (url) => {
    assert(String(url).startsWith("https://test.invalid/"));
    upstreamCalls++;
    await new Promise((resolve) => setTimeout(resolve, 20));
    return new Response(String(url).includes("/gba/") ? payload : String(url).includes(invalidId) ? invalidZip : archive);
  };

  const service = require(path.join(project, "src/lib/server/dsi.ts"));
  const wait = async (job) => {
    for (let i = 0; i < 200 && (!["ready", "error"].includes(job.state) || globalThis.__dsiStore.active); i++)
      await new Promise((resolve) => setTimeout(resolve, 10));
    return job;
  };
  const first = await service.prepare(id);
  assert.equal(await service.prepare(id), first, "Repeated prepare shares one job");
  await assert.rejects(service.prepare(wrongId), (error) => error.status === 409, "Only one upstream preparation at a time");
  await wait(first);
  assert.equal(first.state, "ready", first.message);
  assert.equal(first.size, payload.length);
  assert.equal(first.crc, crc32(payload).toString(16).padStart(8, "0"));
  assert.deepEqual(await readFile(first.file), payload);
  assert.equal(upstreamCalls, 1);
  assert.deepEqual(await readdir(path.dirname(first.file)), ["game.nds"]);

  globalThis.__dsiStore.jobs.clear();
  const cached = await wait(await service.prepare(id));
  assert.equal(cached.state, "ready");
  assert.equal(upstreamCalls, 1);
  const full = await service.gameFile(new Request(`http://local/api/dsi/file?id=${id}`));
  assert.deepEqual(Buffer.from(await full.arrayBuffer()), payload);
  const gbaCatalog = await service.catalog(new URLSearchParams("system=gba&q=homebrew"));
  assert(gbaCatalog.includes(gbaId) && !gbaCatalog.includes(id));
  const ndsCatalog = await service.catalog(new URLSearchParams("system=nds"));
  assert(ndsCatalog.includes(id) && !ndsCatalog.includes(`\n${gbaId}\t`));
  await assert.rejects(service.prepare(id, "gba"), (error) => error.status === 400);
  await assert.rejects(service.prepare(gbaId, "nds"), (error) => error.status === 400);
  await assert.rejects(service.prepare(gbaId, "../gba"), (error) => error.status === 400);
  const gba = await wait(await service.prepare(gbaId, "gba"));
  assert.equal(gba.state, "ready", gba.message);
  assert.equal(gba.system, "gba"); assert(gba.name.endsWith(".gba"));
  assert.equal(gba.crc, gbaId);
  assert(gba.file.includes(`${path.sep}gba${path.sep}${gbaId}${path.sep}`));
  assert.equal((await service.getJob(id)).system, "nds", "GBA preparation preserves the NDS job");
  await assert.rejects(service.getJob(gbaId), (error) => error.status === 400);
  const gbaRange = await service.gameFile(new Request(`http://local/api/dsi/file?system=gba&id=${gbaId}`, {
    headers: { Range: "bytes=1003-" },
  }));
  assert.equal(gbaRange.status, 206);
  assert.equal(gbaRange.headers.get("X-ROM-System"), "gba");
  assert.deepEqual(Buffer.from(await gbaRange.arrayBuffer()), payload.subarray(1003));
  const beforeGbaCache = upstreamCalls;
  globalThis.__dsiStore.jobs.clear();
  const gbaCached = await wait(await service.prepare(gbaId, "gba"));
  assert.equal(gbaCached.state, "ready"); assert.equal(upstreamCalls, beforeGbaCache);
  const badGba = await wait(await service.prepare(badGbaId, "gba"));
  assert.equal(badGba.state, "error"); assert.match(badGba.message, /CRC32/);
  await assert.rejects(access(path.join(process.env.ROMS_DIR, ".dsi-cache", "gba", badGbaId, "game.gba")));
  const broken = await wait(await service.prepare(wrongId));
  assert.equal(broken.state, "error");
  assert.match(broken.message, /MD5/);
  await assert.rejects(access(path.join(process.env.ROMS_DIR, ".dsi-cache", wrongId, "source.zip.part")));
  const badArchive = await wait(await service.prepare(invalidId));
  assert.equal(badArchive.state, "error");
  assert.match(badArchive.message, /ZIP/);
  await assert.rejects(service.gameFile(new Request(`http://local/api/dsi/file?id=${invalidId}`)), (error) => error.status === 409);
  await assert.rejects(service.appUpdate(new Request("http://local/api/dsi/update")), (error) => error.status === 503);
  await mkdir("public/downloads", { recursive: true });
  const update = Buffer.from(payload);
  update.fill(0, 0, 512); update.write("DSiDrop", 0, "ascii");
  update.writeUInt32LE(512, 0x20); update.writeUInt32LE(256, 0x2c);
  update.writeUInt32LE(768, 0x30); update.writeUInt32LE(256, 0x3c);
  await writeFile("public/downloads/DSiDrop.nds", update);
  const updateCrc = crc32(update).toString(16).padStart(8, "0");
  const updateStatus = await service.appUpdate(new Request("http://local/api/dsi/update"));
  assert((await updateStatus.text()).includes(`\t${update.length}\t${updateCrc}\tDSiDrop.nds\tDSiDrop `));
  const updateUrl = `http://local/api/dsi/update?file=1&crc=${updateCrc}&size=${update.length}`;
  const updateFile = await service.appUpdate(new Request(updateUrl));
  assert.deepEqual(Buffer.from(await updateFile.arrayBuffer()), update);
  const updateRange = await service.appUpdate(new Request(updateUrl, { headers: { Range: "bytes=1003-" } }));
  assert.equal(updateRange.status, 206);
  assert.deepEqual(Buffer.from(await updateRange.arrayBuffer()), update.subarray(1003));
  const updateHead = await service.appUpdate(new Request(updateUrl), true);
  assert.equal(updateHead.body, null);
  assert.equal(updateHead.headers.get("X-ROM-CRC32"), updateCrc);
  await assert.rejects(service.appUpdate(new Request(updateUrl.replace(updateCrc, "00000000"))), (error) => error.status === 409);
  update.write("WrongApp", 0, "ascii");
  await writeFile("public/downloads/DSiDrop.nds", update);
  await assert.rejects(service.appUpdate(new Request("http://local/api/dsi/update")), (error) => error.status === 502);
  console.log("DSi preparation: NDS ZIP/MD5, GBA raw/CRC32, system isolation/validation, ranges, persistent caches, single-flight and bad hash/ZIP cleanup passed");
} finally {
  globalThis.fetch = originalFetch;
  process.chdir(project);
  if (originalRomsDir === undefined) delete process.env.ROMS_DIR;
  else process.env.ROMS_DIR = originalRomsDir;
  restoreTypescript();

  assert(path.dirname(root) === path.join(project, ".cache"));
  await rm(root, { recursive: true, force: true });
}
