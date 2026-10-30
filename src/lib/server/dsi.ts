import { createReadStream, createWriteStream } from "node:fs";
import { createHash } from "node:crypto";
import { mkdir, open, readFile, rename, rm, stat } from "node:fs/promises";
import path from "node:path";
import { Readable, Transform } from "node:stream";
import { pipeline } from "node:stream/promises";
import { unstable_rethrow } from "next/navigation";
import type { Dataset, Rom } from "../types";
import { downloadUrl, normalizeQuery } from "../roms";
import { mediaInfo } from "../media";
import { ROMS_DIR, systemDir } from "./rom-file";
import { extractZip, safeFileName } from "./zip";
import { getUploadedFile, uploadedDataset, UploadError, validateHeader } from "./uploads";
import { findTheme, themeCatalog, themeFavorites } from "./themes";
import { prepareTheme } from "./theme-package";
import { THEME_STYLES, type MenuTheme } from "../theme-types";

export const DSI_PAGE_SIZE = 10;
const CACHE_DIR = path.join(ROMS_DIR, ".dsi-cache");
const MAX_ROM_SIZE = 512 * 1024 * 1024;
type State = "checking" | "downloading" | "extracting" | "ready" | "error";
export type DsiSystem = "nds" | "gba" | "others";
export type DsiJob = {
  id: string;
  system: DsiSystem;
  state: State;
  received: number;
  total: number;
  size: number;
  crc: string;
  name: string;
  message: string;
  file?: string;
};
type DsiStore = {
  jobs: Map<string, DsiJob>;
  active?: string;
  datasets?: Partial<Record<DsiSystem, { mtime: number; data: Dataset }>>;
};
const globalStore = globalThis as typeof globalThis & { __dsiStore?: DsiStore };
const store: DsiStore = (globalStore.__dsiStore ??= { jobs: new Map() });

export class DsiError extends Error {
  constructor(message: string, public status = 400) { super(message); }
}

export function ascii(value: string, limit = 120): string {
  return value.replace(/ß/g, "ss").normalize("NFKD")
    .replace(/[\u0300-\u036f]/g, "").replace(/[^\x20-\x7e]/g, " ")
    .replace(/\s+/g, " ").trim().slice(0, limit);
}

export function dsiText(body: string, status = 200): Response {
  return new Response(body, { status, headers: {
    "Content-Type": "text/plain; charset=us-ascii",
    "Content-Length": String(Buffer.byteLength(body)),
    "Cache-Control": "no-store, no-transform",
    "X-Content-Type-Options": "nosniff",
  } });
}

export function dsiError(error: unknown): Response {
  unstable_rethrow(error);
  console.error("DSi:", error);
  return dsiText(`RFERROR1\t${ascii(error instanceof Error ? error.message : "Server error")}\n`,
    error instanceof DsiError ? error.status : 500);
}

export function dsiSystem(value?: string | null): DsiSystem {
  if (value == null || value === "nds") return "nds";
  if (value === "gba") return "gba";
  if (value === "others") return "others";
  throw new DsiError("Unsupported system. Choose NDS, GBA or Others.");
}

export async function ndsDataset(system: DsiSystem = "nds"): Promise<Dataset> {
  if (system === "others") return uploadedDataset();
  const file = path.join(process.cwd(), "public", "data", `${system}.json`);
  let info;
  try { info = await stat(file); }
  catch { throw new DsiError(`${system.toUpperCase()} catalog missing. Run npm run index on your PC.`, 503); }
  const datasets = (store.datasets ??= {});
  if (datasets[system]?.mtime === info.mtimeMs) return datasets[system].data;
  const data = JSON.parse(await readFile(file, "utf8")) as Dataset;
  data.roms.sort((a, b) => a.t.localeCompare(b.t, "en") || a.n.localeCompare(b.n, "en"));
  datasets[system] = { mtime: info.mtimeMs, data };
  return data;
}

