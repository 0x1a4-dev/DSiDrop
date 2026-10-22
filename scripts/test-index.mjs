import assert from "node:assert/strict";
import { test } from "node:test";
import { parseName, regionFromFolder } from "./index/names.mjs";
import { matchCovers } from "./index/covers.mjs";

test("catalog names preserve regions, languages, revisions and dump flags", () => {
  assert.deepEqual(parseName("lost island (Europe) (En,Fr,De) (Rev 1) [!]"), {
    title: "Lost Island", region: "Europe", regions: ["Europe"],
    languages: ["En", "Fr", "De"], tags: ["Rev 1", "Verified"],
  });
  assert.deepEqual(parseName("Pocket (UE) (M3) [b1]"), {
    title: "Pocket", region: "USA", regions: ["USA", "Europe"],
    languages: [], tags: ["M3", "Bad Dump"],
  });
  assert.equal(regionFromFolder("Australia"), "Australia");
  assert.equal(regionFromFolder("unsorted"), null);
});

test("cover matching preserves exact names and GoodTools region aliases", () => {
  const { covers, exact, region } = matchCovers([
    { name: "Alpha & Omega (USA)", region: "USA" },
    { name: "Pocket (U)", region: "USA" },
  ], ["Alpha _ Omega (USA)", "Pocket (USA)"]);
  assert.equal(exact, 1);
  assert.equal(region, 1);
  assert.deepEqual(covers.get("Pocket (U)"), { file: "Pocket (USA)", approx: false });
});

test("cover matching distinguishes shared regions from alternate artwork", () => {
  const { covers } = matchCovers([
    { name: "Island (Europe)", region: "Europe" },
    { name: "Orbit (Japan)", region: "Japan" },
  ], ["Island (UE)", "Orbit (USA)"]);
  assert.deepEqual(covers.get("Island (Europe)"), { file: "Island (UE)", approx: false });
  assert.deepEqual(covers.get("Orbit (Japan)"), { file: "Orbit (USA)", approx: true });
});

test("a cover is assigned once and missing artwork remains missing", () => {
  const { covers } = matchCovers([
    { name: "Pocket (USA)", region: "USA" },
    { name: "Pocket (Japan)", region: "Japan" },
    { name: "Missing (Europe)", region: "Europe" },
  ], ["Pocket (USA)"]);
  assert.deepEqual([...covers.keys()], ["Pocket (USA)"]);
});
