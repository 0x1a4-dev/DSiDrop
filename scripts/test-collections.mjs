import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import { registerTypescript } from "./test-support/typescript.mjs";
const { require } = registerTypescript();
const { CollectionLibrary } = require(path.resolve("src/lib/server/collections.ts"));
const directory = await mkdtemp(path.join(os.tmpdir(), "dsidrop-collections-"));
try {
  const file = path.join(directory, "collections.json"), resolve = async (id) => ({ rom: { t: `Game ${id}`, r: "World", c: "Game cover" }, data: { meta: { coverBase: "https://example.test/covers/" } } });
  const library = new CollectionLibrary(file, resolve);
  const created = await library.change({ action: "create", name: "Pokemon" }), id = created.selected;
  await library.change({ action: "add", id, items: [{ system: "nds", id: "a".repeat(32) }, { system: "gba", id: "b".repeat(8) }] });
  await library.change({ action: "add", id, items: [{ system: "nds", id: "a".repeat(32) }] });
  assert.equal((await library.snapshot()).collections[0].items.length, 2);
  assert.equal((await library.snapshot()).collections[0].items[0].cover, "https://example.test/covers/Game%20cover.png");
  await library.change({ action: "rename", id, name: "Favorites" });
  assert.equal((await new CollectionLibrary(file, resolve).snapshot()).collections[0].name, "Favorites");
  assert.equal((await new CollectionLibrary(file, resolve).snapshot()).collections[0].items[0].cover, "https://example.test/covers/Game%20cover.png");
  const unavailable = new CollectionLibrary(file, async () => { throw new Error("Unavailable game"); });
  assert.equal((await unavailable.snapshot()).collections[0].items[0].cover, null);
  await assert.rejects(() => library.change({ action: "add", id, items: [{ system: "nes", id: "123" }] }));
  await library.change({ action: "remove", id, game: "a".repeat(32), system: "nds" });
  assert.equal((await library.snapshot()).collections[0].items.length, 1);
  await library.change({ action: "delete", id });
  assert.equal((await library.snapshot()).collections.length, 0);
  console.log("PASS: collection persistence, rename, deduplication, removal and system validation");
} finally { await rm(directory, { recursive: true, force: true }); }