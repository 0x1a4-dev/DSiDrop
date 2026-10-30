import sharp from "sharp";
import { findTheme } from "@/lib/server/themes";

export async function GET(request: Request) {
  try {
    const params = new URL(request.url).searchParams;
    const theme = await findTheme(params.get("id") || "");
    const index = Number(params.get("shot") || 0);
    const shot = Number.isSafeInteger(index) && index >= 0 ? theme?.screenshots[index] : undefined;
    if (!shot) return new Response("No preview available", { status: 404 });
    const response = await fetch(shot.url, { redirect: "error", signal: AbortSignal.timeout(15_000) });
    if (!response.ok || !response.body) return new Response("Preview unavailable", { status: 502 });
    const chunks: Uint8Array[] = [];
    let length = 0;
    const reader = response.body.getReader();
    try {
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        length += value.length;
        if (length > 4 * 1024 * 1024) { await reader.cancel(); throw new Error("Preview too large"); }
        chunks.push(value);
      }
    } finally { reader.releaseLock(); }
    const image = sharp(Buffer.concat(chunks), { limitInputPixels: 16_000_000 }).rotate();
    const headers = { "Cache-Control": "public, max-age=3600", "X-Content-Type-Options": "nosniff" };
    if (params.get("native") === "1") {
      const rgba = await image.resize(256, 384, { fit: "contain", background: "#000000" }).ensureAlpha().raw().toBuffer();
      const output = Buffer.alloc(8 + 256 * 384 * 2);
      output.write("RFTMPRE1");
      for (let pixel = 0; pixel < 256 * 384; pixel++) {
        const offset = pixel * 4;
        output.writeUInt16LE(0x8000 | (rgba[offset] >> 3) | ((rgba[offset + 1] >> 3) << 5) | ((rgba[offset + 2] >> 3) << 10), 8 + pixel * 2);
      }
      return new Response(output, { headers: { ...headers, "Content-Type": "application/octet-stream", "Content-Length": String(output.length) } });
    }
    const png = await image.png().toBuffer();
    return new Response(new Uint8Array(png), { headers: { ...headers, "Content-Type": "image/png" } });
  } catch { return new Response("Preview unavailable", { status: 502 }); }
}