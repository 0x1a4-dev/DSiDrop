import type { DatasetMeta, Rom, RomCategory, SortKey } from "./types";

export const PAGE_SIZE = 48;

export function romFileName(meta: DatasetMeta, rom: Rom): string {
  const ext = rom.e ?? meta.ext;
  const prefix = rom.d ? `${rom.d}/` : "";
  return `${prefix}${rom.n}.${ext}`;
}

export function romExt(meta: DatasetMeta, rom: Rom): string {
  return meta.arch ? meta.out : rom.e ?? meta.ext;
}

export function downloadUrl(meta: DatasetMeta, rom: Rom): string {
  if (meta.id === "others") return `/api/library/file?id=${encodeURIComponent(rom.m)}`;
  const segments = romFileName(meta, rom).split("/").map(encodeURIComponent);
  return `${meta.download}/${segments.join("/")}`;
}

export function checksumLabel(meta: DatasetMeta): string {
  return meta.checksum === "crc32" ? "CRC32" : "MD5";
}

export function coverUrl(meta: DatasetMeta, rom: Rom): string | null {
  if (!rom.c) return null;
  return `${meta.coverBase}${encodeURIComponent(`${rom.c}.png`)}`;
}

export function romCategory(rom: Rom): RomCategory {
  const tags = rom.g ?? [];
  if (tags.includes("Unl") || tags.includes("Aftermarket")) return "Unlicensed";
  if (tags.includes("Hack")) return "Hack";
  if (tags.includes("Translation")) return "Translation";
  if (tags.includes("Pirate")) return "Pirate";
  if (tags.includes("Demo")) return "Demo";
  if (tags.includes("Proto")) return "Prototype";
  if (tags.includes("Beta")) return "Beta";
  if (tags.includes("Kiosk")) return "Kiosk";
  if (
    tags.includes("Save Data") ||
    tags.includes("Program") ||
    tags.includes("BIOS")
  ) {
    return "Utility";
  }
  if (tags.includes("NDSi")) return "NDSi Enhanced";
  return "Retail";
}

export const CATEGORY_ORDER: RomCategory[] = [
  "Retail",
  "Demo",
  "Prototype",
  "Beta",
  "Kiosk",
  "Unlicensed",
  "Hack",
  "Translation",
  "Pirate",
  "NDSi Enhanced",
  "Utility",
];

export function categoryLabel(category: string): string {
  const labels: Record<string, string> = {
    "NDSi Enhanced": "DSi enhanced",
  };
  return labels[category] ?? category;
}

export function languageLabel(language: string): string {
  const labels: Record<string, string> = {
    En: "English", Fr: "French", De: "German", Es: "Spanish", It: "Italian",
    Ja: "Japanese", Ko: "Korean", Zh: "Chinese", Nl: "Dutch", Pt: "Portuguese",
    Ru: "Russian", Sv: "Swedish", Da: "Danish", No: "Norwegian", Fi: "Finnish",
    Pl: "Polish", Cs: "Czech", Hu: "Hungarian", El: "Greek", Tr: "Turkish",
    Ar: "Arabic", Ca: "Catalan", Hr: "Croatian", Ro: "Romanian", Sk: "Slovak",
    Sl: "Slovenian", Th: "Thai", He: "Hebrew", Vi: "Vietnamese", Id: "Indonesian",
    Ms: "Malay", Uk: "Ukrainian", Bg: "Bulgarian", Is: "Icelandic", Ga: "Irish",
    La: "Latin", Lt: "Lithuanian", Lv: "Latvian", Et: "Estonian", Hi: "Hindi",
  };
  return labels[language.trim()] ?? language.trim();
}

export function formatBytes(bytes: number): string {
  if (!bytes) return "—";
  const units = ["B", "KB", "MB", "GB"];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value.toFixed(value >= 100 || unit === 0 ? 0 : 1)} ${units[unit]}`;
}

export function formatLanguages(value?: string): string {
  if (!value) return "";
  return value.split(",").map(languageLabel).join(" · ");
}

export function regionFlag(region: string): string {
  const map: Record<string, string> = {
    Europe: "🇪🇺",
    USA: "🇺🇸",
    Japan: "🇯🇵",
    Germany: "🇩🇪",
    France: "🇫🇷",
    Italy: "🇮🇹",
    Spain: "🇪🇸",
    Netherlands: "🇳🇱",
    "United Kingdom": "🇬🇧",
    Australia: "🇦🇺",
    Canada: "🇨🇦",
    Korea: "🇰🇷",
    World: "🌍",
    China: "🇨🇳",
    Taiwan: "🇹🇼",
    Denmark: "🇩🇰",
    Norway: "🇳🇴",
    Sweden: "🇸🇪",
    Finland: "🇫🇮",
    Russia: "🇷🇺",
    Brazil: "🇧🇷",
    Poland: "🇵🇱",
  };
  return map[region] ?? "🌐";
}

export function sortRoms(roms: Rom[], sort: SortKey): Rom[] {
  const copy = [...roms];
  switch (sort) {
    case "title-desc":
      return copy.sort((a, b) => b.t.localeCompare(a.t));
    case "size-asc":
      return copy.sort((a, b) => a.s - b.s);
    case "size-desc":
      return copy.sort((a, b) => b.s - a.s);
    case "title-asc":
    default:
      return copy.sort((a, b) => a.t.localeCompare(b.t));
  }
}

export function searchIndex(rom: Rom): string {
  return `${rom.t} ${rom.n} ${rom.r} ${rom.l ?? ""}`.toLowerCase();
}

export function normalizeQuery(value: string): string {
  return value
    .toLowerCase()
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .trim();
}
