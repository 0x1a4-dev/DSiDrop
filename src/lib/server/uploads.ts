import Busboy from "busboy";
import { constants } from "node:fs";
import { createHash, randomUUID } from "node:crypto";
import { createReadStream, createWriteStream } from "node:fs";
import { copyFile, link, mkdir, readFile, readdir, rename, rm, stat } from "node:fs/promises";
import path from "node:path";
import { Readable, Transform } from "node:stream";
import { pipeline } from "node:stream/promises";
import { unstable_rethrow } from "next/navigation";
import type { Dataset, Rom } from "../types";
import { mediaInfo } from "../media";
import { folderPath, MAX_FOLDER_FILES, MAX_FOLDER_SIZE, type UploadFolder } from "../upload-folder";
import { buildUploadFolder } from "./upload-folder";
import { themeCrc } from "./theme-package";
import { ROMS_DIR } from "./rom-file";

export const MAX_UPLOAD_SIZE = 512 * 1024 * 1024;
export const MAX_UPLOAD_BATCH_SIZE = 1024 * 1024 * 1024;
export const MAX_UPLOAD_FILES = 20;
const MAX_REQUEST_SIZE = MAX_UPLOAD_BATCH_SIZE + 1024 * 1024;
const DIRECTORY = path.join(ROMS_DIR, "others");
const APPS_DIRECTORY = path.resolve(process.env.APPS_DIR || path.join(process.cwd(), "public", "downloads", "apps"));
const MANIFEST = path.join(DIRECTORY, "library.json");
const ID = /^[a-f0-9]{32}$/;
type Entry = { id: string; name: string; title: string; size: number; uploadedAt: string; folder?: UploadFolder };
const appCache = new Map<string, { mtime: number; entry: Entry }>();
type Manifest = { version: 1; updatedAt: string; entries: Entry[] };
type Staged = Entry & { temporary: string; crc?: number };
type Locks = { tail: Promise<unknown> };
const shared = globalThis as typeof globalThis & { __uploadLocks?: Map<string, Locks> };
const locks = (shared.__uploadLocks ??= new Map());
const lock = locks.get(DIRECTORY) ?? { tail: Promise.resolve() };
locks.set(DIRECTORY, lock);

export class UploadError extends Error {
  constructor(message: string, public status = 400) { super(message); }
}

export function uploadError(error: unknown): Response {
  unstable_rethrow(error);
  if (!(error instanceof UploadError)) console.error("DSiDrop uploads:", error);
  return Response.json({ error: error instanceof UploadError ? error.message : "The local library could not be accessed. Please try again." },
    { status: error instanceof UploadError ? error.status : 500, headers: { "Cache-Control": "no-store" } });
}

function validateId(id: string | null): asserts id is string {
  if (!id || !ID.test(id)) throw new UploadError("Invalid uploaded game ID.");
}

function filePath(id: string, name: string): string {
  return path.join(/* turbopackIgnore: true */ DIRECTORY, `${id}.${name.endsWith(".folder") ? "folder" : mediaInfo(name)?.extension ?? "nds"}`);
}

async function readManifest(): Promise<Manifest> {
  let raw: string;
  try { raw = await readFile(MANIFEST, "utf8"); }
  catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") return { version: 1, updatedAt: new Date(0).toISOString(), entries: [] };
    throw error;
  }
  const manifest = JSON.parse(raw) as Manifest;
  if (manifest.version !== 1 || !Array.isArray(manifest.entries) || manifest.entries.some((entry) =>
    !ID.test(entry.id) || typeof entry.name !== "string" || typeof entry.title !== "string" ||
    (!entry.folder && !/\.nds$/i.test(entry.name) && !mediaInfo(entry.name)) ||
    (entry.folder && (!entry.name.endsWith(".folder") || !Array.isArray(entry.folder.files) || !entry.folder.files.length || entry.folder.files.length > MAX_FOLDER_FILES || !["Uploaded", "Multimedia"].includes(entry.folder.category))) ||
    !Number.isSafeInteger(entry.size) || entry.size < (entry.folder || mediaInfo(entry.name) ? 1 : 512) || entry.size > MAX_UPLOAD_SIZE)) {
    throw new Error("Invalid local library manifest");
  }
  return manifest;
}

