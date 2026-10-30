"use client";

import type { DeviceQueueInput, DeviceSnapshot } from "./device-types";
import type { QueueItem } from "./queue-client";

export type DeviceSelection = { items: DeviceQueueInput[]; label: string; multimedia?: boolean; folder?: boolean };
const OPEN_EVENT = "dsidrop-devices-open";
const CHANGE_EVENT = "dsidrop-devices-changed";

export function openDevices(selection?: DeviceSelection): void {
  if (typeof window !== "undefined") window.dispatchEvent(new CustomEvent(OPEN_EVENT, { detail: selection }));
}

export function onDevicesOpen(handler: (selection?: DeviceSelection) => void): () => void {
  const listener = (event: Event) => handler((event as CustomEvent<DeviceSelection | undefined>).detail);
  window.addEventListener(OPEN_EVENT, listener);
  return () => window.removeEventListener(OPEN_EVENT, listener);
}

export function onDevicesChange(handler: () => void): () => void {
  window.addEventListener(CHANGE_EVENT, handler);
  return () => window.removeEventListener(CHANGE_EVENT, handler);
}

async function request(path: string, method = "GET", body?: unknown): Promise<DeviceSnapshot> {
  const response = await fetch(path, { method, cache: "no-store", headers: body ? { "Content-Type": "application/json" } : undefined, body: body ? JSON.stringify(body) : undefined });
  const result = await response.json().catch(() => null) as (DeviceSnapshot & { error?: string }) | null;
  if (!response.ok || !result) throw new Error(result?.error ?? `Device request failed (HTTP ${response.status}).`);
  if (method !== "GET") window.dispatchEvent(new Event(CHANGE_EVENT));
  return result;
}

export const loadDevices = () => request("/api/devices");
export const renameDevice = (mac: string, name: string) => request("/api/devices", "PATCH", { mac, name });
export const pauseDevice = (mac: string, paused: boolean) => request("/api/devices", "PATCH", { mac, paused });
export const retryDeviceTask = (mac: string, retryTask: string) => request("/api/devices", "PATCH", { mac, retryTask });
export const forgetDevice = (mac: string) => request("/api/devices", "DELETE", { mac });
export const sendDeviceQueue = (mac: string, items: DeviceQueueInput[]) => request("/api/devices/queue", "POST", { mac, items, start: true });
export const removeDeviceTask = (mac: string, taskId: string) => request("/api/devices/queue", "DELETE", { mac, taskId });

export function deviceQueueItems(items: QueueItem[]): DeviceQueueInput[] {
  const result: DeviceQueueInput[] = [];
  const seen = new Set<string>();
  for (const item of items) {
    const system = (item.systemId ?? item.system).toLowerCase();
    if (system !== "nds" && system !== "gba" && system !== "others") continue;
    if (!(system === "gba" ? /^[a-f0-9]{8}$/i : /^[a-f0-9]{32}$/i).test(item.id)) continue;
    const id = item.id.toLowerCase();
    if (seen.has(`${system}:${id}`)) continue;
    seen.add(`${system}:${id}`);
    result.push({ system, id });
  }
  return result;
}

export function formatMac(mac: string): string { return mac.toUpperCase().match(/.{1,2}/g)?.join(":") ?? mac; }
