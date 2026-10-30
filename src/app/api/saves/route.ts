import { deviceBody, DeviceError, deviceError, getDevices, normalizeMac } from "@/lib/server/devices";
import { saves, savePath } from "@/lib/server/saves";
import { deviceLibrary } from "@/lib/server/device-library";
import { gameSaveTargets, matchingDeviceGame, supportsAutomaticRestore } from "@/lib/device-library";
export async function GET(request: Request) {
  try { return Response.json(await saves.snapshot(new URL(request.url).searchParams.get("mac")), { headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return deviceError(error); }
}
export async function POST(request: Request) {
  try {
    const body = await deviceBody(request);
    if (body.action === "restore") {
      const mac = normalizeMac(body.mac), destination = (await getDevices().snapshot()).devices.find((device) => device.mac === mac);
      if (!destination || !supportsAutomaticRestore(destination.version)) throw new DeviceError("Update the destination DSi to DSiDrop 1.16.0 for automatic restore.");
    }
    if (body.action !== "transfer") return Response.json(await saves.change(body), { headers: { "Cache-Control": "no-store" } });
    const sourceMac = normalizeMac(body.mac), targetMac = normalizeMac(body.targetMac);
    if (sourceMac === targetMac) throw new DeviceError("Choose a different destination DSi.");
    const [devices, source, target, backups] = await Promise.all([getDevices().snapshot(), deviceLibrary.snapshot(sourceMac), deviceLibrary.snapshot(targetMac), saves.snapshot(sourceMac)]);
    const destination = devices.devices.find((device) => device.mac === targetMac);
    if (!destination || !supportsAutomaticRestore(destination.version)) throw new DeviceError("Update the destination DSi to DSiDrop 1.16.0 for automatic transfer.");
    const sourceGame = source.games.find((game) => game.id === body.game), targetGame = target.games.find((game) => game.id === body.targetGame);
    if (!sourceGame || !targetGame || !matchingDeviceGame(sourceGame, targetGame)) throw new DeviceError("Choose the matching installed game on the destination DSi.");
    const backup = backups.backups.find((entry) => entry.id === body.backup && sourceGame.savePaths.includes(entry.path));
    if (!backup) throw new DeviceError("Choose a backup belonging to this game.", 404);
    const targetPath = savePath(body.path);
    if (!gameSaveTargets(targetGame).includes(targetPath)) throw new DeviceError("Choose a save path belonging to the destination game.");
    return Response.json(await saves.change({ mac: targetMac, action: "transfer", sourceMac, backup: backup.id, path: targetPath }), { headers: { "Cache-Control": "no-store" } });
  }
  catch (error) { return deviceError(error); }
}