import sharp from "sharp";
import { readFile, stat } from "node:fs/promises";
import path from "node:path";
import { coverUrl } from "../roms";
import { DsiError, dsiSystem, dsiText, lookup, validateId } from "./dsi";
import { findTheme } from "./themes";
import { mediaThumbnail } from "./media-thumbnail";

const MAX_IMAGE_BYTES = 2 * 1024 * 1024;
const CACHE_LIMIT = 128;
type Entry = { expires: number; result: Promise<Buffer | null> };
const shared = globalThis as typeof globalThis & { __dsiCovers?: Map<string, Entry> };
const cache = (shared.__dsiCovers ??= new Map<string, Entry>());

async function fetchCover(url: string, size: number): Promise<Buffer | null> {
  try {
    let image: Buffer;
    const local = /^\/downloads\/apps\/covers\/([a-z0-9-]+\.png)$/.exec(url);
    const thumbnail = /^\/api\/library\/thumbnail\?id=([a-f0-9]{32})$/.exec(url);
    if (thumbnail) {
      const preview = await mediaThumbnail(thumbnail[1]);
      if (!preview) return null;
      image = preview;
    } else if (local) {
      const file = path.join(process.env.APPS_DIR || path.join(process.cwd(), "public", "downloads", "apps"), "covers", local[1]);
      if ((await stat(file)).size > MAX_IMAGE_BYTES) return null;
      image = await readFile(file);
      if (image.length > MAX_IMAGE_BYTES) return null;
    } else {
      const response = await fetch(url, { signal: AbortSignal.timeout(4000), cache: "no-store" });
      if (!response.ok || !response.body || Number(response.headers.get("Content-Length")) > MAX_IMAGE_BYTES) {
        await response.body?.cancel();
        return null;
      }
      const reader = response.body.getReader();
      const chunks: Uint8Array[] = [];
      let length = 0;
      try {
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          length += value.length;
          if (length > MAX_IMAGE_BYTES) { await reader.cancel(); return null; }
          chunks.push(value);
        }
      } finally { reader.releaseLock(); }
      image = Buffer.concat(chunks);
    }
    const rgba = await sharp(image, { limitInputPixels: 16_000_000, animated: false })
      .rotate().resize(size, size, { fit: "contain", background: { r: 0, g: 0, b: 0, alpha: 0 } })
      .toColourspace("srgb").ensureAlpha().raw().toBuffer();
    const headerSize = size === 64 ? 8 : 12;
    const output = Buffer.alloc(headerSize + size * size * 2);
    output.write(size === 64 ? "RFCOVER1" : "RFCOVER2", "ascii");
    if (headerSize === 12) { output.writeUInt16LE(size, 8); output.writeUInt16LE(size, 10); }
    for (let pixel = 0; pixel < size * size; pixel++) {
      const p = pixel * 4;
      const rgb555 = rgba[p + 3] < 128 ? 0 :
        0x8000 | (rgba[p] >> 3) | ((rgba[p + 1] >> 3) << 5) | ((rgba[p + 2] >> 3) << 10);
      output.writeUInt16LE(rgb555, headerSize + pixel * 2);
    }
    return output;
  } catch { return null; }
}

export async function dsiCover(params: URLSearchParams): Promise<Response> {
  const system = dsiSystem(params.get("system"));
  const id = params.get("id");
  validateId(id, system);
  const sizeParam = params.get("size");
  if (sizeParam != null && sizeParam !== "64" && sizeParam !== "88" && sizeParam !== "112") throw new DsiError("Unsupported cover size");
  const size = sizeParam === "112" ? 112 : sizeParam === "88" ? 88 : 64;
  const { rom, data } = await lookup(id, system);
  const url = rom.g?.includes("Themes") ? (await findTheme(id))?.screenshots[0]?.url : coverUrl(data.meta, rom);
  if (!url) return dsiText("RFERROR1\tNo cover available\n", 404);

  const key = `${system}:${id}:${size}:${url}`;
  let entry = cache.get(key);
  if (!entry || entry.expires <= Date.now()) {
    if (cache.size >= CACHE_LIMIT) cache.delete(cache.keys().next().value!);
    entry = { expires: Date.now() + 60_000, result: fetchCover(url, size) };
    cache.set(key, entry);
    const current = entry;
    void current.result.then((result) => { current.expires = Date.now() + (result ? 86_400_000 : 60_000); });
  } else { cache.delete(key); cache.set(key, entry); }
  const output = await entry.result;
  if (!output) return dsiText("RFERROR1\tCover unavailable\n", 404);
  if (output.length !== (size === 64 ? 8 : 12) + size * size * 2) throw new DsiError("Invalid cover output", 500);
  return new Response(new Uint8Array(output), { headers: {
    "Content-Type": "application/octet-stream",
    "Content-Length": String(output.length),
    "Cache-Control": "no-store, no-transform",
    "X-Content-Type-Options": "nosniff",
  } });
}
