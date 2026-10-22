import { createReadStream, createWriteStream } from "node:fs";
import { mkdir, open } from "node:fs/promises";
import path from "node:path";
import { pipeline } from "node:stream/promises";
import { createInflateRaw } from "node:zlib";

const EOCD_SIG = 0x06054b50;
const CD_SIG = 0x02014b50;
const LFH_SIG = 0x04034b50;

const MAX_EOCD_SEARCH = 65557;

export type ZipEntry = {
  name: string;
  method: number;
  compressedSize: number;
  uncompressedSize: number;
  localHeaderOffset: number;
};

async function readCentralDirectory(zipPath: string): Promise<ZipEntry[]> {
  const handle = await open(zipPath, "r");
  try {
    const { size } = await handle.stat();
    if (size < 22) throw new Error("File is too small to be a ZIP archive");

    const tailLength = Math.min(size, MAX_EOCD_SEARCH);
    const tail = Buffer.alloc(tailLength);
    await handle.read(tail, 0, tailLength, size - tailLength);

    let eocd = -1;
    for (let i = tail.length - 22; i >= 0; i--) {
      if (tail.readUInt32LE(i) === EOCD_SIG) {
        eocd = i;
        break;
      }
    }
    if (eocd < 0) throw new Error("Invalid ZIP: central directory not found");

    const entryCount = tail.readUInt16LE(eocd + 10);
    const cdSize = tail.readUInt32LE(eocd + 12);
    const cdOffset = tail.readUInt32LE(eocd + 16);

    if (entryCount === 0xffff || cdOffset === 0xffffffff) {
      throw new Error("ZIP64 is not supported");
    }

    const directory = Buffer.alloc(cdSize);
    await handle.read(directory, 0, cdSize, cdOffset);

    const entries: ZipEntry[] = [];
    let position = 0;
    for (let i = 0; i < entryCount && position + 46 <= directory.length; i++) {
      if (directory.readUInt32LE(position) !== CD_SIG) break;
      const nameLength = directory.readUInt16LE(position + 28);
      const extraLength = directory.readUInt16LE(position + 30);
      const commentLength = directory.readUInt16LE(position + 32);
      entries.push({
        method: directory.readUInt16LE(position + 10),
        compressedSize: directory.readUInt32LE(position + 20),
        uncompressedSize: directory.readUInt32LE(position + 24),
        localHeaderOffset: directory.readUInt32LE(position + 42),
        name: directory.toString("utf8", position + 46, position + 46 + nameLength),
      });
      position += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
  } finally {
    await handle.close();
  }
}

export function safeFileName(name: string): string {
  return path
    .basename(name)
    .replace(/[<>:"/\\|?*\u0000-\u001f]/g, "_")
    .replace(/^\.+/, "")
    .trim();
}

async function extractEntry(zipPath: string, entry: ZipEntry, destPath: string): Promise<void> {
  if (entry.method !== 0 && entry.method !== 8) {
    throw new Error(`Unsupported ZIP compression (method ${entry.method})`);
  }

  const handle = await open(zipPath, "r");
  try {
    const header = Buffer.alloc(30);
    await handle.read(header, 0, 30, entry.localHeaderOffset);
    if (header.readUInt32LE(0) !== LFH_SIG) {
      throw new Error("Invalid local ZIP header");
    }
    const nameLength = header.readUInt16LE(26);
    const extraLength = header.readUInt16LE(28);
    const dataStart = entry.localHeaderOffset + 30 + nameLength + extraLength;

    if (entry.compressedSize === 0) {
      const empty = createWriteStream(destPath);
      await pipeline(async function* () {}, empty);
      return;
    }

    const source = createReadStream(zipPath, {
      start: dataStart,
      end: dataStart + entry.compressedSize - 1,
    });
    const target = createWriteStream(destPath);

    if (entry.method === 0) {
      await pipeline(source, target);
    } else {
      await pipeline(source, createInflateRaw(), target);
    }
  } finally {
    await handle.close();
  }
}

export async function extractZip(zipPath: string, destDir: string): Promise<string[]> {
  const entries = await readCentralDirectory(zipPath);
  await mkdir(destDir, { recursive: true });

  const written: string[] = [];
  for (const entry of entries) {
    if (!entry.name || entry.name.endsWith("/")) continue;
    const name = safeFileName(entry.name);
    if (!name) continue;
    const destPath = path.join(destDir, name);
    await extractEntry(zipPath, entry, destPath);
    written.push(destPath);
  }
  return written;
}
