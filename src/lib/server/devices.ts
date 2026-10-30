import { randomUUID } from "node:crypto";
import { mkdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { unstable_rethrow } from "next/navigation";
import type { DeviceClientState, DeviceQueueInput, DeviceSnapshot, DeviceSystem, DsiDevice } from "../device-types";
import { ascii, lookup } from "./dsi";
import { supportsMultimedia } from "../media";
import { supportsFolderTransfer } from "../upload-folder";
import { ROMS_DIR } from "./rom-file";

export const DEVICE_QUEUE_LIMIT = 32;
export const DEVICE_POLL_INTERVAL = 3000;
export const DEVICE_OFFLINE_AFTER = 20_000;
const MAX_DEVICES = 64;
const MAX_HISTORY = 96;
const MAX_BYTES = 512 * 1024 * 1024;
const TASK_ID = /^[a-f0-9]{32}$/;
const STATES = new Set<DeviceClientState>(["idle", "busy", "downloading", "done", "error", "canceled"]);
const TERMINAL = new Set(["done", "error", "canceled"]);
type StoredDevice = Omit<DsiDevice, "online">;
type Manifest = { version: 1; devices: StoredDevice[] };
type Lookup = (id: string, system: DeviceSystem) => Promise<{ rom: { t: string; r: string; s: number; media?: string; folder?: unknown } }>;
type Poll = { mac: string; ip: string; version: string; task: string; state: DeviceClientState; received: number; size: number; message: string };

export class DeviceError extends Error {
  constructor(message: string, public status = 400) { super(message); }
}

export function deviceError(error: unknown): Response {
  unstable_rethrow(error);
  if (!(error instanceof DeviceError)) console.error("DSiDrop devices:", error);
  return Response.json({ error: error instanceof DeviceError ? error.message : "The DSi device registry could not be accessed. Please try again." }, {
    status: error instanceof DeviceError ? error.status : 500, headers: { "Cache-Control": "no-store" },
  });
}

export function normalizeMac(value: unknown): string {
  if (typeof value !== "string" || !/^(?:[a-f0-9]{12}|(?:[a-f0-9]{2}:){5}[a-f0-9]{2}|(?:[a-f0-9]{2}-){5}[a-f0-9]{2})$/i.test(value))
    throw new DeviceError("A valid DSi MAC address is required.");
  const mac = value.replace(/[:-]/g, "").toLowerCase();
  if (mac === "000000000000" || (parseInt(mac.slice(0, 2), 16) & 1)) throw new DeviceError("A valid unicast DSi MAC address is required.");
  return mac;
}

function system(value: unknown): DeviceSystem {
  if (value !== "nds" && value !== "gba" && value !== "others") throw new DeviceError("Choose NDS, GBA or Others for a DSi download.");
  return value;
}

function romId(value: unknown, platform: DeviceSystem): string {
  if (typeof value !== "string" || !(platform === "gba" ? /^[a-f0-9]{8}$/ : TASK_ID).test(value))
    throw new DeviceError("Invalid game ID.");
  return value;
}

function taskId(value: unknown): string {
  if (typeof value !== "string" || !TASK_ID.test(value)) throw new DeviceError("Invalid device task ID.");
  return value;
}

function name(value: unknown): string {
  if (typeof value !== "string" || !value.trim() || value.trim().length > 64 || /[\x00-\x1f\x7f]/.test(value))
    throw new DeviceError("Enter a device name with 1–64 characters.");
  return value.trim();
}

function bytes(value: string | null): number {
  if (value == null || value === "") return 0;
  if (!/^\d{1,10}$/.test(value)) throw new DeviceError("Invalid transfer size.");
  const count = Number(value);
  if (!Number.isSafeInteger(count) || count > MAX_BYTES) throw new DeviceError("Invalid transfer size.");
  return count;
}

export function clientPoll(params: URLSearchParams): Poll {
  const mac = normalizeMac(params.get("mac"));
  const ip = params.get("ip") ?? "";
  if (ip && (!/^(\d{1,3}\.){3}\d{1,3}$/.test(ip) || ip.split(".").some((part) => Number(part) > 255)))
    throw new DeviceError("Invalid client IPv4 address.");
  const version = params.get("version") ?? "";
  if (!/^[a-zA-Z0-9.+_-]{1,32}$/.test(version)) throw new DeviceError("Invalid client version.");
  const state = params.get("state") as DeviceClientState;
  if (!STATES.has(state)) throw new DeviceError("Invalid client state.");
  const task = params.get("task") ?? "-";
  if (task !== "-") taskId(task);
  return { mac, ip, version, task, state, received: bytes(params.get("received")), size: bytes(params.get("size")), message: ascii(params.get("message") ?? "", 160) };
}

export function deviceSameOrigin(request: Request): void {
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
  if (!expected || origin !== expected) throw new DeviceError("Manage DSi devices from the DSiDrop page on this server.", 403);
}

export async function deviceBody(request: Request): Promise<Record<string, unknown>> {
  deviceSameOrigin(request);
  const declared = request.headers.get("content-length");
  if (declared && (!/^\d+$/.test(declared) || Number(declared) > 65_536)) throw new DeviceError("The device request is too large.", 413);
  const reader = request.body?.getReader();
  if (!reader) throw new DeviceError("A JSON request body is required.");
  const chunks: Uint8Array[] = [];
  let length = 0;
  try {
    for (;;) {
      const result = await reader.read();
      if (result.done) break;
      length += result.value.byteLength;
      if (length > 65_536) { await reader.cancel(); throw new DeviceError("The device request is too large.", 413); }
      chunks.push(result.value);
    }
  } finally { reader.releaseLock(); }
  let data;
  try { data = JSON.parse(Buffer.concat(chunks).toString("utf8")) as unknown; }
  catch { throw new DeviceError("Invalid JSON request."); }
  if (!data || typeof data !== "object" || Array.isArray(data)) throw new DeviceError("Invalid device request.");
  return data as Record<string, unknown>;
}

export class DeviceRegistry {
  private devices?: StoredDevice[];
  private tail: Promise<unknown> = Promise.resolve();
  private lastWritten = 0;
  private dirty = false;

  constructor(private file: string, private resolveGame: Lookup = lookup, private now: () => number = Date.now) {}

  private async serial<T>(action: () => Promise<T>): Promise<T> {
    const run = this.tail.then(action);
    this.tail = run.catch(() => undefined);
    return run;
  }

  private async load(): Promise<StoredDevice[]> {
    if (this.devices) return this.devices;
    let raw: string;
    try { raw = await readFile(this.file, "utf8"); }
    catch (error) {
      if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
      return (this.devices = []);
    }
    const manifest = JSON.parse(raw) as Manifest;
    if (manifest.version !== 1 || !Array.isArray(manifest.devices) || manifest.devices.length > MAX_DEVICES) throw new Error("Invalid DSi registry manifest");
    const addresses = new Set<string>();
    const tasks = new Set<string>();
    for (const device of manifest.devices) {
      if (normalizeMac(device.mac) !== device.mac || addresses.has(device.mac) || name(device.name) !== device.name ||
          typeof device.paused !== "boolean" || !STATES.has(device.state) || typeof device.ip !== "string" ||
          typeof device.version !== "string" || !Array.isArray(device.tasks) || device.tasks.length > MAX_HISTORY + DEVICE_QUEUE_LIMIT ||
          !Number.isFinite(Date.parse(device.createdAt)) || !Number.isFinite(Date.parse(device.updatedAt))) throw new Error("Invalid DSi registry device");
      addresses.add(device.mac);
      let active = 0, pending = 0;
      for (const task of device.tasks) {
        taskId(task.id); romId(task.romId, system(task.system));
        if (tasks.has(task.id) || typeof task.title !== "string" || typeof task.region !== "string" || typeof task.message !== "string" ||
            !["queued", "downloading", "done", "error", "canceled"].includes(task.status) ||
            !Number.isSafeInteger(task.received) || task.received < 0 || task.received > MAX_BYTES ||
            !Number.isSafeInteger(task.size) || task.size < 0 || task.size > MAX_BYTES ||
            !Number.isFinite(Date.parse(task.addedAt)) || !Number.isFinite(Date.parse(task.updatedAt))) throw new Error("Invalid DSi registry task");
        tasks.add(task.id);
        if (task.status === "downloading") active++;
        if (!TERMINAL.has(task.status)) pending++;
      }
      if (active > 1 || pending > DEVICE_QUEUE_LIMIT) throw new Error("Invalid DSi registry queue");

      device.lastSeen = null;
    }
    return (this.devices = manifest.devices);
  }

  private trim(device: StoredDevice): void {
    const history = device.tasks.filter((task) => TERMINAL.has(task.status));
    const remove = new Set(history.slice(0, Math.max(0, history.length - MAX_HISTORY)).map((task) => task.id));
    device.tasks = device.tasks.filter((task) => !remove.has(task.id));
  }

  private async persist(): Promise<void> {
    this.dirty = true;
    await mkdir(path.dirname(this.file), { recursive: true });
    const temporary = `${this.file}.${randomUUID()}.tmp`;
    try {
      await writeFile(temporary, JSON.stringify({ version: 1, devices: this.devices ?? [] } satisfies Manifest, null, 2), "utf8");
      await rename(temporary, this.file);
      this.dirty = false;
      this.lastWritten = this.now();
    } finally { await rm(temporary, { force: true }); }
  }

  private view(): DeviceSnapshot {
    return { devices: (this.devices ?? []).map((device) => ({ ...device, tasks: device.tasks.map((task) => ({ ...task })),
      online: device.lastSeen !== null && this.now() - Date.parse(device.lastSeen) < DEVICE_OFFLINE_AFTER,
    })), limit: DEVICE_QUEUE_LIMIT, pollInterval: DEVICE_POLL_INTERVAL, offlineAfter: DEVICE_OFFLINE_AFTER };
  }

  private find(mac: unknown): StoredDevice {
    const device = this.devices!.find((entry) => entry.mac === normalizeMac(mac));
    if (!device) throw new DeviceError("Open DSiDrop on this device first so it can connect to this PC.", 404);
    return device;
  }

  snapshot(): Promise<DeviceSnapshot> {
    return this.serial(async () => { await this.load(); return this.view(); });
  }

  update(body: Record<string, unknown>): Promise<DeviceSnapshot> {
    return this.serial(async () => {
      await this.load();
      const device = this.find(body.mac);
      if (body.name === undefined && body.paused === undefined && body.retryTask === undefined) throw new DeviceError("Choose a device change.");
      const renamed = body.name === undefined ? undefined : name(body.name);
      if (body.paused !== undefined && typeof body.paused !== "boolean") throw new DeviceError("Invalid queue pause setting.");
      if (body.retryTask !== undefined) {
        const original = device.tasks.find((task) => task.id === taskId(body.retryTask));
        if (!original || !["error", "canceled"].includes(original.status)) throw new DeviceError("Only failed or canceled downloads can be retried.", 409);
        if (device.tasks.some((task) => !TERMINAL.has(task.status) && task.system === original.system && task.romId === original.romId)) throw new DeviceError("This game is already waiting on the device.", 409);
        this.capacity(device, 1);
        const stamp = new Date(this.now()).toISOString();
        const retry = { ...original, id: randomUUID().replaceAll("-", ""), status: "queued" as const, received: 0, message: "Waiting for the DSi", addedAt: stamp, updatedAt: stamp };
        delete retry.finishedAt;
        device.tasks.push(retry);
        device.paused = false;
      }
      if (renamed !== undefined) device.name = renamed;
      if (typeof body.paused === "boolean") device.paused = body.paused;
      device.updatedAt = new Date(this.now()).toISOString();
      this.trim(device);
      await this.persist();
      return this.view();
    });
  }

  private capacity(device: StoredDevice, count: number): void {
    if (device.tasks.filter((task) => !TERMINAL.has(task.status)).length + count > DEVICE_QUEUE_LIMIT)
      throw new DeviceError(`A DSi queue can hold up to ${DEVICE_QUEUE_LIMIT} waiting or active games.`, 409);
  }

  enqueue(body: Record<string, unknown>): Promise<DeviceSnapshot> {
    return this.serial(async () => {
      await this.load();
      const device = this.find(body.mac);
      if (!Array.isArray(body.items) || !body.items.length || body.items.length > DEVICE_QUEUE_LIMIT) throw new DeviceError("Select 1–32 games to send to a DSi.");
      if (body.start !== undefined && typeof body.start !== "boolean") throw new DeviceError("Invalid queue start setting.");
      const inputs: DeviceQueueInput[] = body.items.map((item: unknown) => {
        if (!item || typeof item !== "object") throw new DeviceError("Invalid game selection.");
        const input = item as Record<string, unknown>, platform = system(input.system);
        return { system: platform, id: romId(input.id, platform) };
      });
      const existing = new Set(device.tasks.filter((task) => !TERMINAL.has(task.status)).map((task) => `${task.system}:${task.romId}`));
      const unique = inputs.filter((input) => {
        const key = `${input.system}:${input.id}`;
        if (existing.has(key)) return false;
        existing.add(key); return true;
      });
      this.capacity(device, unique.length);
      const prepared = await Promise.all(unique.map(async (input) => {
        try { return { input, rom: (await this.resolveGame(input.id, input.system)).rom }; }
        catch (error) {
          unstable_rethrow(error);
          throw new DeviceError(error instanceof Error ? error.message : "The selected game is unavailable.", 404);
        }
      }));
      if (prepared.some(({ rom }) => rom.media) && !supportsMultimedia(device.version))
        throw new DeviceError("Update this DSi to DSiDrop 1.17.0 or newer before sending multimedia files.", 409);
      if (prepared.some(({ rom }) => rom.folder) && !supportsFolderTransfer(device.version))
        throw new DeviceError("Update this DSi to DSiDrop 1.19.0 or newer before sending complete folders.", 409);
      const stamp = new Date(this.now()).toISOString();
      for (const { input, rom } of prepared) device.tasks.push({
        id: randomUUID().replaceAll("-", ""), system: input.system, romId: input.id, title: rom.t, region: rom.r,
        status: "queued", received: 0, size: rom.s, message: "Waiting for the DSi", addedAt: stamp, updatedAt: stamp,
      });
      if (body.start === true) device.paused = false;
      device.updatedAt = stamp;
      this.trim(device);
      await this.persist();
      return this.view();
    });
  }

  removeTasks(body: Record<string, unknown>): Promise<DeviceSnapshot> {
    return this.serial(async () => {
      await this.load();
      const device = this.find(body.mac);
      const selected = body.taskId === undefined ? undefined : taskId(body.taskId);
      if (selected && !device.tasks.some((task) => task.id === selected)) throw new DeviceError("The device task was not found.", 404);
      if (selected && device.tasks.some((task) => task.id === selected && task.status === "downloading")) throw new DeviceError("Cancel the active transfer on the DSi before removing it.", 409);
      device.tasks = device.tasks.filter((task) => task.status === "downloading" || (selected !== undefined && task.id !== selected));
      device.updatedAt = new Date(this.now()).toISOString();
      await this.persist();
      return this.view();
    });
  }

  forget(body: Record<string, unknown>): Promise<DeviceSnapshot> {
    return this.serial(async () => {
      await this.load();
      const device = this.find(body.mac);
      if (device.lastSeen !== null && this.now() - Date.parse(device.lastSeen) < DEVICE_OFFLINE_AFTER) throw new DeviceError("Close DSiDrop on the device before forgetting it.", 409);
      if (device.tasks.some((task) => task.status === "downloading")) throw new DeviceError("Reconnect the DSi and finish or cancel its active transfer first.", 409);
      this.devices = this.devices!.filter((entry) => entry !== device);
      await this.persist();
      return this.view();
    });
  }

  poll(input: Poll): Promise<string> {
    return this.serial(async () => {
      await this.load();
      const stamp = new Date(this.now()).toISOString();
      let device = this.devices!.find((entry) => entry.mac === input.mac);
      let changed = false;
      if (!device) {
        if (this.devices!.length >= MAX_DEVICES) throw new DeviceError("The PC already knows 64 DSi devices. Remove an unused device first.", 429);
        device = { mac: input.mac, name: `DSi ${input.mac.slice(-6).match(/../g)!.join(":").toUpperCase()}`, ip: input.ip, version: input.version,
          state: input.state, paused: false, createdAt: stamp, updatedAt: stamp, lastSeen: stamp, tasks: [] };
        this.devices!.push(device); changed = true;
      }
      if (device.ip !== input.ip || device.version !== input.version) changed = true;
      device.lastSeen = stamp;
      device.ip = input.ip;
      device.version = input.version;
      device.state = input.state;
      const reported = device.tasks.find((task) => task.id === input.task);
      if (reported?.status === "downloading") {
        reported.received = input.received;
        if (input.size) reported.size = input.size;
        if (input.message) reported.message = input.message;
        reported.updatedAt = stamp;
        if (TERMINAL.has(input.state)) {
          reported.status = input.state as "done" | "error" | "canceled";
          reported.message = input.message || (input.state === "done" ? "Saved to the DSi SD card" : input.state === "canceled" ? "Canceled on the DSi" : "Download failed on the DSi");
          reported.finishedAt = stamp;
          if (input.state === "done") reported.received = reported.size;
          else device.paused = true;
          changed = true;
        }
      }
      let assigned = device.tasks.find((task) => task.status === "downloading");
      const canReceive = input.state === "idle" || input.state === "done";
      if (!assigned && !device.paused && canReceive) {
        assigned = device.tasks.find((task) => task.status === "queued");
        if (assigned) {
          assigned.status = "downloading";
          assigned.message = "Assigned to the DSi";
          assigned.updatedAt = stamp;
          changed = true;
        }
      }
      device.updatedAt = stamp;
      this.trim(device);

      if (this.dirty || changed || this.now() - this.lastWritten >= 5000) await this.persist();
      if (!assigned || input.state === "busy" || input.state === "error" || input.state === "canceled")
        return `RFCLIENT1\t${DEVICE_POLL_INTERVAL}\t-\t-\t-\t-\t0\n`;
      return ["RFCLIENT1", DEVICE_POLL_INTERVAL, assigned.id, assigned.system, assigned.romId, ascii(assigned.title, 90) || "Game", assigned.size].join("\t") + "\n";
    });
  }
}

const shared = globalThis as typeof globalThis & { __dsiDeviceRegistries?: Map<string, DeviceRegistry> };
const registries = (shared.__dsiDeviceRegistries ??= new Map());
const file = path.join(ROMS_DIR, ".devices.json");
export function getDevices(): DeviceRegistry {
  let registry = registries.get(file);
  if (!registry) { registry = new DeviceRegistry(file); registries.set(file, registry); }
  return registry;
}
