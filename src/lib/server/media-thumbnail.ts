import sharp from "sharp";
import { createReadStream, createWriteStream } from "node:fs";
import { mkdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { pipeline } from "node:stream/promises";
import { mediaInfo } from "../media";
import { ROMS_DIR } from "./rom-file";
import { getUploadedFile } from "./uploads";

const directory = path.join(ROMS_DIR, "others", ".thumbnails");
const active = new Map<string, Promise<Buffer | null>>();
const unavailable = new Map<string, number>();
let tail: Promise<unknown> = Promise.resolve();

export async function mediaThumbnail(id: string): Promise<Buffer | null> {
  const entry = await getUploadedFile(id);
  const files = entry.folder?.category === "Multimedia" ? entry.folder.files.filter((file) => file.size > 0) : [];
  const preview = files.find((file) => mediaInfo(file.name)?.kind === "Image");
  const media = mediaInfo(preview?.name ?? entry.name);
  if (media?.kind !== "Image") return null;
  if ((unavailable.get(id) ?? 0) > Date.now()) return null;
  const cache = path.join(directory, `${id}.jpg`);
  try { return await readFile(cache); }
  catch (error) { if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error; }
  const pending = active.get(id);
  if (pending) return pending;
  const task = tail.then(async () => {
    await mkdir(directory, { recursive: true });
    const temporary = path.join(directory, `.${randomUUID()}.${media.extension}`);
    const output = `${temporary}.jpg`;
    try {
      let source = entry.file;
      if (preview) {
        if (!Number.isSafeInteger(preview.offset) || preview.offset < 12 || preview.offset + preview.size > entry.size) return null;
        await pipeline(createReadStream(entry.file, { start: preview.offset, end: preview.offset + preview.size - 1 }), createWriteStream(temporary, { flags: "wx" }));
        source = temporary;
      }
      const thumbnail = await sharp(source, { limitInputPixels: 40_000_000, animated: false }).rotate().resize(384, 384, { fit: "inside", withoutEnlargement: true }).jpeg({ quality: 82 }).toBuffer();
      await writeFile(output, thumbnail, { flag: "wx" });
      await rename(output, cache);
      try { await getUploadedFile(id); }
      catch { await rm(cache, { force: true }); return null; }
      return thumbnail;
    } catch { return null; }
    finally { await Promise.all([temporary, output].map((file) => rm(file, { force: true }))); }
  });
  active.set(id, task); tail = task.catch(() => undefined);
  try {
    const result = await task;
    if (!result) {
      if (unavailable.size >= 128) unavailable.delete(unavailable.keys().next().value!);
      unavailable.set(id, Date.now() + 60_000);
    }
    return result;
  } finally { active.delete(id); }
}