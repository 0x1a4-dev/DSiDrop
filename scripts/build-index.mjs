#!/usr/bin/env node
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { OUT_DIR } from './index/cache.mjs';
import { parseName, regionFromFolder } from './index/names.mjs';
import { matchCovers } from './index/covers.mjs';
import { loadRomList, loadCoverList } from './index/sources.mjs';
import { SYSTEMS, downloadBase } from './index/systems.mjs';

function fallbackId(file) {
  return createHash('md5').update(file).digest('hex');
}

async function buildSystem(system) {
  console.log(`\n== ${system.label} (${system.id}) ==`);
  const symlinkCache = system.coverCache.replace('tree', 'symlinks');
  const [romList, coverData] = await Promise.all([
    loadRomList(system),
    loadCoverList(system.coverRepo, system.coverCache, symlinkCache),
  ]);
  const coverList = coverData.names;
  const resolveCover = (file) =>
    file && coverData.resolve.has(file) ? coverData.resolve.get(file) : file;
  console.log(`  ${romList.length} ROMs, ${coverList.length} covers found.`);

  const romRefs = romList.map((rom) => {
    let region = parseName(rom.name).region;
    if (region === 'Unknown' && rom.dir) {
      region = regionFromFolder(rom.dir) || 'Unknown';
    }
    return { name: rom.name, region };
  });
  const {
    covers,
    exact,
    fuzzy,
    region,
    regionSubset,
    titleRegion,
    titleUnique,
  } = matchCovers(romRefs, coverList);
  const covered = exact + fuzzy + region + regionSubset + titleRegion + titleUnique;
  console.log(
    `  Cover matches: exact: ${exact}, canonical: ${fuzzy}, Region: ${region}, ` +
      `region subset: ${regionSubset}, unique title: ${titleUnique}, ` +
      `title/region: ${titleRegion}, missing: ${romList.length - covered}`,
  );

  const regionCounts = new Map();
  const tagCounts = new Map();
  const languageCounts = new Map();

  const roms = romList.map((rom) => {
    const parsed = parseName(rom.name);
    let primary = parsed.region;
    if (primary === 'Unknown' && rom.dir) {
      primary = regionFromFolder(rom.dir) || 'Unknown';
    }
    const cover = covers.get(rom.name) || null;
    const coverFile = resolveCover(cover ? cover.file : null);
    regionCounts.set(primary, (regionCounts.get(primary) || 0) + 1);
    for (const tag of parsed.tags) tagCounts.set(tag, (tagCounts.get(tag) || 0) + 1);
    for (const lang of parsed.languages) {
      languageCounts.set(lang, (languageCounts.get(lang) || 0) + 1);
    }

    const entry = {
      n: rom.name,
      t: parsed.title,
      r: primary,
      s: rom.size,
      m: rom.id || fallbackId(rom.file || rom.name),
      c: coverFile,
    };
    if (rom.sha1) entry.h = rom.sha1;
    if (rom.ext && rom.ext !== system.ext) entry.e = rom.ext;
    if (rom.dir) entry.d = rom.dir;
    if (cover?.approx) entry.x = 1;
    if (parsed.regions.length > 1) entry.a = parsed.regions;
    if (parsed.languages.length) entry.l = parsed.languages.join(',');
    if (parsed.tags.length) entry.g = parsed.tags;
    return entry;
  });

  const meta = {
    id: system.id,
    label: system.label,
    short: system.short,
    ext: system.ext,
    arch: system.arch,
    out: system.out,
    checksum: system.checksum,
    source: `https://archive.org/details/${system.itemId}`,
    download: downloadBase(system),
    coverSource: `https://github.com/${system.coverRepo}`,
    coverBase: `https://raw.githubusercontent.com/${system.coverRepo}/master/Named_Boxarts/`,
    generatedAt: new Date().toISOString(),
    total: roms.length,
    withCover: roms.filter((r) => r.c).length,
    regions: [...regionCounts.entries()]
      .sort((a, b) => b[1] - a[1])
      .map(([name, count]) => ({ name, count })),
    tags: [...tagCounts.entries()]
      .sort((a, b) => b[1] - a[1])
      .slice(0, 40)
      .map(([name, count]) => ({ name, count })),
    languages: [...languageCounts.entries()]
      .sort((a, b) => b[1] - a[1])
      .slice(0, 40)
      .map(([name, count]) => ({ name, count })),
  };

  return { meta, roms };
}

async function main() {
  console.log('Building ROM index ...');
  mkdirSync(OUT_DIR, { recursive: true });

  const index = [];
  for (const system of SYSTEMS) {
    const { meta, roms } = await buildSystem(system);
    const outFile = join(OUT_DIR, system.file);
    writeFileSync(outFile, JSON.stringify({ meta, roms }));
    const kb = (readFileSync(outFile).length / 1024).toFixed(0);
    console.log(`  -> ${system.file} (${kb} kB)`);
    console.log(
      `     Coverage: ${meta.withCover}/${meta.total} (${((meta.withCover / meta.total) * 100).toFixed(1)} % with covers)`,
    );
    index.push({
      id: meta.id,
      label: meta.label,
      short: meta.short,
      data: `/data/${system.file}`,
      ext: meta.ext,
      arch: meta.arch,
      out: meta.out,
      checksum: meta.checksum,
      total: meta.total,
      withCover: meta.withCover,
      generatedAt: meta.generatedAt,
    });
  }

  writeFileSync(join(OUT_DIR, 'systems.json'), JSON.stringify(index, null, 2));
  console.log(`\nWritten: ${join(OUT_DIR, 'systems.json')} (${index.length} systems)`);
}

main().catch((error) => {
  console.error(`Error: ${error.message}`);
  process.exitCode = 1;
});