async function appEntries(): Promise<Entry[]> {
  let files;
  try { files = await readdir(APPS_DIRECTORY, { withFileTypes: true }); }
  catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") return [];
    throw error;
  }
  const entries: Entry[] = [];
  const titles: Record<string, string> = { "notpipe.nds": "notPipe DSi", "dsorganize.nds": "DSOrganize 3.2", "pkmn-chest.nds": "pkmn-chest" };
  for (const file of files) {
    if (!file.isFile() || !/\.nds$/i.test(file.name)) continue;
    const location = path.join(APPS_DIRECTORY, file.name);
    const info = await stat(location);
    if (info.size < 512 || info.size > MAX_UPLOAD_SIZE) continue;
    const cached = appCache.get(file.name);
    if (cached?.mtime === info.mtimeMs && cached.entry.size === info.size) {
      entries.push(cached.entry);
      continue;
    }
    const digest = createHash("md5");
    let header = Buffer.alloc(0);
    for await (const chunk of createReadStream(location)) {
      digest.update(chunk);
      if (header.length < 512) header = Buffer.concat([header, chunk.subarray(0, 512 - header.length)]);
    }
    try { validateHeader(header, info.size); }
    catch (error) { if (error instanceof UploadError) continue; throw error; }
    const entry: Entry = { id: digest.digest("hex"), name: file.name,
      title: titles[file.name.toLowerCase()] ?? file.name.slice(0, -4), size: info.size, uploadedAt: info.mtime.toISOString() };
    appCache.set(file.name, { mtime: info.mtimeMs, entry });
    entries.push(entry);
  }
  return [...new Map(entries.map((entry) => [entry.id, entry])).values()];
}

async function asDataset(manifest: Manifest): Promise<Dataset> {
  const apps = await appEntries();
  const hasDsOrganizePackage = await stat(path.join(APPS_DIRECTORY, "DSOrganize_3_2.zip")).then((info) => info.isFile(), () => false);
  const appIds = new Set(apps.map((entry) => entry.id));
  const roms: Rom[] = manifest.entries.filter((entry) => !appIds.has(entry.id)).map((entry) => {
    const previewNames = entry.folder?.category === "Multimedia" ? entry.folder.files.filter((file) => file.size > 0).map((file) => file.name) : [entry.name];
    const thumbnail = previewNames.some((name) => mediaInfo(name)?.kind === "Image");
    return {
      m: entry.id, n: entry.title, t: entry.title, r: "Unknown", s: entry.size, c: thumbnail ? entry.id : null,
      ...(thumbnail ? { thumbnail: true as const } : {}),
      g: [entry.folder?.category ?? (mediaInfo(entry.name) ? "Multimedia" : "Uploaded")],
      ...(entry.folder ? { folder: { destination: entry.folder.destination, count: entry.folder.files.length }, e: "folder" } : {}),
      ...(mediaInfo(entry.name) ? { media: entry.name, e: mediaInfo(entry.name)!.extension } : {}),
    };
  });
  roms.push(...await Promise.all(apps.map(async (entry): Promise<Rom> => {
    const cover = entry.name.slice(0, -4).toLowerCase().replace(/[^a-z0-9-]/g, "-");
    const hasCover = await stat(path.join(APPS_DIRECTORY, "covers", `${cover}.png`)).then((info) => info.isFile(), () => false);
    return { m: entry.id, n: entry.title, t: entry.title, r: "World", s: entry.size, c: hasCover ? cover : null, g: ["Apps"],
      ...(entry.name.toLowerCase() === "dsorganize.nds" && hasDsOrganizePackage ? { package: "/downloads/apps/DSOrganize_3_2.zip" } : {}),
    };
  })));
  roms.sort((a, b) => a.t.localeCompare(b.t, "en") || a.m.localeCompare(b.m));
  const uploaded = roms.filter((rom) => rom.g?.includes("Uploaded")).length;
  const multimedia = roms.filter((rom) => rom.g?.includes("Multimedia")).length;
  return { meta: {
    id: "others", label: "Others", short: "Others", ext: "nds", arch: null, out: "nds", checksum: "md5",
    source: "/?sys=others", download: "/api/library/file?id=", coverSource: "NDS application icons", coverBase: "/downloads/apps/covers/",
    generatedAt: [manifest.updatedAt, ...apps.map((entry) => entry.uploadedAt)].sort().at(-1)!, total: roms.length, withCover: roms.filter((rom) => rom.c).length,
    regions: [...(uploaded + multimedia ? [{ name: "Unknown", count: uploaded + multimedia }] : []), ...(apps.length ? [{ name: "World", count: apps.length }] : [])],
    tags: [...(uploaded ? [{ name: "Uploaded", count: uploaded }] : []), ...(apps.length ? [{ name: "Apps", count: apps.length }] : []),
      ...(multimedia ? [{ name: "Multimedia", count: multimedia }] : [])], languages: [],
  }, roms };
}

