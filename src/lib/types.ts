export type Rom = {
  n: string;
  t: string;
  r: string;
  a?: string[];
  l?: string;
  g?: string[];
  s: number;
  m: string;
  h?: string;
  d?: string;
  e?: string;
  package?: string;
  media?: string;
  folder?: { destination: string; count: number };
  thumbnail?: true;
  c: string | null;
  x?: 1;
};

export type Facet = { name: string; count: number };

export type ChecksumKind = "md5" | "crc32";

export type ArchiveKind = "zip" | "7z";

export type DatasetMeta = {
  id: string;
  label: string;
  short: string;
  ext: string;
  arch: ArchiveKind | null;
  out: string;
  checksum: ChecksumKind;
  source: string;
  download: string;
  coverSource: string;
  coverBase: string;
  generatedAt: string;
  total: number;
  withCover: number;
  regions: Facet[];
  tags: Facet[];
  languages: Facet[];
};

export type SystemInfo = {
  id: string;
  label: string;
  short: string;
  data: string;
  ext: string;
  arch: ArchiveKind | null;
  out: string;
  checksum: ChecksumKind;
  total: number;
  withCover: number;
  generatedAt: string;
};

export type Dataset = {
  meta: DatasetMeta;
  roms: Rom[];
};

export type RomCategory =
  | "Retail"
  | "Apps"
  | "Multimedia"
  | "Uploaded"
  | "Demo"
  | "Prototype"
  | "Beta"
  | "Kiosk"
  | "Unlicensed"
  | "Hack"
  | "Translation"
  | "Pirate"
  | "Utility"
  | "NDSi Enhanced";

export type SortKey = "title-asc" | "title-desc" | "size-asc" | "size-desc";

export type CoverFilter = "all" | "with" | "without";
