import assert from "node:assert/strict";
import path from "node:path";
import { registerTypescript } from "./test-support/typescript.mjs";

const { require, restore } = registerTypescript();
try {
  const { parseThemes, themePath } = require(path.resolve("src/lib/server/themes.ts"));
  const root = "https://raw.githubusercontent.com/DS-Homebrew/twlmenu-extras/master/";
  const entry = (menu = "dsimenu", output = `/_nds/TWiLightMenu/${menu}/themes/Test/`) => ({
    info: { title: "Test", console: "Nintendo DSi", screenshots: [{ url: `${root}test.png` }] },
    Test: [{ type: "downloadFile", file: `${root}_nds/TWiLightMenu/${menu}/themes/Test.7z`, output: "/Test.7z" },
      { type: "extractFile", file: "/Test.7z", input: "Test/", output }],
  });
  const themes = parseThemes({ storeContent: [entry(), entry("extras"), entry("dsimenu", "/../../bad/")] });
  assert.equal(themes.length, 1);
  assert.equal(themes[0].input, "Test");
  assert.equal(themes[0].destination, "/_nds/TWiLightMenu/dsimenu/themes/Test");
  assert.match(themes[0].id, /^[a-f0-9]{32}$/);
  for (const name of ["../bad", "C:/bad", "a\\b", "a/../b", "a/NUL.txt", "a/b. "]) assert.throws(() => themePath(name));
  const { themeArchiveEntries, themeCrc } = require(path.resolve("src/lib/server/theme-package.ts"));
  assert.equal(themeCrc(Buffer.from("123456789")), 0xcbf43926);
  const listing = "Path = Test/background/top.png\nSize = 10\nAttributes = A\n\nPath = Test/theme.ini\nSize = 2\nAttributes = A";
  assert.deepEqual(themeArchiveEntries(listing, "Test").map((file) => file.relative), ["background/top.png", "theme.ini"]);
  assert.throws(() => themeArchiveEntries("Path = ../escape\nSize = 1", ""));
  assert.throws(() => themeArchiveEntries("Path = Test/link\nSize = 1\nSymbolic Link = /tmp", "Test"));
  assert.throws(() => themeArchiveEntries("Path = Test/bomb\nSize = 999999999", "Test"));
  console.log("PASS: theme metadata, destination allowlist and unsafe paths");
} finally { restore(); }