export async function uploadedDataset(): Promise<Dataset> {
  return asDataset(await readManifest());
}

export async function getUploadedFile(id: string): Promise<{ file: string; name: string; title: string; size: number; folder?: UploadFolder }> {
  validateId(id);
  const app = (await appEntries()).find((entry) => entry.id === id);
  if (app) return { file: path.join(APPS_DIRECTORY, app.name), name: app.name, title: app.title, size: app.size };
  const entry = (await readManifest()).entries.find((item) => item.id === id);
  if (!entry) throw new UploadError("The uploaded game was not found.", 404);
  const file = filePath(id, entry.name);
  let info;
  try { info = await stat(file); }
  catch { throw new UploadError("The uploaded game file is missing from the PC.", 404); }
  if (!info.isFile() || info.size !== entry.size) throw new UploadError("The uploaded game file has changed on the PC. Restore the original file before retrying.", 409);
  return { file, name: entry.name, title: entry.title, size: entry.size, ...(entry.folder ? { folder: entry.folder } : {}) };
}

function safeUploadName(value: string): string {
  if (!value || /[/\\\x00-\x1f\x7f]/.test(value)) throw new UploadError("Upload a file name without folders or control characters.");
  const name = value.normalize("NFC").trim();
  const extension = mediaInfo(name)?.extension ?? (/\.nds$/i.test(name) ? "nds" : null);
  if (!extension) throw new UploadError("Choose an NDS file or a supported video, music or image file.", 415);
  const title = name.slice(0, -extension.length - 1).replace(/[<>:"|?*]/g, "_").replace(/^[. ]+|[. ]+$/g, "").slice(0, 160);
  if (!title) throw new UploadError("The uploaded file needs a file name.");
  return `${title}.${extension}`;
}

export function validateHeader(header: Buffer, size: number): void {
  if (size < 512 || header.length < 512 || ![0, 2, 3].includes(header[0x12]))
    throw new UploadError("This file is not a valid Nintendo DS ROM.", 415);
  const ranges = [0x20, 0x30].map((offset) => ({ start: header.readUInt32LE(offset), length: header.readUInt32LE(offset + 12) }));
  for (const range of ranges) {
    if (range.start < 512 || !range.length || range.start + range.length > size)
      throw new UploadError("The NDS header points outside the uploaded file.", 415);
  }
  if (ranges[0].start < ranges[1].start + ranges[1].length && ranges[1].start < ranges[0].start + ranges[0].length)
    throw new UploadError("The NDS executable sections overlap.", 415);
}

function sameOrigin(request: Request): void {
  const origin = request.headers.get("origin");
  if (!origin) return;

  const url = new URL(request.url);
  const forwarded = request.headers.get("x-forwarded-proto")?.split(",")[0].trim().toLowerCase();
  const protocol = forwarded === "http" || forwarded === "https" ? forwarded : url.protocol.slice(0, -1);
  const host = request.headers.get("host") ?? url.host;
  let expected = "";
  try {
    if (/[/\\?#@\s]/.test(host)) throw new Error("Invalid Host header");
    expected = new URL(`${protocol}://${host}`).origin;
  } catch {}
  if (!expected || origin !== expected) throw new UploadError("Manage local games from the DSiDrop page on this server.", 403);
}

export async function deleteUploaded(request: Request): Promise<{ deleted: string; dataset: Dataset }> {
  sameOrigin(request);
  const id = new URL(request.url).searchParams.get("id");
  validateId(id);
  if ((await appEntries()).some((entry) => entry.id === id)) throw new UploadError("Bundled apps cannot be deleted from the upload library.", 403);

  const run = lock.tail.then(async () => {
    const manifest = await readManifest();
    if (!manifest.entries.some((entry) => entry.id === id)) throw new UploadError("The uploaded game was not found.", 404);
    const updated: Manifest = { version: 1, updatedAt: new Date().toISOString(), entries: manifest.entries.filter((entry) => entry.id !== id) };
    const token = randomUUID();
    const temporaryManifest = path.join(DIRECTORY, `.manifest-${token}.part`);
    const rollbackManifest = path.join(DIRECTORY, `.manifest-${token}-rollback.part`);
    const removedFile = path.join(DIRECTORY, `.delete-${token}.part`);
    const entry = manifest.entries.find((item) => item.id === id)!;
    const file = filePath(id, entry.name);
    let moved = false;
    let published = false;
    try {
      await pipeline(Readable.from([JSON.stringify(updated, null, 2)]), createWriteStream(temporaryManifest, { flags: "wx" }));
      await pipeline(Readable.from([JSON.stringify(manifest, null, 2)]), createWriteStream(rollbackManifest, { flags: "wx" }));
      try { await rename(file, removedFile); moved = true; }
      catch (error) {
        if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
      }
      await rename(temporaryManifest, MANIFEST);
      published = true;
      if (moved) await rm(removedFile);
      await rm(path.join(DIRECTORY, ".thumbnails", `${id}.jpg`), { force: true }).catch(() => undefined);
      return { deleted: id, dataset: await asDataset(updated) };
    } catch (error) {
      if (moved) await rename(removedFile, file);
      if (published) await rename(rollbackManifest, MANIFEST);
      if (["EBUSY", "EACCES", "EPERM"].includes((error as NodeJS.ErrnoException).code ?? ""))
        throw new UploadError("This game file is in use. Finish its download and try deleting it again.", 409);
      throw error;
    } finally {
      await Promise.all([temporaryManifest, rollbackManifest].map((file) => rm(file, { force: true })));
    }
  });
  lock.tail = run.catch(() => undefined);
  return run;
}

async function commit(staged: Staged[], signal: AbortSignal): Promise<{ uploaded: number; duplicates: number; dataset: Dataset }> {
  const run = lock.tail.then(async () => {
    const manifest = await readManifest();
    const entries = [...manifest.entries];
    const added: string[] = [];
    let uploaded = 0;
    let duplicates = 0;
    const temporaryManifest = path.join(DIRECTORY, `.manifest-${randomUUID()}.part`);
    try {
      for (const item of staged) {
        if (signal.aborted) throw new UploadError("The upload was canceled.", 499);
        const recorded = entries.find((entry) => entry.id === item.id);
        const destination = filePath(item.id, recorded?.name ?? item.name);
        if (recorded) {
          try {
            const info = await stat(destination);
            if (!info.isFile() || info.size !== item.size) throw new UploadError("An existing library file has conflicting contents.", 409);
            duplicates++;
            continue;
          } catch (error) {
            if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
          }
        }
        try {
          try { await link(item.temporary, destination); }
          catch (error) {
            if (!["EPERM", "ENOTSUP", "EXDEV"].includes((error as NodeJS.ErrnoException).code ?? "")) throw error;
            await copyFile(item.temporary, destination, constants.COPYFILE_EXCL);
          }
          added.push(destination);
        }
        catch (error) {
          if ((error as NodeJS.ErrnoException).code !== "EEXIST") throw error;
          const existing = createHash("md5");
          for await (const chunk of createReadStream(destination)) existing.update(chunk);
          if (existing.digest("hex") !== item.id) throw new UploadError("An existing library file has conflicting contents.", 409);
        }
        if (!recorded) entries.push({ id: item.id, name: item.name, title: item.title, size: item.size, uploadedAt: item.uploadedAt, ...(item.folder ? { folder: item.folder } : {}) });
        uploaded++;
      }
      if (uploaded) {
        if (signal.aborted) throw new UploadError("The upload was canceled.", 499);
        const updated: Manifest = { version: 1, updatedAt: new Date().toISOString(), entries };
        const output = createWriteStream(temporaryManifest, { flags: "wx" });
        await pipeline(Readable.from([JSON.stringify(updated, null, 2)]), output);
        if (signal.aborted) throw new UploadError("The upload was canceled.", 499);
        await rename(temporaryManifest, MANIFEST);
        return { uploaded, duplicates, dataset: await asDataset(updated) };
      }
      return { uploaded, duplicates, dataset: await asDataset(manifest) };
    } catch (error) {
      await Promise.all(added.map((file) => rm(file, { force: true })));
      throw error;
    } finally { await rm(temporaryManifest, { force: true }); }
  });
  lock.tail = run.catch(() => undefined);
  return run;
}

export async function receiveUploads(request: Request): Promise<{ uploaded: number; duplicates: number; dataset: Dataset }> {
  sameOrigin(request);
  const mode = new URL(request.url).searchParams.get("folder");
  if (mode !== null && mode !== "uploaded" && mode !== "multimedia") throw new UploadError("Choose a valid folder category.");
  const fileLimit = mode ? MAX_FOLDER_FILES : MAX_UPLOAD_FILES;
  if (!request.body) throw new UploadError("Choose one or more files to upload.");
  const contentLength = Number(request.headers.get("content-length"));
  if (contentLength > MAX_REQUEST_SIZE) throw new UploadError("Each upload batch must be 1 GiB or smaller.", 413);
  let parser;
  try {
    parser = Busboy({ headers: { "content-type": request.headers.get("content-type") ?? "" }, preservePath: true, defParamCharset: "utf8",
      limits: { fileSize: MAX_UPLOAD_SIZE + 1, files: fileLimit, fields: 0, parts: fileLimit + 1 } });
  } catch { throw new UploadError("Send files using a multipart form upload."); }
  await mkdir(DIRECTORY, { recursive: true });
  const temporaryFiles: string[] = [];
  const staged: Staged[] = [];
  const tasks: Promise<void>[] = [];
  const streams: Readable[] = [];
  let failure: unknown;
  let payloadBytes = 0;
  const fail = (error: unknown) => { failure ??= error; };
  parser.on("filesLimit", () => fail(new UploadError(`Upload no more than ${fileLimit} files at a time.`, 413)));
  parser.on("fieldsLimit", () => fail(new UploadError("Only file fields are supported.")));
  parser.on("partsLimit", () => fail(new UploadError(`Upload no more than ${fileLimit} files at a time.`, 413)));
  parser.on("file", (field, stream, info) => {
    streams.push(stream);
    let name: string;
    try {
      if (field !== "files") throw new UploadError("Use the files field for uploads.");
      name = mode ? folderPath(info.filename ?? "") : safeUploadName(info.filename ?? "");
    } catch (error) { fail(error); stream.resume(); return; }
    const temporary = path.join(DIRECTORY, `.upload-${randomUUID()}.part`);
    temporaryFiles.push(temporary);
    const digest = createHash("md5");
    let size = 0;
    let crc = 0;
    let header = Buffer.alloc(0);
    let truncated = false;
    stream.on("limit", () => { truncated = true; });
    const meter = new Transform({ transform(chunk: Buffer, _encoding, callback) {
      size += chunk.length;
      payloadBytes += chunk.length;
      if (payloadBytes > MAX_UPLOAD_BATCH_SIZE) return callback(new UploadError("Each upload batch must be 1 GiB or smaller.", 413));
      if (mode && payloadBytes > MAX_FOLDER_SIZE) return callback(new UploadError("Each folder must be 512 MiB or smaller.", 413));
      digest.update(chunk);
      if (mode) crc = themeCrc(chunk, crc);
      if (header.length < 512) header = Buffer.concat([header, chunk.subarray(0, 512 - header.length)]);
      callback(null, chunk);
    } });
    tasks.push(pipeline(stream, meter, createWriteStream(temporary, { flags: "wx" }), { signal: request.signal }).then(() => {
      if (truncated || size > MAX_UPLOAD_SIZE) throw new UploadError("Each file must be 512 MiB or smaller.", 413);
      if (mode) {
        if (/\.nds$/i.test(name)) validateHeader(header, size);
      } else if (mediaInfo(name)) {
        if (!size) throw new UploadError("Empty media files cannot be uploaded.", 415);
      } else validateHeader(header, size);
      staged.push({ id: digest.digest("hex"), name, title: name.slice(0, name.lastIndexOf(".")), size, uploadedAt: new Date().toISOString(), temporary, crc });
    }).catch((error) => { fail(error); parser.destroy(error as Error); }));
  });
  let received = 0;
  const totalMeter = new Transform({ transform(chunk: Buffer, _encoding, callback) {
    received += chunk.length;
    callback(received > MAX_REQUEST_SIZE ? new UploadError("Each upload batch must be 1 GiB or smaller.", 413) : null, chunk);
  } });
  try {
    try {
      await pipeline(Readable.fromWeb(request.body as Parameters<typeof Readable.fromWeb>[0]), totalMeter, parser, { signal: request.signal });
    } catch (error) {
      fail(error);
      streams.forEach((stream) => stream.destroy());
    }
    await Promise.all(tasks);
    if (request.signal.aborted) throw new UploadError("The upload was canceled.", 499);
    if (failure) {
      if (failure instanceof UploadError) throw failure;
      throw new UploadError("The upload was interrupted or the multipart data is incomplete.");
    }
    if (!staged.length) throw new UploadError("Choose one or more files to upload.");
    if (mode) {
      let folder;
      try { folder = await buildUploadFolder(staged.map((file) => ({ ...file, crc: file.crc! })), mode === "multimedia", DIRECTORY, request.signal); }
      catch (error) { throw new UploadError(error instanceof Error ? error.message : "Folder import failed."); }
      temporaryFiles.push(folder.temporary);
      return await commit([folder], request.signal);
    }
    return await commit(staged, request.signal);
  } finally { await Promise.all(temporaryFiles.map((file) => rm(file, { force: true }))); }
}

function rangeFor(header: string | null, size: number): { start: number; end: number; partial: boolean } {
  if (!header) return { start: 0, end: size - 1, partial: false };
  const match = /^bytes=(\d*)-(\d*)$/.exec(header);
  if (!match || (!match[1] && !match[2])) throw new UploadError("Invalid byte range.", 416);
  const suffix = !match[1];
  const start = suffix ? Math.max(0, size - Number(match[2])) : Number(match[1]);
  const end = suffix || !match[2] ? size - 1 : Math.min(size - 1, Number(match[2]));
  if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start < 0 || start >= size || end < start)
    throw new UploadError("Requested byte range is outside the game file.", 416);
  return { start, end, partial: true };
}

export async function uploadFileResponse(request: Request, head = false): Promise<Response> {
  const id = new URL(request.url).searchParams.get("id");
  validateId(id);
  const entry = await getUploadedFile(id);
  let range;
  try { range = rangeFor(request.headers.get("range"), entry.size); }
  catch (error) {
    if (error instanceof UploadError && error.status === 416) return new Response(null, { status: 416, headers: {
      "Content-Range": `bytes */${entry.size}`, "Content-Length": "0", "Cache-Control": "no-store",
    } });
    throw error;
  }
  const fallback = entry.name.normalize("NFKD").replace(/[^\x20-\x7e]|["\\]/g, "_");
  const headers = new Headers({ "Content-Type": "application/octet-stream", "Accept-Ranges": "bytes",
    "Content-Length": String(range.end - range.start + 1), "Cache-Control": "no-store, no-transform",
    "Content-Disposition": `attachment; filename="${fallback}"; filename*=UTF-8''${encodeURIComponent(entry.name)}`,
    "X-Content-Type-Options": "nosniff", "X-ROM-System": "others" });
  if (range.partial) headers.set("Content-Range", `bytes ${range.start}-${range.end}/${entry.size}`);
  if (head) return new Response(null, { status: range.partial ? 206 : 200, headers });
  const stream = createReadStream(entry.file, { start: range.start, end: range.end });
  const cancel = () => stream.destroy();
  request.signal.addEventListener("abort", cancel, { once: true });
  stream.once("close", () => request.signal.removeEventListener("abort", cancel));
  if (request.signal.aborted) stream.destroy();
  return new Response(Readable.toWeb(stream) as ReadableStream, { status: range.partial ? 206 : 200, headers });
}
