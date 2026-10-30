import type { DeviceQueueInput } from "./device-types";
export type CollectionItem = DeviceQueueInput & { title: string; region: string; cover?: string | null };
export type GameCollection = { id: string; name: string; items: CollectionItem[]; updatedAt: string };