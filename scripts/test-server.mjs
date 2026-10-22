import assert from "node:assert/strict";
import { test } from "node:test";
import path from "node:path";
import { registerTypescript } from "./test-support/typescript.mjs";

const { require, restore } = registerTypescript();

test("the server edition rejects desktop operations without reading user paths", async () => {
  try {
    for (const [file, methods] of [
      ["play", ["GET", "POST"]],
      ["emulators", ["GET", "POST", "DELETE"]],
      ["emulators/browse", ["GET"]],
    ]) {
      const route = require(path.resolve(`src/app/api/${file}/route.ts`));
      for (const method of methods) {
        const response = await route[method](new Request(`http://localhost/api/${file}`, { method }));
        assert.equal(response.status, 501);
        assert.match((await response.json()).error, /desktop edition/);
      }
    }
    const health = require(path.resolve("src/app/api/health/route.ts"));
    assert.deepEqual(await health.GET().json(), { status: "ok", application: "DSiDrop", version: "1.11.0" });
  } finally {
    restore();
  }
});
