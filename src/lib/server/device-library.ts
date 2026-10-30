import { createHash, randomBytes } from "node:crypto";
import path from "node:path";
import { unstable_rethrow } from "next/navigation";
import type { DeviceFile, DeviceGame, DeviceLibrarySnapshot, DeviceLibraryTask, DeviceStorage } from "../device-library-types";
import type { DeviceSystem } from "../device-types";
import type { Dataset } from "../types";
import { coverUrl } from "../roms";
import { DeviceError, normalizeMac } from "./devices";
import { ascii, ndsDataset } from "./dsi";
import { LibraryStore } from "./library-store";
import { ROMS_DIR } from "./rom-file";
import { saves } from "./saves";
import { themeCrc } from "./theme-package";

export function sdLibraryPath(value: unknown): string {
  if (typeof value !== "string" || !value.startsWith("/") || Buffer.byteLength(value) > 220 || /[\x00-\x1f\x7f\\:<>"|?*]/.test(value)
    || value.slice(1).split("/").some((part) => !part || part === "." || part === ".." || /[. ]$/.test(part)) || value.split("/").length > 14)
    throw new DeviceError("Invalid SD library path.");
  return value;
}

export function managedRomPath(value: string): boolean {
  return /\.(nds|gba)$/i.test(value) && !/^\/(?:_nds|private|3ds)(?:\/|$)/i.test(value) && !/^\/boot\.nds$/i.test(value)
    && !/^dsidrop(?:\s*\[[a-f0-9]{8}\])?\.nds$/i.test(path.posix.basename(value));
}

export function parseDeviceInventory(text: string): DeviceFile[] {
  const lines = text.trimEnd().split("\n");
  if (lines.shift() !== "RFINVENTORY1" || lines.length > 4096) throw new DeviceError("Invalid device inventory.");
  const seen = new Set<string>();
  return lines.map((line) => {
    const [encoded, size, modified, header, extra] = line.split("\t");
    if (extra !== undefined || !/^(?:[a-f0-9]{2}){1,220}$/.test(encoded) || !/^\d{1,10}$/.test(size) || !/^\d{1,10}$/.test(modified) || !/^[a-f0-9]{8}$/.test(header)) throw new DeviceError("Invalid device inventory entry.");
    const target = sdLibraryPath(Buffer.from(encoded, "hex").toString("utf8"));
    if (Buffer.from(target).toString("hex") !== encoded || (!managedRomPath(target) && !/\.sav$/i.test(target)) || Number(size) > 512 * 1024 * 1024 || Number(modified) > 0xffffffff || seen.has(target.toLowerCase())) throw new DeviceError("Unsafe or duplicate device inventory entry.");
    seen.add(target.toLowerCase());
    return { path: target, size: Number(size), modified: Number(modified), header };
  });
}

const fileId = (target: string) => createHash("sha256").update(target.toLowerCase()).digest("hex").slice(0, 32);
const stem = (target: string) => path.posix.basename(target).replace(/\.(sav|nds|gba)$/i, "").replace(/\.(nds|gba)$/i, "");
const nameKey = (name: string) => ascii(name, 240).toLowerCase().replace(/^\d{4,5}\s*[-_]\s*/, "").replace(/[^a-z0-9]+/g, " ").trim();

export function deviceGames(files: DeviceFile[], backupPaths: string[], datasets: Dataset[]): DeviceGame[] {
  const entries = datasets.flatMap((data) => data.roms.map((rom) => ({ rom, meta: data.meta })));
  const byPrefix = new Map<string, typeof entries>(), byName = new Map<string, typeof entries>();
  for (const entry of entries) {
    const prefix = entry.rom.m.slice(0, 8);
    byPrefix.set(prefix, [...(byPrefix.get(prefix) || []), entry]);
    for (const key of new Set([nameKey(entry.rom.n), nameKey(entry.rom.t)])) byName.set(key, [...(byName.get(key) || []), entry]);
  }
  function resolve(target: string) {
    const base = stem(target), prefix = base.match(/\[([a-f0-9]{8})\]$/i)?.[1].toLowerCase();
    const candidates = prefix ? byPrefix.get(prefix) || [] : byName.get(nameKey(base)) || [];
    const platform = /\.(gba|gba\.sav)$/i.test(target) || /^\/roms\/gba\//i.test(target) ? "gba" : /^\/roms\/others\//i.test(target) ? "others" : /\.nds$/i.test(target) ? "nds" : null;
    const matching = candidates.filter((entry) => !platform || platform === entry.meta.id || platform === "nds" && entry.meta.id === "others");
    return matching.length === 1 ? matching[0] : null;
  }
  function game(target: string, size: number, installed: boolean): DeviceGame {
    const entry = resolve(target);
    return { id: fileId(target), path: installed ? target : null, title: entry?.rom.t || stem(target), cover: entry ? coverUrl(entry.meta, entry.rom) : null,
      system: entry ? entry.meta.id as DeviceSystem : installed ? /\.gba$/i.test(target) ? "gba" : "nds" : null, romId: entry?.rom.m || null, size, installed, savePaths: [] };
  }
  const games = files.filter((file) => managedRomPath(file.path)).map((file) => game(file.path, file.size, true));
  const saves = [...new Set([...files.filter((file) => /\.sav$/i.test(file.path)).map((file) => file.path), ...backupPaths])];
  for (const target of saves) {
    const key = target.toLowerCase();
    let matching = games.filter((entry) => entry.installed).filter((entry) => {
      const rom = entry.path!, directory = path.posix.dirname(rom), base = path.posix.basename(rom), name = base.replace(/\.(nds|gba)$/i, "");
      return [path.posix.join(directory, `${name}.sav`), path.posix.join(directory, "saves", `${name}.sav`), `${rom}.sav`, path.posix.join(directory, "saves", `${base}.sav`)].some((candidate) => candidate.toLowerCase() === key);
    });
    if (!matching.length) {
      const known = resolve(target);
      if (known) matching = games.filter((entry) => entry.romId === known.rom.m && entry.system === known.meta.id);
    }
    if (matching.length === 1) matching[0].savePaths.push(target);
    else { const archived = game(target, 0, false); archived.savePaths = [target]; games.push(archived); }
  }
  return games.sort((first, second) => Number(second.installed) - Number(first.installed) || first.title.localeCompare(second.title));
}

type Inventory = { files: DeviceFile[]; scannedAt: string | null; tasks: DeviceLibraryTask[]; storage?: DeviceStorage | null; fingerprint?: string };
const blankInventory = (): Inventory => ({ files: [], scannedAt: null, tasks: [], storage: null });
const newTask = (action: DeviceLibraryTask["action"], file?: DeviceFile): DeviceLibraryTask => ({ id: randomBytes(16).toString("hex"), action, file, status: "queued", message: "Waiting for DSiDrop 1.15.0", createdAt: new Date().toISOString() });
async function libraryDatasets() {
  const datasets = await Promise.all((["nds", "gba", "others"] as const).map(async (system) => {
    try { return await ndsDataset(system); } catch (error) { unstable_rethrow(error); return null; }
  }));
  return datasets.filter((entry): entry is Dataset => !!entry);
}

export class DeviceLibrary {
  private store: LibraryStore<Record<string, Inventory>>;
  private sessions = new Map<string, string>();
  constructor(file: string, private catalogs = libraryDatasets, private backups = async (mac: string) => (await saves.snapshot(mac)).backups.map((entry) => entry.path)) {
    this.store = new LibraryStore(file, () => ({}));
  }
  async snapshot(value: unknown): Promise<DeviceLibrarySnapshot> {
    const mac = normalizeMac(value);
    const [inventory, backupPaths, datasets] = await Promise.all([this.store.run(async (data) => data[mac] || blankInventory(), false), this.backups(mac), this.catalogs()]);
    return { games: deviceGames(inventory.files, backupPaths, datasets), scannedAt: inventory.scannedAt, task: inventory.tasks.at(-1) || null, storage: inventory.storage || null };
  }
  change(body: Record<string, unknown>) {
    const mac = normalizeMac(body.mac);
    return this.store.run(async (data) => {
      const inventory = data[mac] ??= blankInventory();
      const pending = inventory.tasks.find((task) => task.status === "queued" || task.status === "running");
      if (body.cancel === true) {
        if (pending) { pending.status = "error"; pending.message = "Cleared on server"; }
      } else {
        if (pending) throw new DeviceError("Finish the current library operation first.", 409);
        if (body.action === "scan") inventory.tasks.push(newTask("scan"));
        else if (body.action === "delete" && body.confirmed === true) {
          const file = inventory.files.find((entry) => fileId(entry.path) === body.game && managedRomPath(entry.path));
          if (!file) throw new DeviceError("ROM not found in this device's inventory. Refresh the games list first.", 404);
          inventory.tasks.push(newTask("delete", { ...file }));
        } else throw new DeviceError("Choose a library operation and confirm ROM deletion in the browser.");
      }
      inventory.tasks = inventory.tasks.slice(-20);
      return { ok: true };
    });
  }
  poll(params: URLSearchParams) {
    const mac = normalizeMac(params.get("mac")), session = params.get("session") || "";
    if (params.get("cap") !== "1" || !/^[a-f0-9]{1,32}$/.test(session)) throw new DeviceError("Requires DSiDrop 1.15.0.");
    const fingerprint = params.get("fingerprint");
    if (fingerprint !== null && !/^[a-f0-9]{16}$/.test(fingerprint)) throw new DeviceError("Invalid SD inventory fingerprint.");
    const startup = params.get("startup");
    if (startup !== null && startup !== "0" && startup !== "1") throw new DeviceError("Invalid startup flag.");
    const total = params.get("total"), free = params.get("free");
    let storage: DeviceStorage | undefined;
    if (total !== null || free !== null) {
      if (!total || !free || !/^\d{1,16}$/.test(total) || !/^\d{1,16}$/.test(free) || !Number.isSafeInteger(Number(total)) || !Number.isSafeInteger(Number(free)) || Number(total) <= 0 || Number(free) > Number(total)) throw new DeviceError("Invalid SD storage values.");
      storage = { total: Number(total), free: Number(free), updatedAt: new Date().toISOString() };
    }
    return this.store.run(async (data) => {
      const inventory = data[mac] ??= blankInventory();
      const connected = this.sessions.get(mac) !== session;
      this.sessions.set(mac, session);
      if (connected) inventory.storage = null;
      if (storage) inventory.storage = storage;
      const completed = inventory.tasks.find((task) => task.id === params.get("finished") && task.status === "running");
      if (completed) {
        completed.status = params.get("ok") === "1" ? "done" : "error";
        completed.message = (params.get("message") || completed.status).slice(0, 180);
        if (completed.status === "done" && completed.action === "delete") {
          inventory.files = inventory.files.filter((file) => file.path !== completed.file?.path);
          inventory.fingerprint = undefined;
        }
      }
      let task = inventory.tasks.find((entry) => entry.status === "queued" || entry.status === "running");
      const changed = !inventory.scannedAt || !fingerprint || fingerprint !== inventory.fingerprint;
      if (!task && (connected && startup !== "0" && changed || params.get("refresh") === "1")) { task = newTask("scan"); inventory.tasks.push(task); }
      inventory.tasks = inventory.tasks.slice(-20);
      if (!task) return "RFLIBRARY1\tnone\n";
      task.status = "running";
      task.message = task.action === "scan" ? "Reading games from SD" : "Deleting ROM on DSi; saves are preserved";
      if (task.action === "scan") return `RFLIBRARY1\tscan\t${task.id}\n`;
      const file = task.file!;
      return `RFLIBRARY1\tdelete\t${task.id}\t${Buffer.from(file.path).toString("hex")}\t${file.size}\t${file.modified}\t${file.header}\n`;
    });
  }
  async upload(request: Request) {
    const params = new URL(request.url).searchParams, mac = normalizeMac(params.get("mac")), job = params.get("job");
    const fingerprint = params.get("fingerprint");
    if (fingerprint !== null && !/^[a-f0-9]{16}$/.test(fingerprint)) throw new DeviceError("Invalid SD inventory fingerprint.");
    const limit = 2 * 1024 * 1024, declared = request.headers.get("content-length");
    if (!declared || !/^\d+$/.test(declared) || Number(declared) > limit || Number(declared) < 13) throw new DeviceError("Invalid inventory size.", 413);
    const active = (inventory?: Inventory) => inventory?.tasks.some((task) => task.id === job && task.action === "scan" && task.status === "running");
    await this.store.run(async (data) => { if (!active(data[mac])) throw new DeviceError("Inventory scan is no longer active.", 409); }, false);
    const reader = request.body?.getReader();
    if (!reader) throw new DeviceError("Missing inventory data.");
    const chunks: Uint8Array[] = []; let size = 0;
    try {
      for (;;) {
        const part = await reader.read(); if (part.done) break;
        size += part.value.length;
        if (size > limit || size > Number(declared)) { await reader.cancel(); throw new DeviceError("Inventory too large.", 413); }
        chunks.push(part.value);
      }
    } finally { reader.releaseLock(); }
    const bytes = Buffer.concat(chunks);
    if (size !== Number(declared) || themeCrc(bytes).toString(16).padStart(8, "0") !== params.get("crc")) throw new DeviceError("Inventory checksum mismatch.");
    const files = parseDeviceInventory(bytes.toString("ascii"));
    await this.store.run(async (data) => {
      if (!active(data[mac])) throw new DeviceError("Inventory scan is no longer active.", 409);
      data[mac].files = files; data[mac].scannedAt = new Date().toISOString();
      data[mac].fingerprint = fingerprint || undefined;
    });
  }
}

const globalLibrary = globalThis as typeof globalThis & { deviceLibrary?: DeviceLibrary };
export const deviceLibrary = globalLibrary.deviceLibrary ??= new DeviceLibrary(path.join(ROMS_DIR, ".device-library.json"));