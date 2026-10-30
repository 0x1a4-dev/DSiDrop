import { mkdir, readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import sharp from "sharp";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "public", "downloads", "apps");
const output = path.join(root, "covers");
const apps = [
  { file: "notpipe.nds", title: "notPipe DSi", label: "DSi HOMEBREW", background: "#d8f1e7", foreground: "#174b3b", accent: "#34a77e", footer: "notPipe" },
  { file: "DSOrganize.nds", title: "DSOrganize", label: "NINTENDO DS", background: "#e4edf9", foreground: "#203f70", accent: "#e1ab35", footer: "VERSION 3.2" },
  { file: "pkmn-chest.nds", title: "pkmn-chest", label: "UNIVERSAL-TEAM", background: "#f8eab9", foreground: "#35445e", accent: "#e36b5f", footer: "NINTENDO DS" },
];

function bannerIcon(rom) {
  const banner = rom.readUInt32LE(0x68);
  if (banner < 512 || banner + 0x240 > rom.length) throw new Error("NDS banner is missing or truncated");
  const pixels = Buffer.alloc(32 * 32 * 4);
  for (let row = 0; row < 32; row++) {
    for (let column = 0; column < 32; column++) {
      const tile = (Math.floor(row / 8) * 4 + Math.floor(column / 8)) * 32;
      const packed = rom[banner + 0x20 + tile + (row % 8) * 4 + Math.floor((column % 8) / 2)];
      const index = (packed >> ((column % 2) * 4)) & 15;
      const color = rom.readUInt16LE(banner + 0x220 + index * 2);
      const offset = (row * 32 + column) * 4;
      pixels[offset] = Math.round((color & 31) * 255 / 31);
      pixels[offset + 1] = Math.round(((color >> 5) & 31) * 255 / 31);
      pixels[offset + 2] = Math.round(((color >> 10) & 31) * 255 / 31);
      pixels[offset + 3] = index ? 255 : 0;
    }
  }
  return sharp(pixels, { raw: { width: 32, height: 32, channels: 4 } })
    .resize(224, 224, { kernel: "nearest" }).png().toBuffer();
}

async function textLayer(text, fontSize, color, top) {
  const input = await sharp({ text: { text: `<span foreground="${color}">${text}</span>`,
    font: `DejaVu Sans Bold ${fontSize}`, rgba: true } }).png().toBuffer();
  const { width } = await sharp(input).metadata();
  if (width > 408) throw new Error(`Cover title is too wide: ${text}`);
  return { input, top, left: Math.floor((448 - width) / 2) };
}

await mkdir(output, { recursive: true });
for (const app of apps) {
  const rom = await readFile(path.join(root, app.file));
  const stripe = await sharp({ create: { width: 448, height: 12, channels: 4, background: app.accent } }).png().toBuffer();
  const target = path.join(output, `${app.file.slice(0, -4).toLowerCase()}.png`);
  await sharp({ create: { width: 448, height: 448, channels: 4, background: app.background } }).composite([
    { input: stripe, top: 0, left: 0 },
    await textLayer(app.label, 17, app.foreground, 37),
    { input: await bannerIcon(rom), top: 93, left: 112 },
    await textLayer(app.title, 40, app.foreground, 350),
    await textLayer(app.footer, 16, app.foreground, 409),
  ]).png().toFile(target);
  console.log(`Created ${path.relative(root, target)} from ${app.file}`);
}