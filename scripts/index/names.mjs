const REGIONS_FULL = new Set([
  'europe', 'usa', 'japan', 'germany', 'france', 'united kingdom', 'australia',
  'canada', 'korea', 'italy', 'spain', 'netherlands', 'world', 'china', 'taiwan',
  'asia', 'denmark', 'norway', 'sweden', 'finland', 'russia', 'brazil', 'poland',
  'portugal', 'unknown', 'new zealand', 'argentina', 'mexico', 'india',
  'singapore', 'hong kong', 'uk',
]);

const GT_SINGLE = {
  u: 'USA', e: 'Europe', j: 'Japan', w: 'World', a: 'Australia',
  c: 'Canada', f: 'France', g: 'Germany', s: 'Spain', i: 'Italy',
  k: 'Korea', t: 'Taiwan',
};
const GT_TOKENS = {
  br: 'Brazil', ch: 'China', nl: 'Netherlands', as: 'Asia',
  uk: 'United Kingdom', unk: 'Unknown', us: 'USA', eu: 'Europe',
  jp: 'Japan', au: 'Australia', kr: 'Korea', cn: 'China', tw: 'Taiwan',
  ca: 'Canada',
};

const LANGS = new Set([
  'en', 'ja', 'fr', 'de', 'es', 'it', 'nl', 'pt', 'sv', 'no', 'da', 'fi', 'ko',
  'zh', 'ru', 'pl', 'cs', 'hu', 'el', 'tr', 'ar', 'he', 'th', 'id', 'vi', 'uk',
]);

const TYPE_MAP = [
  [/^demo$/i, 'Demo'],
  [/^proto(?:type)?$/i, 'Proto'],
  [/^beta/i, 'Beta'],
  [/^sample$/i, 'Sample'],
  [/^unl$/i, 'Unl'],
  [/^hack$/i, 'Hack'],
  [/^pirate$/i, 'Pirate'],
  [/^translation$/i, 'Translation'],
  [/^trainer$/i, 'Trainer'],
  [/^(?:wi-?fi\s+)?kiosk$/i, 'Kiosk'],
  [/^ndsi enhanced$/i, 'NDSi'],
  [/^save data$/i, 'Save Data'],
  [/^program$/i, 'Program'],
  [/^not for resale$/i, 'NFR'],
  [/^aftermarket$/i, 'Aftermarket'],
  [/^rev\s*([a-z0-9]+)$/i, null],
];

function splitTopLevel(inner) {
  return inner.split(',').map((s) => s.trim()).filter(Boolean);
}

function capitalize(value) {
  return value.charAt(0).toUpperCase() + value.slice(1);
}

function titleCaseRegion(value) {
  return value.split(' ').map((w) => capitalize(w.trim())).join(' ');
}

function isLanguageToken(token) {
  return LANGS.has(token.trim().toLowerCase());
}

function regionToken(inner) {
  const lower = inner.trim().toLowerCase();
  if (!lower) return null;
  if (REGIONS_FULL.has(lower)) return [titleCaseRegion(lower)];
  const key = lower.replace(/[\s.&-]+/g, '');
  if (!key) return null;
  if (GT_TOKENS[key]) return [GT_TOKENS[key]];
  if (key.length === 1) return GT_SINGLE[key] ? [GT_SINGLE[key]] : null;
  if (
    /^[a-z]+$/.test(key) &&
    [...key].every((c) => GT_SINGLE[c]) &&
    new Set(key).size === key.length
  ) {
    return [...key].map((c) => GT_SINGLE[c]);
  }
  return null;
}

function flagLabel(inner) {
  if (inner === '!') return 'Verified';
  if (inner === 'T') return 'Trained';
  const i = inner.toLowerCase();
  if (/^a\d*$/.test(i)) return 'Alternate';
  if (/^b\d*$/.test(i)) return 'Bad Dump';
  if (/^o\d*$/.test(i)) return 'Overdump';
  if (/^h\d*$/.test(i)) return 'Hack';
  if (/^t\d*$/.test(i)) return 'Translation';
  if (/^f\d*$/.test(i)) return 'Fixed';
  if (/^p\d*$/.test(i)) return 'Pirate';
  if (i === 'c') return 'Color';
  if (i === 's') return 'Sound Fix';
  return inner;
}

