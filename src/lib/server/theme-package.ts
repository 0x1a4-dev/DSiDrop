import { execFile } from "node:child_process";
import { createWriteStream } from "node:fs";
import { lstat, mkdir, open, readFile, rename, rm } from "node:fs/promises";
import path from "node:path";
import { Readable, Transform } from "node:stream";
import { pipeline } from "node:stream/promises";
import { promisify } from "node:util";
import { path7za } from "7zip-bin";
import type { MenuTheme } from "../theme-types";
import { ROMS_DIR } from "./rom-file";
import { themePath } from "./themes";

const execute = promisify(execFile);
const LIMIT = 128 * 1024 * 1024;
const FILE_LIMIT = 32 * 1024 * 1024;
const active = new Map<string, Promise<string>>();
const crcTable = Array.from({ length: 256 }, (_, index) => {
  let value = index;
  for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  return value >>> 0;
});

export function themeCrc(bytes: Buffer, previous = 0): number {
  let value = previous ^ 0xffffffff;
  for (const byte of bytes) value = (value >>> 8) ^ crcTable[(value ^ byte) & 255];
  return (value ^ 0xffffffff) >>> 0;
}

export function themeArchiveEntries(listing: string, input: string): { name: string; relative: string; size: number }[] {
  const files: { name: string; relative: string; size: number }[] = [];
  const names = new Set<string>();
  let total = 0;
  for (const block of listing.trim().split(/\r?\n\r?\n/)) {
    const record = Object.fromEntries(block.split(/\r?\n/).flatMap((line) => {
      const separator = line.indexOf(" = ");
      return separator < 0 ? [] : [[line.slice(0, separator), line.slice(separator + 3)]];
    }));
    if (!record.Path) continue;
    const name = themePath(record.Path.replaceAll("\\", "/").replace(/\/$/, ""));
    if (record["Symbolic Link"] || record["Hard Link"] || /l/.test(record.Attributes || "") || record.Encrypted === "+") throw new Error("Theme links or encrypted files are not allowed");
    if (record.Folder === "+" || /^D/.test(record.Attributes || "")) continue;
    const size = Number(record.Size);
    if (!Number.isSafeInteger(size) || size < 0 || size > FILE_LIMIT || (total += size) > LIMIT) throw new Error("Theme archive is too large");
    if (input && !name.startsWith(`${input}/`)) continue;
    const relative = themePath(input ? name.slice(input.length + 1) : name);
    if (/\.(nds|app|exe|dll|bat|cmd|ps1|com)$/i.test(relative)) throw new Error("Executable files are not theme assets");
    if (names.has(relative.toLowerCase())) throw new Error("Duplicate theme asset");
    names.add(relative.toLowerCase());
    files.push({ name, relative, size });
    if (files.length > 2048) throw new Error("Too many theme files");
  }
  if (!files.length) throw new Error("Theme archive contains no matching assets");
  return files;
}

export function prepareTheme(theme: MenuTheme): Promise<string> {
  const existing = active.get(theme.id);
  if (existing) return existing;
  const task = buildTheme(theme).finally(() => active.delete(theme.id));
  active.set(theme.id, task);
  return task;
}

async function buildTheme(theme: MenuTheme): Promise<string> {
  const directory = path.join(ROMS_DIR, ".themes", "packages", theme.id);
  const result = path.join(directory, "theme.bin");
  await mkdir(directory, { recursive: true });
  const archive = path.join(directory, "source.7z");
  const extracted = path.join(directory, "extracted");
  const temporary = `${result}.part`;
  try {
    const response = await fetch(theme.archive, { redirect: "error", signal: AbortSignal.timeout(120_000) });
    if (!response.ok || !response.body) throw new Error(`Theme archive: HTTP ${response.status}`);
    let received = 0;
    await pipeline(Readable.fromWeb(response.body as Parameters<typeof Readable.fromWeb>[0]), new Transform({
      transform(chunk: Buffer, _encoding, callback) {
        received += chunk.length;
        callback(received > LIMIT ? new Error("Theme archive is too large") : null, chunk);
      },
    }), createWriteStream(archive));
    const { stdout } = await execute(path7za, ["l", "-slt", "-ba", "-sccUTF-8", archive], { windowsHide: true, timeout: 30_000, maxBuffer: 4 * 1024 * 1024 });
    const files = themeArchiveEntries(stdout, theme.input);
    await rm(extracted, { recursive: true, force: true });
    await execute(path7za, ["x", archive, `-o${extracted}`, "-y", "-bso0", "-bsp0"], { windowsHide: true, timeout: 120_000, maxBuffer: 1024 * 1024 });
    const root = Buffer.from(theme.destination, "utf8");
    const header = Buffer.alloc(12);
    header.write("RFTHEME1");
    header.writeUInt16LE(root.length, 8);
    header.writeUInt16LE(files.length, 10);
    const output = await open(temporary, "w");
    try {
      await output.write(header);
      await output.write(root);
      for (const entry of files) {
        const file = path.join(extracted, ...entry.name.split("/"));
        const info = await lstat(file);
        if (!info.isFile() || info.isSymbolicLink() || info.size !== entry.size) throw new Error("Theme asset changed during extraction");
        const bytes = await readFile(file);
        const name = Buffer.from(entry.relative, "utf8");
        const item = Buffer.alloc(10);
        item.writeUInt16LE(name.length);
        item.writeUInt32LE(bytes.length, 2);
        item.writeUInt32LE(themeCrc(bytes), 6);
        await output.write(item);
        await output.write(name);
        await output.write(bytes);
      }
      await output.sync();
    } finally { await output.close(); }
    await rename(temporary, result);
    return result;
  } finally {
    await rm(archive, { force: true });
    await rm(temporary, { force: true });
    await rm(extracted, { recursive: true, force: true });
  }
}