import { createWriteStream } from "node:fs";
import { mkdir, rename, rm } from "node:fs/promises";
import path from "node:path";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";

import { extractZip, safeFileName } from "./zip";
import { extract7z } from "./sevenzip";
import type { ArchiveKind } from "../queue-types";

export type { ArchiveKind } from "../queue-types";

export const ROMS_DIR = process.env.ROMS_DIR
  ? path.resolve(process.env.ROMS_DIR)
  : path.join(process.cwd(), "roms");

export function systemDir(systemId?: string | null): string {
  const safe = (systemId ?? "").toLowerCase().replace(/[^a-z0-9_-]/g, "");
  return path.join(/*turbopackIgnore: true*/ ROMS_DIR, safe || "misc");
}

export const ARCHIVE_OUT_EXT = [
  "nds", "sfc", "smc", "fig", "swc", "gb", "gbc", "gba", "nes",
  "z64", "n64", "v64",
];

const NON_ROM_FILE = /\.(txt|nfo|md|json|xml|png|jpe?g|bmp|gif|ini|cfg|sav|sgm|dat|db)$/i;

export function pickArchiveRom(files: string[], out?: string): string | undefined {
  if (files.length === 0) return undefined;
  const preferred = out ? [out, ...ARCHIVE_OUT_EXT] : ARCHIVE_OUT_EXT;
  for (const ext of preferred) {
    const hit = files.find((file) => file.toLowerCase().endsWith(`.${ext}`));
    if (hit) return hit;
  }
  return files.find((file) => !NON_ROM_FILE.test(file)) ?? files[0];
}

export type RomSource = {
  id: string;
  name: string;
  systemId?: string;
  url: string;
  arch?: ArchiveKind | null;
  ext?: string;
  out?: string;
};

export type RomProgress = {
  phase: "downloading" | "extracting";
  received: number;
  total: number;
  progress: number;
};

export class DownloadCanceledError extends Error {
  constructor() {
    super("Canceled");
    this.name = "DownloadCanceledError";
  }
}

export type FetchRomOptions = {
  signal?: AbortSignal;
  onProgress?: (progress: RomProgress) => void;
  isCanceled?: () => boolean;
};

export async function fetchRom(
  source: RomSource,
  options: FetchRomOptions = {},
): Promise<string> {
  const dir = systemDir(source.systemId);
  await mkdir(dir, { recursive: true });

  const arch = source.arch ?? null;
  const ext = source.ext || (arch === "7z" ? "7z" : arch === "zip" ? "zip" : "bin");
  const tmp = path.join(dir, `.tmp-${source.id}`);

  const report = (phase: "downloading" | "extracting", received: number, total: number) => {
    options.onProgress?.({
      phase,
      received,
      total,
      progress: total > 0 ? Math.min(1, received / total) : 0,
    });
  };

  report("downloading", 0, 0);
  const response = await fetch(source.url, { signal: options.signal });
  if (!response.ok || !response.body) {
    throw new Error(`Download failed (HTTP ${response.status})`);
  }

  const total = Number(response.headers.get("content-length") ?? 0);
  let received = 0;
  report("downloading", received, total);
  const stream = Readable.fromWeb(
    response.body as unknown as Parameters<typeof Readable.fromWeb>[0],
  );
  stream.on("data", (chunk: Buffer) => {
    received += chunk.length;
    report("downloading", received, total);
  });

  try {
    await pipeline(stream, createWriteStream(tmp));
  } catch (error) {
    await rm(tmp, { force: true });
    throw error;
  }

  if (options.isCanceled?.()) {
    await rm(tmp, { force: true });
    throw new DownloadCanceledError();
  }

  let saved: string | undefined;
  if (arch === "zip" || arch === "7z") {
    report("extracting", received, total || received);

    const outDir = arch === "7z" ? path.join(dir, `.extract-${source.id}`) : dir;
    let files: string[] = [];
    try {
      files = arch === "7z" ? await extract7z(tmp, outDir) : await extractZip(tmp, dir);
    } finally {
      await rm(tmp, { force: true });
    }

    const chosen = pickArchiveRom(files, source.out);
    if (arch === "7z") {
      if (chosen) {
        const target = path.join(/*turbopackIgnore: true*/ dir, safeFileName(path.basename(chosen)));
        await rm(target, { force: true });
        await rename(chosen, target);
        saved = target;
      }
      await rm(outDir, { recursive: true, force: true });
    } else {
      saved = chosen;
    }
    if (!saved) throw new Error("No game file found in the archive");
  } else {
    const target = path.join(/*turbopackIgnore: true*/ dir, safeFileName(`${source.name}.${ext}`));
    await rm(target, { force: true });
    await rename(tmp, target);
    saved = target;
  }

  const canceled = options.isCanceled?.();
  if (canceled) {
    await rm(saved, { force: true });
    throw new DownloadCanceledError();
  }

  return saved;
}
