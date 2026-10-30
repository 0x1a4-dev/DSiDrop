import type { DeviceGame } from "./device-library-types";

export function supportsAutomaticRestore(version: string): boolean {
  const [major, minor] = version.split(".").map(Number);
  return Number.isInteger(major) && Number.isInteger(minor) && (major > 1 || major === 1 && minor >= 16);
}

export function matchingDeviceGame(source: DeviceGame, target: DeviceGame): boolean {
  if (!target.installed || source.system && target.system && source.system !== target.system) return false;
  if (source.romId || target.romId) return !!source.romId && source.romId === target.romId;
  return source.title.localeCompare(target.title, undefined, { sensitivity: "accent" }) === 0;
}

export function gameSaveTargets(game: DeviceGame): string[] {
  if (!game.path) return game.savePaths;
  const slash = game.path.lastIndexOf("/"), directory = game.path.slice(0, slash), filename = game.path.slice(slash + 1).replace(/\.(nds|gba)$/i, "");
  const candidates = [...game.savePaths, `${directory}/saves/${filename}.sav`, `${directory}/${filename}.sav`, `${game.path}.sav`];
  return candidates.filter((candidate, index) => candidates.findIndex((entry) => entry.toLowerCase() === candidate.toLowerCase()) === index);
}