function smartTitle(title) {
  if (!title || /[A-Z]/.test(title)) return title;
  const small = new Set([
    'a', 'an', 'and', 'the', 'of', 'in', 'on', 'to', 'for', 'with', 'vs',
    'de', 'la', 'le', 'du', 'el', 'or',
  ]);
  let firstWord = true;
  return title
    .split(/(\s+)/)
    .map((word) => {
      if (!word.trim() || !/[a-z]/i.test(word)) return word;
      const lower = word.toLowerCase();
      if (!firstWord && small.has(lower)) return lower;
      firstWord = false;
      return word.replace(/[a-z]/i, (c) => c.toUpperCase());
    })
    .join('');
}

function parseName(name) {
  const regions = [];
  const langs = [];
  const tags = [];

  for (const match of name.matchAll(/\(([^)]*)\)/g)) {
    const inner = match[1].trim();
    if (!inner) continue;
    const parts = splitTopLevel(inner);

    if (parts.length > 1 && parts.every(isLanguageToken)) {
      for (const part of parts) langs.push(capitalize(part));
      continue;
    }
    if (parts.length > 1 && parts.every((part) => regionToken(part))) {
      for (const part of parts) regions.push(...regionToken(part));
      continue;
    }
    if (/^m\d+$/i.test(inner)) {
      tags.push(inner.toUpperCase());
      continue;
    }
    const region = regionToken(inner);
    if (region) {
      regions.push(...region);
      continue;
    }
    if (isLanguageToken(inner)) {
      langs.push(capitalize(inner));
      continue;
    }
    const rev = inner.match(/^rev\s*([a-z0-9]+)$/i);
    if (rev) {
      tags.push(`Rev ${rev[1]}`);
      continue;
    }
    let mapped = null;
    for (const [re, label] of TYPE_MAP) {
      if (re.test(inner)) {
        mapped = label ?? inner;
        break;
      }
    }
    tags.push(mapped ?? inner);
  }

  for (const match of name.matchAll(/\[([^\]]*)\]/g)) {
    const inner = match[1].trim();
    if (inner) tags.push(flagLabel(inner));
  }

  const rawTitle = name
    .replace(/\s*\([^)]*\)/g, '')
    .replace(/\s*\[[^\]]*\]/g, '')
    .replace(/\s{2,}/g, ' ')
    .trim();

  return {
    title: smartTitle(rawTitle),
    region: regions[0] || 'Unknown',
    regions: [...new Set(regions)],
    languages: [...new Set(langs)],
    tags: [...new Set(tags)],
  };
}

function primaryRegion(name) {
  for (const match of name.matchAll(/\(([^)]*)\)/g)) {
    const inner = match[1].trim();
    const parts = splitTopLevel(inner);
    if (parts.length > 1 && parts.every(isLanguageToken)) continue;
    const region = regionToken(inner);
    if (region) return region[0];
  }
  return 'Unknown';
}

function regionSet(name) {
  const regions = [];
  for (const match of name.matchAll(/\(([^)]*)\)/g)) {
    const inner = match[1].trim();
    const parts = splitTopLevel(inner);
    if (parts.length > 1 && parts.every(isLanguageToken)) continue;
    const region = regionToken(inner);
    if (region) regions.push(...region);
  }
  return [...new Set(regions)];
}

const FOLDER_REGION = {
  australia: 'Australia', canada: 'Canada', europe: 'Europe', usa: 'USA',
  japan: 'Japan', world: 'World', france: 'France', germany: 'Germany',
  spain: 'Spain', italy: 'Italy', asia: 'Asia', korea: 'Korea', china: 'China',
  taiwan: 'Taiwan', brazil: 'Brazil', netherlands: 'Netherlands',
  country_unk: 'Unknown', unknown: 'Unknown',
};

function regionFromFolder(dir) {
  const key = dir.toLowerCase().replace(/[^a-z]+/g, '_').replace(/^_|_$/g, '');
  return FOLDER_REGION[key] ?? null;
}
export { parseName, regionFromFolder, primaryRegion, regionSet, regionToken, splitTopLevel, isLanguageToken };
