export type DeviceSystem = "nds" | "gba" | "others";
export type DeviceClientState = "idle" | "busy" | "downloading" | "done" | "error" | "canceled";
export type DeviceTaskStatus = "queued" | "downloading" | "done" | "error" | "canceled";

export type DeviceQueueInput = { system: DeviceSystem; id: string };
export type DeviceTask = {
  id: string;
  system: DeviceSystem;
  romId: string;
  title: string;
  region: string;
  status: DeviceTaskStatus;
  received: number;
  size: number;
  message: string;
  addedAt: string;
  updatedAt: string;
  finishedAt?: string;
};

export type DsiDevice = {
  mac: string;
  name: string;
  ip: string;
  version: string;
  createdAt: string;
  updatedAt: string;
  lastSeen: string | null;
  online: boolean;
  paused: boolean;
  state: DeviceClientState;
  tasks: DeviceTask[];
};

export type DeviceSnapshot = {
  devices: DsiDevice[];
  limit: number;
  pollInterval: number;
  offlineAfter: number;
};
