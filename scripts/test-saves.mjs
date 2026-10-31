import assert from "node:assert/strict";
import { access, mkdtemp, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import Module from "node:module";
import { registerTypescript } from "./test-support/typescript.mjs";
const { require } = registerTypescript();
const { SaveLibrary, savePath } = require(path.resolve("src/lib/server/saves.ts"));
const { themeCrc } = require(path.resolve("src/lib/server/theme-package.ts"));
const directory = await mkdtemp(path.join(os.tmpdir(), "dsidrop-saves-"));
try {
  const library = new SaveLibrary(directory), mac = "020000000001", target = "/roms/nds/saves/Pokemon.sav";
  for (const unsafe of ["/../a.sav", "/roms/./a.sav", "fat:/a.sav", "/roms/a.nds", "/roms//a.sav"]) assert.throws(() => savePath(unsafe));
  const poll = (extra = {}) => library.poll(new URLSearchParams({ mac, session: "1234", ...extra }));
  for (let version = 0; version < 2; version++) {
    await library.change({ mac, action: "backup" });
    const job = (await poll()).trim().split("\t")[2], bytes = Buffer.from(`save version ${version}`);
    const url = new URL("http://localhost/api/saves/sync");
    url.search = new URLSearchParams({ mac, job, path: Buffer.from(target).toString("hex"), crc: themeCrc(bytes).toString(16).padStart(8, "0") });
    const upload = () => library.upload(new Request(url, { method: "PUT", headers: { "Content-Length": String(bytes.length) }, body: bytes }));
    await upload(); await upload();
    await poll({ finished: job, ok: "1" });
  }
  const snapshot = await new SaveLibrary(directory).snapshot(mac);
  assert.equal(snapshot.backups.length, 2);
  await library.change({ mac, action: "restore", backup: snapshot.backups[0].id });
  const restore = (await poll()).trim().split("\t");
  assert.equal(restore[1], "restore"); assert.equal(Buffer.from(restore[3], "hex").toString(), target);
  assert.equal((await library.file(mac, null, restore[2])).bytes.toString(), "save version 0");
  await assert.rejects(() => library.change({ mac, delete: snapshot.backups[0].id }));
  await poll({ finished: restore[2], ok: "1" });
  await library.change({ mac, delete: snapshot.backups[0].id });
  await assert.rejects(() => access(path.join(directory, `${snapshot.backups[0].id}.sav`)));
  await library.change({ mac, automatic: true });
  const automatic = (await poll({ session: "5678" })).trim().split("\t");
  assert.equal(automatic[1], "backup");
  assert.equal(await poll({ session: "5678", finished: automatic[2], ok: "1" }), "RFSAVE1\tnone\n");
  assert.equal(await poll({ session: "5678" }), "RFSAVE1\tnone\n");
  await library.change({ mac, action: "backup", path: target });
  assert.equal(await poll({ session: "5678" }), "RFSAVE1\tnone\n");
  const scoped = (await poll({ session: "5678", cap: "1" })).trim().split("\t");
  assert.equal(scoped[1], "backup"); assert.equal(Buffer.from(scoped[3], "hex").toString(), target);
  const bytes = Buffer.from("selected save"), url = new URL("http://localhost/api/saves/sync");
  url.search = new URLSearchParams({ mac, job: scoped[2], path: Buffer.from("/wrong.sav").toString("hex"), crc: themeCrc(bytes).toString(16).padStart(8, "0") });
  await assert.rejects(() => library.upload(new Request(url, { method: "PUT", headers: { "Content-Length": String(bytes.length) }, body: bytes })));
  await poll({ session: "5678", cap: "1", finished: scoped[2], ok: "1" });
  const destination = "020000000002", destinationPath = "/games/saves/Pokemon [12345678].sav";
  const sourceBackup = (await library.snapshot(mac)).backups[0];
  await library.change({ mac: destination, action: "transfer", sourceMac: mac, backup: sourceBackup.id, path: destinationPath });
  const destinationPoll = (extra = {}) => library.poll(new URLSearchParams({ mac: destination, session: "abcd", ...extra }));
  const transfer = (await destinationPoll()).trim().split("\t");
  assert.equal(transfer[1], "restore"); assert.equal(Buffer.from(transfer[3], "hex").toString(), destinationPath);
  assert.equal((await library.file(destination, null, transfer[2])).bytes.toString(), "save version 1");
  await assert.rejects(() => library.file(destination, sourceBackup.id, null));
  await assert.rejects(() => library.change({ mac, delete: sourceBackup.id }));
  const oldDestination = Buffer.from("previous destination save");
  const destinationUrl = new URL("http://localhost/api/saves/sync");
  destinationUrl.search = new URLSearchParams({ mac: destination, job: transfer[2], path: Buffer.from(destinationPath).toString("hex"), crc: themeCrc(oldDestination).toString(16).padStart(8, "0") });
  await library.upload(new Request(destinationUrl, { method: "PUT", headers: { "Content-Length": String(oldDestination.length) }, body: oldDestination }));
  const destinationBackup = (await library.snapshot(destination)).backups[0];
  assert.equal(destinationBackup.path, destinationPath);
  assert.equal((await library.file(destination, destinationBackup.id, null)).bytes.toString(), "previous destination save");
  await destinationPoll({ finished: transfer[2], ok: "1" });
  assert.equal((await library.file(mac, sourceBackup.id, null)).bytes.toString(), "save version 1");
  const fingerprint = "1234567890abcdef";
  const startup = (extra = {}) => library.poll(new URLSearchParams({ mac, session: "abcd1234", fingerprint, ...extra }));
  const startupJob = (await startup()).trim().split("\t");
  assert.equal(startupJob[1], "backup");
  assert.equal(await startup({ finished: startupJob[2], ok: "1" }), "RFSAVE1\tnone\n");
  assert.equal(await startup({ session: "abcd1235" }), "RFSAVE1\tnone\n", "Unchanged app startup skips automatic backup");
  const reopened = new SaveLibrary(directory);
  assert.equal(await reopened.poll(new URLSearchParams({ mac, session: "abcd1235", fingerprint: "abcdef1234567890", startup: "0" })), "RFSAVE1\tnone\n", "Server restart during an active app does not trigger a backup");
  assert.equal(await reopened.poll(new URLSearchParams({ mac, session: "abcd1235", fingerprint })), "RFSAVE1\tnone\n", "Server restart retains save fingerprint");
  const changed = (await startup({ session: "abcd1236", fingerprint: "abcdef1234567890" })).trim().split("\t");
  assert.equal(changed[1], "backup");
  await startup({ session: "abcd1236", fingerprint: "abcdef1234567890", finished: changed[2], ok: "0" });
  assert.equal((await startup({ session: "abcd1237", fingerprint: "abcdef1234567890" })).trim().split("\t")[1], "backup", "Failed backup is retried on next changed startup");
  assert.throws(() => startup({ fingerprint: "invalid" }));
  const previousResolver = Module._resolveFilename;
  Module._resolveFilename = function (specifier, parent, ...options) {
    return previousResolver.call(this, specifier.startsWith("@/") ? path.resolve("src", specifier.slice(2)) : specifier, parent, ...options);
  };
  const nativeStore = require(path.resolve("src/lib/server/saves.ts")).saves;
  const previousSnapshot = nativeStore.snapshot, previousChange = nativeStore.change;
  const changes = [];
  try {
    const native = require(path.resolve("src/app/api/dsi/saves/route.ts"));
    nativeStore.snapshot = async () => ({ backups: Array.from({ length: 8 }, (_, index) => ({ ...sourceBackup,
      id: (index + 1).toString(16).padStart(32, "0"), path: target, createdAt: `2026-10-${String(index + 1).padStart(2, "0")}T12:30:00.000Z`,
    })), tasks: [], automatic: { [mac]: true } });
    nativeStore.change = async (body) => { changes.push(body); return { ok: true }; };
    const nativeUrl = (params = {}) => new URL(`http://localhost/api/dsi/saves?${new URLSearchParams({ mac, view: "backups", path: Buffer.from("/roms/nds/Pokemon.nds").toString("hex"), ...params })}`);
    const firstPage = await native.GET(new Request(nativeUrl())), firstText = await firstPage.text();
    assert.equal(firstPage.status, 200);
    assert.equal(Number(firstPage.headers.get("Content-Length")), Buffer.byteLength(firstText));
    assert.match(firstText, /^RFSAVES1\tbackups\t6\t8\t1\n/);
    assert.match(firstText.split("\n")[1], /^00000000000000000000000000000008\t2026-10-08 12:30\t/);
    assert.equal(firstText.trimEnd().split("\n").length, 7);
    assert.match(await (await native.GET(new Request(nativeUrl({ page: "1" })))).text(), /^RFSAVES1\tbackups\t2\t8\t1\n/);
    for (const params of [{ path: "bad" }, { page: "-1" }, { path: Buffer.from("/../Pokemon.nds").toString("hex") }]) {
      const result = await native.GET(new Request(nativeUrl(params)));
      assert.equal(result.status, 400); assert.match(await result.text(), /^RFSAVES1\terror\t/);
    }
    const backupRequest = await native.POST(new Request(nativeUrl({ action: "backup", path: Buffer.from(target).toString("hex") }), { method: "POST" }));
    assert.equal(await backupRequest.text(), "RFSAVES1\tok\n");
    assert.deepEqual(changes.at(-1), { mac, action: "backup", path: target });
    assert.equal((await native.POST(new Request(nativeUrl({ action: "automatic", enabled: "0" }), { method: "POST" }))).status, 200);
    assert.deepEqual(changes.at(-1), { mac, automatic: false });
    assert.equal((await native.POST(new Request(nativeUrl({ action: "automatic", enabled: "2" }), { method: "POST" }))).status, 400);
    assert.equal((await native.POST(new Request(nativeUrl({ action: "restore", backup: "00000000000000000000000000000001", path: Buffer.from("/wrong.sav").toString("hex") }), { method: "POST" }))).status, 404);
    assert.equal(changes.length, 2, "Rejected native requests must not modify saves");
    console.log("PASS: native save pagination, English protocol, Content-Length, scoped backups, automatic settings and rejected unsafe requests");
  } finally {
    nativeStore.snapshot = previousSnapshot; nativeStore.change = previousChange;
    Module._resolveFilename = previousResolver;
  }
  console.log("PASS: automatic save backups only for changed startups, including persisted state and retry after failure");
  console.log("PASS: cross-device restore uses destination path and preserves source and destination backups");
  console.log("PASS: save versions, persistence, retry deduplication, restore selection and unsafe paths");
} finally { await rm(directory, { recursive: true, force: true }); }