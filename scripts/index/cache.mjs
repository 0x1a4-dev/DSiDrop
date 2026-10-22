import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = fileURLToPath(new URL('../../', import.meta.url));
const CACHE_DIR = join(ROOT, '.cache');
export const OUT_DIR = join(ROOT, 'public', 'data');
const args = new Set(process.argv.slice(2));
const OFFLINE = args.has('--offline');
const REFRESH = args.has('--refresh');
const CACHE_MAX_AGE_MS = 24 * 60 * 60 * 1000;

async function fetchJson(url, headers = {}) {
  const res = await fetch(url, {
    headers: { 'User-Agent': 'roms-finder-indexer/1.0', ...headers },
  });
  if (!res.ok) throw new Error(`HTTP ${res.status} for ${url}`);
  return res.json();
}

function readCache(name) {
  const file = join(CACHE_DIR, name);
  if (!existsSync(file)) return null;
  try {
    return JSON.parse(readFileSync(file, 'utf8'));
  } catch {
    return null;
  }
}

function writeCache(name, data) {
  mkdirSync(CACHE_DIR, { recursive: true });
  writeFileSync(join(CACHE_DIR, name), JSON.stringify(data));
}

async function cached(name, loader) {
  const cachedValue = readCache(name);
  const fresh =
    cachedValue && Date.now() - (cachedValue.__fetchedAt || 0) < CACHE_MAX_AGE_MS;
  if (cachedValue && fresh && !REFRESH) {
    console.log(`  [cache] ${name}`);
    return cachedValue.data;
  }
  if (OFFLINE) {
    if (!cachedValue) throw new Error(`--offline: no cached data for ${name}`);
    console.log(`  [cache/offline] ${name}`);
    return cachedValue.data;
  }
  console.log(`  [load]  ${name}`);
  const data = await loader();
  writeCache(name, { __fetchedAt: Date.now(), data });
  return data;
}
export { fetchJson, cached };
