import assert from "node:assert/strict";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { createHash } from "node:crypto";
import path from "node:path";

const base = process.env.DSI_TEST_URL || "http://127.0.0.1:3000";
const dataset = JSON.parse(await readFile("public/data/nds.json", "utf8"));
const romDir = path.join(process.env.ROMS_DIR || "roms", "nds");
let sample;
let source;
for (const rom of dataset.roms) {
  try {
    source = await readFile(path.join(romDir, `${rom.n}.nds`));
    sample = rom;
    break;
  } catch { /* choose an available local game */ }
}
assert(sample, "At least one already downloaded NDS file is needed for the integration test");
async function request(route, options) {
  return fetch(`${base}/api/dsi/${route}`, { ...options, signal: AbortSignal.timeout(30_000) });
}
const all = await request("catalog");
assert.equal(all.status, 200);
assert.equal(all.headers.get("content-type"), "text/plain; charset=us-ascii");
assert.equal(all.headers.get("transfer-encoding"), null);
const catalog = await all.text();
const gbaCatalogResponse = await request("catalog?system=gba");
assert.equal(gbaCatalogResponse.status, 200);
const gbaRows = (await gbaCatalogResponse.text()).trim().split("\n");
assert(gbaRows[0].startsWith("RFCATALOG1\t"));
for (const row of gbaRows.slice(1)) assert.match(row.split("\t")[0], /^[0-9a-f]{8}$/);
assert.equal((await request("catalog?system=../gba")).status, 400);
assert.equal((await request(`prepare?system=gba&id=${sample.m}`, { method: "POST" })).status, 400);
assert(Buffer.byteLength(catalog) < 4096);
assert(/^[\x09\x0a\x20-\x7e]+$/.test(catalog));
assert.equal(Number(all.headers.get("content-length")), Buffer.byteLength(catalog));
assert.equal(catalog.trim().split("\n").length, 11);
const european = (await (await request("catalog?q=mario&region=europe&page=999999")).text()).trim().split("\n");
const header = european[0].split("\t");
assert.equal(Number(header[2]), Number(header[3]) - 1, "Pages are clamped");
for (const row of european.slice(1)) assert.equal(row.split("\t")[2].toLowerCase(), "europe");
assert((await (await request("catalog?q=__no_such_game__")).text()).startsWith("RFCATALOG1\t0\t0\t1\n"));
assert((await (await request("catalog?page=-10")).text()).startsWith("RFCATALOG1\t"));
assert.equal((await request("prepare?id=../../file", { method: "POST" })).status, 400);
assert.equal((await request(`prepare?id=${"0".repeat(32)}`, { method: "POST" })).status, 404);
assert.equal((await request(`prepare?id=${sample.m}`)).status, 405, "Preparation requires POST");
assert.equal((await request(`prepare?id=${sample.m}`, { method: "POST" })).status, 200);
let ready;
for (let i = 0; i < 50; i++) {
  const result = await request(`status?id=${sample.m}`);
  assert.equal(result.status, 200);
  ready = await result.text();
  if (ready.split("\t")[1] === "ready") break;
  assert.notEqual(ready.split("\t")[1], "error", ready);
  await new Promise((resolve) => setTimeout(resolve, 200));
}
const fields = ready.trim().split("\t");
assert.equal(fields[1], "ready", ready);
assert.equal(Number(fields[4]), source.length);
assert.match(fields[5], /^[0-9a-f]{8}$/);
const expectedHash = createHash("sha256").update(source).digest("hex");
const download = await request(`file?id=${sample.m}`);
assert.equal(download.status, 200);
assert.equal(Number(download.headers.get("content-length")), source.length);
assert.equal(download.headers.get("transfer-encoding"), null);
const received = Buffer.from(await download.arrayBuffer());
assert.equal(createHash("sha256").update(received).digest("hex"), expectedHash);

let crc = 0xffffffff;
for (const byte of received) {
  crc ^= byte;
  for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
}
assert.equal(((crc ^ 0xffffffff) >>> 0).toString(16).padStart(8, "0"), fields[5]);
const partial = await request(`file?id=${sample.m}`, { headers: { Range: "bytes=100003-" } });
assert.equal(partial.status, 206);
assert.equal(partial.headers.get("content-range"), `bytes 100003-${source.length - 1}/${source.length}`);
assert.deepEqual(Buffer.from(await partial.arrayBuffer()), source.subarray(100003));
const bounded = await request(`file?id=${sample.m}`, { headers: { Range: "bytes=512-1023" } });
assert.equal(bounded.status, 206);
assert.deepEqual(Buffer.from(await bounded.arrayBuffer()), source.subarray(512, 1024));
assert.equal((await request(`file?id=${sample.m}`, { method: "HEAD" })).headers.get("content-length"), String(source.length));
for (const range of ["bytes=-1", "bytes=999999999-", "bytes=4-2", "bytes=0-1,3-4", "bytes=abc-"]) {
  assert.equal((await request(`file?id=${sample.m}`, { headers: { Range: range } })).status, 416);
}
await mkdir("nds/build", { recursive: true });
await writeFile("nds/build/live-catalog.tsv", catalog);
await writeFile("nds/build/live-status.tsv", ready);
console.log(`DSi API: catalog, filters, pagination, validation, preparation, ${source.length} byte download, SHA256, CRC32, resume and ranges passed`);
