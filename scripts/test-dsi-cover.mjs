import { registerTypescript } from './test-support/typescript.mjs';
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, mkdir, writeFile, rm } from "node:fs/promises";
import path from "node:path";
import sharp from "sharp";

const project = process.cwd();
await mkdir(".cache", { recursive: true });
const root = await mkdtemp(path.join(project, ".cache", "dsi-cover-"));
const originalFetch = globalThis.fetch;
const { require, restore: restoreTypescript } = registerTypescript();

try {
  process.env.ROMS_DIR = path.join(root, "roms");
  process.chdir(root);
  await mkdir("public/data", { recursive: true });
  const nds = "a".repeat(32), gba = "1234abcd";
  for (const [system, id] of [["nds", nds], ["gba", gba]]) {
    await writeFile(`public/data/${system}.json`, JSON.stringify({
      meta: { id: system, coverBase: "https://test.invalid/" },
      roms: [
        { m: id, n: "Fixture", t: "Fixture", c: "Fixture (Test)", s: 1024, r: "World" },
        { m: "b".repeat(id.length), n: "Missing", t: "Missing", c: null, s: 1024, r: "World" },
        { m: "c".repeat(id.length), n: "Bad", t: "Bad", c: "Broken", s: 1024, r: "World" },
      ],
    }));
  }

  const png = await sharp({ create: { width: 32, height: 64, channels: 4, background: { r: 255, g: 0, b: 0, alpha: 1 } } }).png().toBuffer();
  let calls = 0;
  globalThis.fetch = async (url, options) => {
    calls++;
    assert(String(url).startsWith("https://test.invalid/"));
    assert(options.signal instanceof AbortSignal);
    await new Promise((resolve) => setTimeout(resolve, 10));
    return new Response(String(url).endsWith("Broken.png") ? "invalid image" : png);
  };
  const { dsiCover } = require(path.join(project, "src/lib/server/dsi-cover.ts"));
  const request = (system, id) => dsiCover(new URLSearchParams({ system, id }));
  const responses = await Promise.all([request("nds", nds), request("nds", nds)]);
  assert.equal(calls, 1, "Concurrent requests share the image conversion");
  for (const response of responses) {
    assert.equal(response.status, 200);
    assert.equal(response.headers.get("Content-Length"), "8200");
    const body = Buffer.from(await response.arrayBuffer());
    assert.equal(body.length, 8200);
    assert.equal(body.subarray(0, 8).toString(), "RFCOVER1");
    assert.equal(body.readUInt16LE(8), 0, "Left padding is transparent");
    assert.equal(body.readUInt16LE(8 + (32 * 64 + 32) * 2), 0x801f, "Center is opaque red in DS RGB555");
    assert.equal(body.readUInt16LE(8 + (32 * 64 + 63) * 2), 0);
  }
  assert.equal((await request("gba", gba)).status, 200);
  assert.equal(calls, 2, "NDS and GBA use separate cache keys");
  assert.equal((await request("nds", nds)).status, 200);
  assert.equal(calls, 2, "Successful covers are cached");
  assert.equal((await request("nds", "b".repeat(32))).status, 404);
  assert.equal(calls, 2, "No upstream fetch without a cover");
  assert.equal((await request("nds", "c".repeat(32))).status, 404);
  assert.equal((await request("nds", "c".repeat(32))).status, 404);
  assert.equal(calls, 3, "Invalid images are harmless cached misses");
  await assert.rejects(request("nds", gba), (error) => error.status === 400);
  await assert.rejects(request("gba", nds), (error) => error.status === 400);
  await assert.rejects(request("../gba", gba), (error) => error.status === 400);
  await assert.rejects(request("nds", "d".repeat(32)), (error) => error.status === 404);
  const large = await dsiCover(new URLSearchParams({ system: "nds", id: nds, size: "88" }));
  const largeBody = Buffer.from(await large.arrayBuffer());
  assert.equal(large.status, 200);
  assert.equal(large.headers.get("Content-Length"), "15500");
  assert.equal(largeBody.length, 15500);
  assert.equal(largeBody.subarray(0, 8).toString(), "RFCOVER2");
  assert.equal(largeBody.readUInt16LE(8), 88);
  assert.equal(largeBody.readUInt16LE(10), 88);
  assert.equal(largeBody.readUInt16LE(12), 0);
  assert.equal(largeBody.readUInt16LE(12 + (44 * 88 + 44) * 2), 0x801f);
  assert.equal(calls, 4, "Higher resolution has its own cache entry");
  assert.equal((await request("nds", nds)).status, 200, "Old clients still receive 64px covers");
  await assert.rejects(dsiCover(new URLSearchParams({ system: "nds", id: nds, size: "512" })), (error) => error.status === 400);

  await mkdir("public/downloads/apps/covers", { recursive: true });
  const app = Buffer.alloc(1024);
  app.writeUInt32LE(512, 0x20); app.writeUInt32LE(256, 0x2c);
  app.writeUInt32LE(768, 0x30); app.writeUInt32LE(256, 0x3c);
  const appId = createHash("md5").update(app).digest("hex");
  await writeFile("public/downloads/apps/Local App.nds", app);
  await writeFile("public/downloads/apps/covers/local-app.png", png);
  const library = await require(path.join(project, "src/lib/server/uploads.ts")).uploadedDataset();
  assert.equal(library.meta.withCover, 1);
  assert.equal(library.roms[0].c, "local-app");
  const previousCalls = calls;
  for (const size of ["64", "88"]) {
    const local = await dsiCover(new URLSearchParams({ system: "others", id: appId, size }));
    assert.equal(local.status, 200);
    assert.equal((await local.arrayBuffer()).byteLength, size === "64" ? 8200 : 15500);
  }
  assert.equal(calls, previousCalls, "Local app covers never require an upstream fetch");

  globalThis.__dsiCovers.clear();
  globalThis.fetch = async () => new Response("", { headers: { "Content-Length": String(3 * 1024 * 1024) } });
  assert.equal((await request("nds", nds)).status, 404);
  globalThis.__dsiCovers.clear();
  globalThis.fetch = async () => { throw new Error("offline"); };
  assert.equal((await request("gba", gba)).status, 404);
  console.log("DSi cover tests passed: NDS/GBA, RGB555, alpha, aspect ratio, fixed length, cache/deduplication, missing, invalid, oversized and offline images.");
} finally {
  process.chdir(project); globalThis.fetch = originalFetch;
  restoreTypescript();
  await rm(root, { recursive: true, force: true });
}
