import { createHash, randomBytes } from "node:crypto";
import { mkdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import type { SaveSnapshot, SaveTask } from "../save-types";
import { DeviceError, normalizeMac } from "./devices";
import { LibraryStore } from "./library-store";
import { ROMS_DIR } from "./rom-file";
import { themeCrc } from "./theme-package";

const LIMIT = 32 * 1024 * 1024;
const identifier = () => randomBytes(16).toString("hex");
export function savePath(value: unknown): string {
  if (typeof value !== "string" || Buffer.byteLength(value) > 220 || !value.startsWith("/") || !/\.sav$/i.test(value) || /[\x00-\x1f\x7f\\:<>"|?*]/.test(value)
    || value.slice(1).split("/").some((part) => !part || part === "." || part === ".." || /[. ]$/.test(part)) || value.split("/").length > 14)
    throw new DeviceError("Invalid SD save path.");
  return value;
}

export class SaveLibrary {
  private store: LibraryStore<SaveSnapshot & { fingerprints?: Record<string, string> }>;
  private connections = new Map<string, string>();
  constructor(private directory: string) { this.store = new LibraryStore(path.join(directory, "manifest.json"), () => ({ backups: [], tasks: [], automatic: {} })); }
  snapshot(mac: unknown) {
    const address = normalizeMac(mac);
    return this.store.run(async (data) => ({ backups: data.backups.filter((entry) => entry.mac === address), tasks: data.tasks.filter((entry) => entry.mac === address), automatic: { [address]: !!data.automatic[address] } }), false);
  }
  change(body: Record<string, unknown>) {
    const mac = normalizeMac(body.mac);
    let removed: string | undefined;
    return this.store.run(async (data) => {
      if (typeof body.automatic === "boolean") data.automatic[mac] = body.automatic;
      else if (body.action === "backup" || body.action === "restore" || body.action === "transfer") {
        if (data.tasks.some((task) => task.mac === mac && (task.status === "queued" || task.status === "running"))) throw new DeviceError("Finish the current save operation first.", 409);
        const sourceMac = body.action === "transfer" ? normalizeMac(body.sourceMac) : mac;
        if (body.action === "transfer" && sourceMac === mac) throw new DeviceError("Choose a different destination DSi.");
        const backup = body.action !== "backup" ? data.backups.find((entry) => entry.id === body.backup && entry.mac === sourceMac) : undefined;
        if (body.action !== "backup" && !backup) throw new DeviceError("Backup not found.", 404);
        const target = body.action === "transfer" || body.action === "backup" && body.path !== undefined ? savePath(body.path) : undefined;
        data.tasks.push({ id: identifier(), mac, action: body.action === "backup" ? "backup" : "restore", backup: backup?.id, path: target, sourceMac: body.action === "transfer" ? sourceMac : undefined, automatic: false, status: "queued", message: body.action === "transfer" ? "Save transfer queued for destination DSi" : target ? "Waiting for DSiDrop 1.15.0" : "Waiting for DSiDrop 1.14.0", createdAt: new Date().toISOString() });
      } else if (typeof body.delete === "string") {
        if (data.tasks.some((task) => task.backup === body.delete && (task.status === "queued" || task.status === "running"))) throw new DeviceError("This backup is waiting for restoration.", 409);
        const backup = data.backups.find((entry) => entry.id === body.delete && entry.mac === mac);
        if (!backup) throw new DeviceError("Backup not found.", 404);
        data.backups = data.backups.filter((entry) => entry !== backup);
        removed = backup.id;
      } else if (body.cancel === true) {
        for (const task of data.tasks.filter((entry) => entry.mac === mac && (entry.status === "queued" || entry.status === "running"))) { task.status = "error"; task.message = "Canceled on server"; }
      } else throw new DeviceError("Choose a save operation.");
      data.tasks = data.tasks.filter((entry) => entry.status === "running" || entry.status === "queued" || data.tasks.filter((task) => task.mac === entry.mac).slice(-20).includes(entry));
      return { ok: true };
    }).then(async (result) => {
      if (removed) await rm(path.join(this.directory, `${removed}.sav`), { force: true });
      return result;
    });
  }
  poll(params: URLSearchParams) {
    const mac = normalizeMac(params.get("mac")), session = params.get("session") || "";
    if (!/^[a-f0-9]{1,32}$/.test(session)) throw new DeviceError("Invalid connection session.");
    const fingerprint = params.get("fingerprint");
    if (fingerprint !== null && !/^[a-f0-9]{16}$/.test(fingerprint)) throw new DeviceError("Invalid save fingerprint.");
    const startup = params.get("startup");
    if (startup !== null && startup !== "0" && startup !== "1") throw new DeviceError("Invalid startup flag.");
    return this.store.run(async (data) => {
      const previous = this.connections.get(mac);
      const connected = previous !== session;
      this.connections.set(mac, session);
      const completed = data.tasks.find((task) => task.mac === mac && task.id === params.get("finished") && task.status === "running");
      if (completed) {
        completed.status = params.get("ok") === "1" ? "done" : "error"; completed.message = (params.get("message") || completed.status).slice(0, 180);
        if (completed.status === "done" && completed.automatic && fingerprint) (data.fingerprints ??= {})[mac] = fingerprint;
      }
      const history = data.tasks.filter((entry) => entry.mac === mac && (entry.status === "done" || entry.status === "error")).slice(-20);
      data.tasks = data.tasks.filter((entry) => entry.mac !== mac || entry.status === "queued" || entry.status === "running" || history.includes(entry));
      let task = data.tasks.find((entry) => entry.mac === mac && (entry.status === "queued" || entry.status === "running"));
      if (!task && connected && startup !== "0" && data.automatic[mac] && (!fingerprint || data.fingerprints?.[mac] !== fingerprint)) {
        task = { id: identifier(), mac, action: "backup", automatic: true, status: "queued", message: "Automatic backup", createdAt: new Date().toISOString() };
        data.tasks.push(task);
      }
      if (!task) return "RFSAVE1\tnone\n";
      if (task.action === "backup" && task.path && params.get("cap") !== "1") return "RFSAVE1\tnone\n";
      task.status = "running";
      task.message = task.action === "backup" ? "Backing up SD saves" : "Restoring save on DSi";
      if (task.action === "backup") return `RFSAVE1\tbackup\t${task.id}${task.path ? `\t${Buffer.from(task.path).toString("hex")}` : ""}\n`;
      const backup = data.backups.find((entry) => entry.id === task!.backup && entry.mac === (task!.sourceMac || mac));
      if (!backup) throw new DeviceError("Backup not found.", 404);
      return `RFSAVE1\trestore\t${task.id}\t${Buffer.from(task.path || backup.path).toString("hex")}\t${backup.size}\t${backup.crc}\n`;
    });
  }
  async upload(request: Request) {
    const params = new URL(request.url).searchParams, mac = normalizeMac(params.get("mac")), job = params.get("job");
    const encoded = params.get("path") || "";
    if (!/^(?:[a-f0-9]{2}){1,220}$/.test(encoded)) throw new DeviceError("Invalid save path encoding.");
    const target = savePath(Buffer.from(encoded, "hex").toString("utf8"));
    const declared = request.headers.get("content-length");
    if (!declared || !/^\d+$/.test(declared) || Number(declared) < 1 || Number(declared) > LIMIT) throw new DeviceError("Save files must be 1 byte to 32 MiB.", 413);
    await this.store.run(async (data) => { this.uploadTask(data, mac, job, target); }, false);
    const reader = request.body?.getReader();
    if (!reader) throw new DeviceError("Missing save data.");
    const chunks: Uint8Array[] = []; let size = 0;
    try { for (;;) { const part = await reader.read(); if (part.done) break; size += part.value.length; if (size > LIMIT || size > Number(declared)) { await reader.cancel(); throw new DeviceError("Save file too large.", 413); } chunks.push(part.value); } }
    finally { reader.releaseLock(); }
    const bytes = Buffer.concat(chunks), crc = themeCrc(bytes).toString(16).padStart(8, "0");
    if (size !== Number(declared) || crc !== params.get("crc")) throw new DeviceError("Save size or checksum mismatch.");
    return this.store.run(async (data) => {
      const task = this.uploadTask(data, mac, job, target);
      const hash = createHash("sha256").update(bytes).digest("hex");
      const versions = data.backups.filter((entry) => entry.mac === mac && entry.path === target);
      if (versions.some((entry) => entry.hash === hash && entry.job === job) || task.automatic && versions.at(-1)?.hash === hash) return;
      if (data.backups.filter((entry) => entry.mac === mac && entry.path === target).length >= 50) throw new DeviceError("50 versions reached for this save. Delete an older backup first.", 409);
      const id = identifier(); await mkdir(this.directory, { recursive: true });
      const temporary = path.join(this.directory, `${id}.tmp`), file = path.join(this.directory, `${id}.sav`);
      try { await writeFile(temporary, bytes); await rename(temporary, file); } finally { await rm(temporary, { force: true }); }
      data.backups.push({ id, mac, path: target, size, crc, hash, createdAt: new Date().toISOString(), automatic: task.automatic, job: task.id });
    });
  }
  private uploadTask(data: SaveSnapshot, mac: string, job: string | null, target: string): SaveTask {
    const task = data.tasks.find((entry) => entry.mac === mac && entry.id === job && entry.status === "running");
    if (!task || task.path && task.path !== target || (task.action === "restore" && !data.backups.some((entry) => entry.id === task.backup && entry.mac === (task.sourceMac || mac) && (task.path || entry.path) === target))) throw new DeviceError("This save operation is no longer active.", 409);
    return task;
  }
  file(mac: unknown, id: string | null, job: string | null) {
    const address = normalizeMac(mac);
    return this.store.run(async (data) => {
      const task = job ? data.tasks.find((entry) => entry.id === job && entry.mac === address && entry.status === "running" && entry.action === "restore") : undefined;
      const backup = data.backups.find((entry) => entry.mac === (job ? task?.sourceMac || address : address) && entry.id === (job ? task?.backup : id));
      if (!backup) throw new DeviceError("Backup not found.", 404);
      const bytes = await readFile(path.join(this.directory, `${backup.id}.sav`));
      if (bytes.length !== backup.size || createHash("sha256").update(bytes).digest("hex") !== backup.hash) throw new DeviceError("Backup integrity check failed.", 409);
      return { backup, bytes };
    }, false);
  }
}

const globalSaves = globalThis as typeof globalThis & { saveLibrary?: SaveLibrary };
export const saves = globalSaves.saveLibrary ??= new SaveLibrary(path.join(ROMS_DIR, ".saves"));