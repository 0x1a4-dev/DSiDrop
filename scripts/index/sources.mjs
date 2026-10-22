import { cached, fetchJson } from './cache.mjs';

async function loadItemMetadata(itemId) {
  return cached(`ia-metadata-${itemId}.json`, () =>
    fetchJson(`https://archive.org/metadata/${itemId}`),
  );
}

function splitFileEntry(fileName, ext) {
  const base = fileName.slice(0, fileName.length - ext.length - 1);
  const slash = base.lastIndexOf('/');
  return {
    name: slash >= 0 ? base.slice(slash + 1) : base,
    dir: slash >= 0 ? base.slice(0, slash) : '',
  };
}

async function loadMetadataRomList(system) {
  const data = await loadItemMetadata(system.itemId);
  const wanted = system.source.extensions;
  return data.files
    .filter((f) => typeof f.name === 'string')
    .map((f) => {
      const match = /\.([a-z0-9]+)$/i.exec(f.name);
      const ext = match ? match[1].toLowerCase() : '';
      return { file: f.name, ext, size: Number(f.size) || 0, id: f.md5 || f.crc32 || '', sha1: f.sha1 || '' };
    })
    .filter((entry) => wanted.includes(entry.ext))
    .map((entry) => ({
      ...splitFileEntry(entry.file, entry.ext),
      ext: entry.ext,
      size: entry.size,
      id: entry.id,
      sha1: entry.sha1,
    }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

async function loadRemoteZipRomList(system) {
  const meta = await loadItemMetadata(system.itemId);
  const zipFile =
    meta.files.find((f) => f.name === system.source.zipName) ||
    meta.files.find((f) => /\.zip$/i.test(f.name));
  if (!zipFile) throw new Error(`No ZIP found in ${system.itemId}`);
  const serverUrl = `https://${meta.server}${meta.dir}/${encodeURIComponent(zipFile.name)}`;
  const entries = await cached(`zip-entries-${system.itemId}.json`, () =>
    readRemoteZipEntries(serverUrl),
  );
  const wanted = system.source.extensions;
  return entries
    .map((entry) => {
      const match = /\.([a-z0-9]+)$/i.exec(entry.name);
      const ext = match ? match[1].toLowerCase() : '';
      return { file: entry.name, ext, size: Number(entry.size) || 0, id: entry.crc32 || '' };
    })
    .filter((entry) => wanted.includes(entry.ext))
    .map((entry) => ({
      ...splitFileEntry(entry.file, entry.ext),
      ext: entry.ext,
      size: entry.size,
      id: entry.id,
      sha1: '',
    }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

const EOCD_SIG = 0x06054b50;
const CD_SIG = 0x02014b50;

async function fetchRange(url, start, end) {
  const res = await fetch(url, {
    headers: {
      'User-Agent': 'roms-finder-indexer/1.0',
      Range: `bytes=${start}-${end}`,
    },
  });
  if (!res.ok && res.status !== 206) {
    throw new Error(`HTTP ${res.status} for range ${start}-${end}`);
  }
  return Buffer.from(await res.arrayBuffer());
}

async function readRemoteZipEntries(url) {
  const head = await fetch(url, { method: 'HEAD' });
  const size = Number(head.headers.get('content-length'));
  if (!size) throw new Error(`No Content-Length for ${url}`);

  const tailLength = Math.min(size, 65557);
  const tail = await fetchRange(url, size - tailLength, size - 1);

  let eocd = -1;
  for (let i = tail.length - 22; i >= 0; i--) {
    if (tail.readUInt32LE(i) === EOCD_SIG) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) throw new Error('ZIP central directory not found');

  const count = tail.readUInt16LE(eocd + 10);
  const cdSize = tail.readUInt32LE(eocd + 12);
  const cdOffset = tail.readUInt32LE(eocd + 16);
  if (count === 0xffff || cdOffset === 0xffffffff) {
    throw new Error('ZIP64 is not supported');
  }

  const cd = await fetchRange(url, cdOffset, cdOffset + cdSize - 1);
  const entries = [];
  let pos = 0;
  for (let i = 0; i < count && pos + 46 <= cd.length; i++) {
    if (cd.readUInt32LE(pos) !== CD_SIG) break;
    const nameLength = cd.readUInt16LE(pos + 28);
    const extraLength = cd.readUInt16LE(pos + 30);
    const commentLength = cd.readUInt16LE(pos + 32);
    entries.push({
      name: cd.toString('utf8', pos + 46, pos + 46 + nameLength),
      crc32: cd.readUInt32LE(pos + 16).toString(16).padStart(8, '0'),
      compressedSize: cd.readUInt32LE(pos + 20),
      size: cd.readUInt32LE(pos + 24),
    });
    pos += 46 + nameLength + extraLength + commentLength;
  }
  return entries;
}

async function loadCoverList(repo, cacheName, symlinkCacheName) {
  const headers = process.env.GITHUB_TOKEN
    ? { Authorization: `Bearer ${process.env.GITHUB_TOKEN}` }
    : {};
  const tree = await cached(cacheName, () =>
    fetchJson(
      `https://api.github.com/repos/${repo}/git/trees/master?recursive=1`,
      headers,
    ),
  );
  const boxarts = tree.tree.filter(
    (t) => typeof t.path === 'string' && t.path.startsWith('Named_Boxarts/'),
  );
  const names = boxarts.map((t) =>
    t.path.slice('Named_Boxarts/'.length).replace(/\.png$/i, ''),
  );
  const nameSet = new Set(names);

  const linkNames = boxarts
    .filter((t) => t.mode === '120000')
    .map((t) => t.path.slice('Named_Boxarts/'.length).replace(/\.png$/i, ''));

  const links = {};
  if (linkNames.length > 0) {
    const map = await cached(symlinkCacheName, async () => {
      const result = {};
      let cursor = 0;
      const worker = async () => {
        while (cursor < linkNames.length) {
          const name = linkNames[cursor++];
          const url =
            `https://raw.githubusercontent.com/${repo}/master/Named_Boxarts/` +
            encodeURIComponent(name + '.png');
          try {
            const res = await fetch(url);
            const text = (await res.text()).trim();
            if (text && text.toLowerCase().endsWith('.png')) {
              result[name] = text.replace(/\.png$/i, '');
            }
          } catch {}
        }
      };
      await Promise.all(Array.from({ length: 24 }, worker));
      return result;
    });
    Object.assign(links, map);
  }

  const resolve = new Map();
  for (const name of names) {
    let current = name;
    const seen = new Set();
    while (links[current] && !seen.has(current)) {
      seen.add(current);
      current = links[current];
    }
    if (current !== name) resolve.set(name, nameSet.has(current) ? current : null);
  }

  return { names, resolve };
}

function loadRomList(system) {
  return system.source.type === 'remote-zip'
    ? loadRemoteZipRomList(system)
    : loadMetadataRomList(system);
}
export { loadRomList, loadCoverList };