export async function catalog(params: URLSearchParams): Promise<string> {
  const system = dsiSystem(params.get("system"));
  if (system === "others" && params.get("type") === "Themes") {
    const favorites = new Set(await themeFavorites());
    const query = normalizeQuery(params.get("q") || "");
    const themes = (await themeCatalog()).filter((theme) => (!params.get("style") || theme.style === params.get("style"))
      && (params.get("favorites") !== "1" || favorites.has(theme.id))
      && query.split(/\s+/).every((word) => normalizeQuery(`${theme.title} ${theme.author}`).includes(word)));
    const pages = Math.max(1, Math.ceil(themes.length / DSI_PAGE_SIZE));
    const page = Math.max(0, Math.min(pages - 1, Number(params.get("page")) || 0));
    return [`RFCATALOG1\t${themes.length}\t${page}\t${pages}`, ...themes.slice(page * DSI_PAGE_SIZE, (page + 1) * DSI_PAGE_SIZE)
      .map((theme) => [theme.id, ascii(theme.title, 90), ascii(THEME_STYLES[theme.style], 24), 0, favorites.has(theme.id) ? "Themes*" : "Themes"].join("\t")), ""].join("\n");
  }
  const { roms } = await ndsDataset(system);
  const query = normalizeQuery(params.get("q")?.slice(0, 80) ?? "");
  const region = (params.get("region") ?? "").toLowerCase();
  const type = system === "others" ? params.get("type") ?? "" : "";
  const filtered = roms.filter((rom) => {
    if (type && !rom.g?.includes(type)) return false;
    const regions = [rom.r, ...(rom.a ?? [])].map((r) => r.toLowerCase());
    if (region && !regions.includes(region)) return false;
    const index = normalizeQuery(`${rom.t} ${rom.n} ${rom.r} ${rom.l ?? ""}`);
    return query.split(/\s+/).every((word) => index.includes(word));
  });
  const pages = Math.max(1, Math.ceil(filtered.length / DSI_PAGE_SIZE));
  const requested = Number(params.get("page") ?? 0);
  const page = Number.isSafeInteger(requested) ? Math.max(0, Math.min(pages - 1, requested)) : 0;
  const rows = filtered.slice(page * DSI_PAGE_SIZE, (page + 1) * DSI_PAGE_SIZE).map((rom) =>
    [rom.m, ascii(rom.t, 90) || `NDS upload ${rom.m.slice(0, 8)}`, ascii(rom.r, 24), rom.s, ascii(rom.l || "-", 40)].join("\t"));
  return [`RFCATALOG1\t${filtered.length}\t${page}\t${pages}`, ...rows, ""].join("\n");
}

export function validateId(id: string | null, system: DsiSystem): asserts id is string {
  if (!id || !(system === "gba" ? /^[a-f0-9]{8}$/ : /^[a-f0-9]{32}$/).test(id))
    throw new DsiError(`Invalid ${system.toUpperCase()} ID`);
}

export async function lookup(id: string, system: DsiSystem): Promise<{ rom: Rom; data: Dataset }> {
  const data = await ndsDataset(system);
  const rom = data.roms.find((r) => r.m === id);
  if (!rom && system === "others") {
    const theme = await findTheme(id);
    if (theme) return { rom: themeRom(theme), data };
  }
  if (!rom) throw new DsiError(`Game not found in the ${system.toUpperCase()} catalog`, 404);
  return { rom, data };
}

export function themeRom(theme: MenuTheme): Rom {
  return { m: theme.id, n: theme.title, t: theme.title, r: THEME_STYLES[theme.style], l: "Themes", g: ["Themes"], s: 0, c: null };
}

export function statusLine(job: DsiJob): string {
  return ["RFSTATUS1", job.state, job.received, job.total, job.size,
    job.crc || "-", job.name || "-", ascii(job.message) || "-"].join("\t") + "\n";
}

const crcTable = Array.from({ length: 256 }, (_, index) => {
  let c = index;
  for (let i = 0; i < 8; i++) c = (c >>> 1) ^ ((c & 1) ? 0xedb88320 : 0);
  return c >>> 0;
});

async function inspectRom(file: string, minimumSize = 512): Promise<{ size: number; crc: string }> {
  const { size } = await stat(file);
  if (size < minimumSize || size > MAX_ROM_SIZE) throw new DsiError("Invalid file size", 502);
  let crc = 0xffffffff;
  for await (const chunk of createReadStream(file)) {
    for (const byte of chunk as Buffer) crc = (crc >>> 8) ^ crcTable[(crc ^ byte) & 255];
  }
  return { size, crc: ((crc ^ 0xffffffff) >>> 0).toString(16).padStart(8, "0") };
}

