import type { DeviceSystem } from "./device-types";

export type DeviceFile = { path: string; size: number; modified: number; header: string };
export type DeviceGame = { id: string; path: string | null; title: string; cover: string | null; system: DeviceSystem | null; romId: string | null; size: number; installed: boolean; savePaths: string[] };
export type DeviceLibraryTask = { id: string; action: "scan" | "delete"; status: "queued" | "running" | "done" | "error"; file?: DeviceFile; message: string; createdAt: string };
export type DeviceStorage = { total: number; free: number; updatedAt: string };
export type DeviceLibrarySnapshot = { games: DeviceGame[]; scannedAt: string | null; task: DeviceLibraryTask | null; storage: DeviceStorage | null };