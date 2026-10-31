import path from "node:path";
import { unstable_rethrow } from "next/navigation";
import { POST as changeSaves } from "../../saves/route";
import { gameSaveTargets, matchingDeviceGame, supportsAutomaticRestore } from "@/lib/device-library";
import { deviceLibrary, sdLibraryPath } from "@/lib/server/device-library";
import { DeviceError, getDevices, normalizeMac } from "@/lib/server/devices";
import { ascii, dsiText } from "@/lib/server/dsi";
import { saves, savePath } from "@/lib/server/saves";

const PAGE_SIZE = 6;
const text = (value: string, length: number) => ascii(value, length).replace(/[\t\r\n]/g, " ") || "-";
const reply = (lines: string[], status = 200) => dsiText(`${lines.join("\n")}\n`, status);
function failure(error: unknown) {
  unstable_rethrow(error);
  return reply([`RFSAVES1\terror\t${text(error instanceof DeviceError ? error.message : "Save service unavailable.", 160)}`], error instanceof DeviceError ? error.status : 500);
}
function decode(value: string | null): string {
  if (!value || !/^(?:[a-f0-9]{2}){1,220}$/.test(value)) throw new DeviceError("Invalid SD path encoding.");
  const decoded = Buffer.from(value, "hex").toString("utf8");
  if (Buffer.from(decoded).toString("hex") !== value) throw new DeviceError("Invalid SD path encoding.");
  return sdLibraryPath(decoded);
}
function pageNumber(value: string | null): number {
  if (value !== null && !/^\d{1,4}$/.test(value)) throw new DeviceError("Invalid backup page.");
  return Number(value || 0);
}
function localSaveTargets(rom: string): string[] {
  if (!/\.(nds|gba)$/i.test(rom)) throw new DeviceError("Choose an installed game.");
  const directory = path.posix.dirname(rom), filename = path.posix.basename(rom), stem = filename.replace(/\.(nds|gba)$/i, "");
  return [path.posix.join(directory, "saves", `${stem}.sav`), path.posix.join(directory, `${stem}.sav`), `${rom}.sav`, path.posix.join(directory, "saves", `${filename}.sav`)];
}

export async function GET(request: Request) {
  try {
    const params = new URL(request.url).searchParams, mac = normalizeMac(params.get("mac")), target = decode(params.get("path"));
    const page = pageNumber(params.get("page")), view = params.get("view");
    if (view === "game") {
      const game = (await deviceLibrary.snapshot(mac)).games.find((entry) => entry.path?.toLowerCase() === target.toLowerCase());
      if (!game) return reply(["RFSAVES1\tgame\t-\t-\t-"]);
      return reply([`RFSAVES1\tgame\t${game.system || "nds"}\t${game.romId || "-"}\t${text(game.title, 90)}`]);
    }
    const snapshot = await saves.snapshot(mac);
    if (view === "backups") {
      const candidates = localSaveTargets(target);
      const backups = snapshot.backups.filter((entry) => candidates.some((candidate) => candidate.toLowerCase() === entry.path.toLowerCase()))
        .sort((first, second) => second.createdAt.localeCompare(first.createdAt));
      const selected = backups.slice(page * PAGE_SIZE, (page + 1) * PAGE_SIZE);
      return reply([`RFSAVES1\tbackups\t${selected.length}\t${backups.length}\t${snapshot.automatic[mac] ? 1 : 0}`,
        ...selected.map((entry) => `${entry.id}\t${entry.createdAt.slice(0, 16).replace("T", " ")}\t${entry.size}\t${Buffer.from(entry.path).toString("hex")}`)]);
    }
    if (view !== "targets") throw new DeviceError("Choose a save view.");
    const backup = snapshot.backups.find((entry) => entry.id === params.get("backup") && entry.path.toLowerCase() === target.toLowerCase());
    if (!backup) throw new DeviceError("Backup not found.", 404);
    const source = (await deviceLibrary.snapshot(mac)).games.find((game) => gameSaveTargets(game).some((candidate) => candidate.toLowerCase() === target.toLowerCase()));
    if (!source) throw new DeviceError("Refresh the SD library before transferring this save.", 409);
    const devices = (await getDevices().snapshot()).devices.filter((device) => device.mac !== mac && device.online && supportsAutomaticRestore(device.version));
    const targets = (await Promise.all(devices.map(async (device) => {
      const games = (await deviceLibrary.snapshot(device.mac)).games.filter((game) => matchingDeviceGame(source, game));
      return games.map((game) => ({ mac: device.mac, game: game.id, name: device.name, path: gameSaveTargets(game)[0] }));
    }))).flat();
    const selected = targets.slice(page * PAGE_SIZE, (page + 1) * PAGE_SIZE);
    return reply([`RFSAVES1\ttargets\t${selected.length}\t${targets.length}`,
      ...selected.map((entry) => `${entry.mac}\t${entry.game}\t${text(entry.name, 40)}\t${Buffer.from(savePath(entry.path)).toString("hex")}`)]);
  } catch (error) { return failure(error); }
}

export async function POST(request: Request) {
  try {
    const params = new URL(request.url).searchParams, mac = normalizeMac(params.get("mac")), action = params.get("action");
    const body: Record<string, unknown> = { mac };
    if (action === "automatic") {
      if (!/^[01]$/.test(params.get("enabled") || "")) throw new DeviceError("Invalid automatic backup setting.");
      body.automatic = params.get("enabled") === "1";
    } else if (action === "backup") {
      body.action = action;
      if (params.has("path")) body.path = savePath(decode(params.get("path")));
    } else if (action === "restore" || action === "transfer" || action === "delete") {
      const target = savePath(decode(params.get("path")));
      const backup = (await saves.snapshot(mac)).backups.find((entry) => entry.id === params.get("backup") && entry.path.toLowerCase() === target.toLowerCase());
      if (!backup) throw new DeviceError("Backup not found for this save.", 404);
      if (action === "delete") body.delete = backup.id;
      else {
        body.action = action; body.backup = backup.id;
        if (action === "transfer") {
          body.targetMac = normalizeMac(params.get("targetMac")); body.targetGame = params.get("targetGame");
          body.game = (await deviceLibrary.snapshot(mac)).games.find((game) => game.savePaths.some((candidate) => candidate.toLowerCase() === target.toLowerCase()))?.id;
          body.path = savePath(decode(params.get("targetPath")));
        }
      }
    } else throw new DeviceError("Choose a save action.");
    const result = await changeSaves(new Request(new URL("/api/saves", request.url), {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
    }));
    if (!result.ok) {
      const error = await result.json();
      return reply([`RFSAVES1\terror\t${text(error.error || "Save request failed.", 160)}`], result.status);
    }
    return reply(["RFSAVES1\tok"]);
  } catch (error) { return failure(error); }
}