async function prepareJob(job: DsiJob): Promise<void> {
  const { rom, data } = await lookup(job.id, job.system);
  if (job.system === "others") {
    if (rom.g?.includes("Themes")) {
      const theme = await findTheme(job.id);
      if (!theme) throw new DsiError("Theme not found", 404);
      job.state = "extracting";
      job.message = "PC is preparing theme assets";
      const file = await prepareTheme(theme);
      const info = await inspectRom(file);
      Object.assign(job, info, { file, state: "ready", message: "Ready to install in TWiLight Menu++" });
      return;
    }
    const local = await getUploadedFile(job.id);
    const info = await inspectRom(local.file, local.folder || mediaInfo(local.name) ? 1 : 512);
    Object.assign(job, info, { file: local.file, state: "ready", message: "Ready to download to SD" });
    return;
  }

  const dir = job.system === "nds" ? path.join(CACHE_DIR, job.id) : path.join(CACHE_DIR, job.system, job.id);
  const cache = path.join(dir, `game.${job.system}`);
  const zip = path.join(dir, "source.zip.part");
  const extract = path.join(dir, "extract");
  await mkdir(dir, { recursive: true });
  let file: string | undefined;
  try { if ((await stat(cache)).isFile()) file = cache; } catch {}
  if (!file) {
    const local = path.join(systemDir(job.system), safeFileName(`${rom.n}.${job.system}`));
    try { if ((await stat(local)).isFile()) file = local; } catch {}
  }
  try {
    if (!file) {
      job.state = "downloading";
      job.message = data.meta.arch ? "PC is downloading the archive" : "PC is downloading the game";
      const response = await fetch(downloadUrl(data.meta, rom), { signal: AbortSignal.timeout(30 * 60_000) });
      if (!response.ok || !response.body) throw new DsiError(`Source: HTTP ${response.status}`, 502);
      job.total = Number(response.headers.get("content-length")) || rom.s;
      const md5 = createHash("md5");
      const meter = new Transform({ transform(chunk: Buffer, _encoding, callback) {
        job.received += chunk.length;
        if (job.received > MAX_ROM_SIZE + 16 * 1024 * 1024) return callback(new Error("Archive is too large"));
        md5.update(chunk);
        callback(null, chunk);
      } });
      await pipeline(Readable.fromWeb(response.body as Parameters<typeof Readable.fromWeb>[0]),
        meter, createWriteStream(zip));
      if (job.system === "nds" && md5.digest("hex") !== rom.m) throw new DsiError("Source archive MD5 mismatch", 502);
      if (data.meta.arch === "zip") {
        job.state = "extracting";
        job.message = "PC is extracting and verifying";
        const files = await extractZip(zip, extract);
        file = files.find((f) => f.toLowerCase().endsWith(`.${job.system}`));
        if (!file) throw new DsiError(`Archive contains no ${job.system.toUpperCase()} file`, 502);
      } else if (!data.meta.arch) {
        file = zip;
      } else throw new DsiError("Unsupported archive format for DSi downloads", 502);
    }
    const info = await inspectRom(file);
    if (job.system === "gba" && info.crc !== rom.m.toLowerCase()) {
      if (file === cache) await rm(cache, { force: true });
      throw new DsiError("GBA CRC32 mismatch. Check any existing local file before retrying.", 502);
    }

    if (file === zip || file.startsWith(extract + path.sep)) {
      await rename(file, cache);
      file = cache;
    }
    Object.assign(job, info, { file, state: "ready", message: "Ready to download to SD" });
  } finally {
    await rm(zip, { force: true });
    await rm(extract, { force: true, recursive: true });
  }
}

export async function prepare(id: string | null, requestedSystem?: string | null): Promise<DsiJob> {
  const system = dsiSystem(requestedSystem);
  validateId(id, system);
  const { rom } = await lookup(id, system);
  const key = `${system}:${id}`;
  const existing = store.jobs.get(key);
  if (existing && existing.state !== "error") return existing;
  if (store.active) throw new DsiError("PC is preparing another game. Please try again shortly.", 409);
  const extension = rom.folder ? "folder" : rom.media ? mediaInfo(rom.media)!.extension : rom.g?.includes("Themes") ? "theme" : system === "gba" ? "gba" : "nds";
  const name = `${ascii(rom.n, 100).replace(/[<>:"/\\|?*]/g, "_").replace(/^[. ]+|[. ]+$/g, "") || "Game"} [${id.slice(0, 8)}].${extension}`;
  const job: DsiJob = { id, system, state: "checking", received: 0, total: 0, size: 0, crc: "", name, message: "PC is checking for an existing file" };
  store.jobs.set(key, job);
  store.active = key;
  void prepareJob(job).catch((error) => {
    job.state = "error";
    job.message = error instanceof Error ? error.message : String(error);
    console.error("DSi prepare:", error);
  }).finally(() => { if (store.active === key) store.active = undefined; });
  return job;
}

