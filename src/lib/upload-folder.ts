import { mediaInfo } from "./media";

export const MAX_FOLDER_FILES = 2048;
export const MAX_FOLDER_SIZE = 512 * 1024 * 1024;

export type UploadFolder = {
  destination: string;
  category: "Uploaded" | "Multimedia";
  files: { name: string; size: number; offset: number }[];
};

export function supportsFolderTransfer(version: string): boolean {
  const match = /^(\d+)\.(\d+)(?:\.\d+)?$/.exec(version);
  return Boolean(match && (Number(match[1]) > 1 || Number(match[1]) === 1 && Number(match[2]) >= 19));
}

export function folderPath(value: string): string {
  const name = value.normalize("NFC");
  if (!name || new TextEncoder().encode(name).length > 200 || /[\x00-\x1f\x7f\\:<>"|?*]/.test(name)
    || name.split("/").length > 12 || name.split("/").some((part) => !part || part === "." || part === ".." || /[. ]$/.test(part)))
    throw new Error("Folder paths must be relative, FAT-safe and at most 12 levels deep.");
  return name;
}

export function folderDestination(paths: string[], multimedia: boolean) {
  if (!paths.length || paths.length > MAX_FOLDER_FILES) throw new Error(`Choose a folder containing 1-${MAX_FOLDER_FILES} files.`);
  const names = paths.map(folderPath);
  const root = names[0].split("/")[0];
  if (names.some((name) => !name.startsWith(`${root}/`))) throw new Error("Import one complete folder at a time.");
  const kinds = new Set(names.flatMap((name) => mediaInfo(name)?.directory ?? []));
  const parent = multimedia ? kinds.size === 1 ? [...kinds][0] : "/multimedia" : "/roms/others";
  if (names.some((name) => new TextEncoder().encode(`${parent}/${name}`).length > 220)) throw new Error("A folder path is too long for the DSi SD card.");
  return { name: root, destination: `${parent}/${root}` };
}