import { primaryRegion, regionSet, regionToken, splitTopLevel, isLanguageToken } from './names.mjs';

function coverName(name) {
  return name.replace(/&/g, '_');
}

function canonical(name) {
  return name
    .toLowerCase()
    .replace(/\((?:rev\s*[a-z0-9]+|ndsi enhanced)\)/g, '')
    .replace(/\[[^\]]*\]/g, '')
    .replace(/[^a-z0-9]+/g, ' ')
    .trim()
    .replace(/\s+/g, ' ');
}

function regionCanonical(name) {
  let value = name.replace(/\[[^\]]*\]/g, ' ');
  value = value.replace(/\((rev\s*[a-z0-9]+|m\d+|ndsi enhanced)\)/gi, ' ');
  value = value.replace(/\(([^)]*)\)/g, (_full, inner) => {
    const parts = splitTopLevel(inner);
    if (parts.length > 1 && parts.every(isLanguageToken)) return ' ';
    const region = regionToken(inner);
    if (region) return ` ${region.join(' ')} `;
    if (isLanguageToken(inner)) return ' ';
    return ` ${inner} `;
  });
  return value.toLowerCase().replace(/[^a-z0-9]+/g, ' ').trim().replace(/\s+/g, ' ');
}

function titleKey(name) {
  return name
    .toLowerCase()
    .replace(/\([^)]*\)/g, ' ')
    .replace(/\[[^\]]*\]/g, ' ')
    .replace(/[^a-z0-9]+/g, ' ')
    .trim()
    .replace(/\s+/g, ' ');
}

function indexBy(names, keyFn) {
  const index = new Map();
  for (const name of names) {
    const key = keyFn(name);
    if (!index.has(key)) index.set(key, []);
    index.get(key).push(name);
  }
  return index;
}

const PREFERRED_REGIONS = ['USA', 'World', 'Europe', 'Japan'];

function matchCovers(roms, coverNames) {
  const covers = new Map();
  const used = new Set();
  const available = new Set(coverNames);

  const byCanonical = indexBy(coverNames, canonical);
  const byRegionCanonical = indexBy(coverNames, regionCanonical);
  const byTitle = indexBy(coverNames, titleKey);
  const coverRegions = new Map(coverNames.map((c) => [c, regionSet(c)]));

  const free = (list) => (list || []).filter((c) => !used.has(c));
  const assign = (rom, file, approx) => {
    covers.set(rom.name, { file, approx });
    used.add(file);
  };

  let exact = 0;
  let fuzzy = 0;
  let region = 0;
  let regionSubset = 0;
  let titleRegion = 0;
  let titleUnique = 0;

  for (const rom of roms) {
    const direct = coverName(rom.name);
    if (available.has(direct) && !used.has(direct)) {
      assign(rom, direct, false);
      exact++;
    }
  }

  for (const rom of roms) {
    if (covers.has(rom.name)) continue;
    const candidates = free(byCanonical.get(canonical(rom.name)));
    if (candidates.length === 1) {
      assign(rom, candidates[0], false);
      fuzzy++;
    }
  }

  for (const rom of roms) {
    if (covers.has(rom.name)) continue;
    const candidates = free(byRegionCanonical.get(regionCanonical(rom.name)));
    if (candidates.length === 1) {
      assign(rom, candidates[0], false);
      region++;
    }
  }

  for (const rom of roms) {
    if (covers.has(rom.name)) continue;
    if (!rom.region || rom.region === 'Unknown') continue;
    const candidates = free(byTitle.get(titleKey(rom.name))).filter((c) =>
      coverRegions.get(c)?.includes(rom.region),
    );
    if (candidates.length === 0) continue;
    const exactRegion = candidates.find((c) => primaryRegion(c) === rom.region);
    assign(rom, exactRegion ?? candidates[0], false);
    regionSubset++;
  }

  for (const rom of roms) {
    if (covers.has(rom.name)) continue;
    const candidates = free(byTitle.get(titleKey(rom.name)));
    if (candidates.length === 1) {
      assign(rom, candidates[0], true);
      titleUnique++;
    }
  }

  for (const rom of roms) {
    if (covers.has(rom.name)) continue;
    const candidates = free(byTitle.get(titleKey(rom.name)));
    if (candidates.length === 0) continue;
    let pick = null;
    if (rom.region && rom.region !== 'Unknown') {
      pick = candidates.find((c) => primaryRegion(c) === rom.region);
    }
    if (!pick) {
      pick = candidates.find((c) => PREFERRED_REGIONS.includes(primaryRegion(c)));
    }
    if (!pick) pick = candidates[0];
    assign(rom, pick, true);
    titleRegion++;
  }

  return { covers, exact, fuzzy, region, regionSubset, titleRegion, titleUnique };
}
export { matchCovers };