export async function getJob(id: string | null, requestedSystem?: string | null): Promise<DsiJob> {
  const system = dsiSystem(requestedSystem);
  validateId(id, system);
  if (system === "others") {
    try { if (!store.jobs.get(`${system}:${id}`)?.name.endsWith(".theme")) await getUploadedFile(id); }
    catch (error) {
      if (error instanceof UploadError) throw new DsiError(error.message, error.status);
      throw error;
    }
  }
  const job = store.jobs.get(`${system}:${id}`);
  if (!job) throw new DsiError("Start preparation again. The PC server was restarted.", 404);
  return job;
}

export function byteRange(header: string | null, size: number): { start: number; end: number; partial: boolean } {
  if (!header) return { start: 0, end: size - 1, partial: false };
  const match = /^bytes=(\d+)-(\d*)$/.exec(header);
  if (!match) throw new DsiError("Invalid Range header", 416);
  const start = Number(match[1]);
  const end = match[2] ? Math.min(Number(match[2]), size - 1) : size - 1;
  if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start < 0 || start >= size || end < start)
    throw new DsiError("Requested range is outside the file", 416);
  return { start, end, partial: true };
}

export async function gameFile(request: Request, head = false): Promise<Response> {
  const params = new URL(request.url).searchParams;
  const job = await getJob(params.get("id"), params.get("system"));
  return preparedFile(request, job, head);
}

export async function appUpdate(request: Request, head = false): Promise<Response> {
  const file = path.join(process.cwd(), "public", "downloads", "DSiDrop.nds");
  let input;
  try { input = await open(file, "r"); }
  catch { throw new DsiError("DSiDrop update is not available on the PC server.", 503); }
  try {
    const header = Buffer.alloc(512);
    const { bytesRead } = await input.read(header, 0, header.length, 0);
    const info = await input.stat();
    validateHeader(header.subarray(0, bytesRead), info.size);
    if (header.subarray(0, 12).toString("ascii").replace(/\0/g, "").trim() !== "DSiDrop")
      throw new DsiError("The update file is not a DSiDrop application.", 502);
  } catch (error) {
    if (error instanceof UploadError) throw new DsiError("The DSiDrop update has an invalid NDS header.", 502);
    throw error;
  } finally { await input.close(); }
  const info = await inspectRom(file);
  const job: DsiJob = { id: "self-update", system: "nds", state: "ready", file, ...info,
    received: info.size, total: info.size, name: "DSiDrop.nds", message: "DSiDrop server build" };
  const params = new URL(request.url).searchParams;
  if (params.get("file") !== "1") {
    const response = dsiText(statusLine(job));
    return head ? new Response(null, { headers: response.headers }) : response;
  }
  if (params.get("crc") !== job.crc || params.get("size") !== String(job.size))
    throw new DsiError("The PC update changed. Check for updates again.", 409);
  return preparedFile(request, job, head);
}

async function preparedFile(request: Request, job: DsiJob, head: boolean): Promise<Response> {
  if (job.state !== "ready" || !job.file) throw new DsiError("Game is not ready yet", 409);
  const { size } = await stat(job.file);
  if (size !== job.size) throw new DsiError("PC file changed. Restart the PC server.", 409);
  const range = byteRange(request.headers.get("range"), size);
  const headers = new Headers({
    "Content-Type": "application/octet-stream",
    "Content-Length": String(range.end - range.start + 1),
    "Content-Disposition": `attachment; filename="${job.name}"`,
    "Accept-Ranges": "bytes",
    "Cache-Control": "no-store, no-transform",
    "X-ROM-CRC32": job.crc,
    "X-ROM-System": job.system,
    "X-Content-Type-Options": "nosniff",
  });
  if (range.partial) headers.set("Content-Range", `bytes ${range.start}-${range.end}/${size}`);
  if (head) return new Response(null, { status: range.partial ? 206 : 200, headers });
  const stream = createReadStream(job.file, { start: range.start, end: range.end });

  const cancel = () => stream.destroy();
  request.signal.addEventListener("abort", cancel, { once: true });
  stream.once("close", () => request.signal.removeEventListener("abort", cancel));
  return new Response(Readable.toWeb(stream) as ReadableStream, { status: range.partial ? 206 : 200, headers });
}
