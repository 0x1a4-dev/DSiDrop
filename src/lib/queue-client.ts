"use client";

import type { QueueInput, QueueSnapshot } from "./queue-types";

export type * from "./queue-types";

const CHANGE_EVENT = "rom-queue-changed";
const OPEN_EVENT = "rom-queue-open";

function emitChange(): void {
  if (typeof window !== "undefined") {
    window.dispatchEvent(new Event(CHANGE_EVENT));
  }
}

export function onQueueChange(handler: () => void): () => void {
  if (typeof window === "undefined") return () => {};
  window.addEventListener(CHANGE_EVENT, handler);
  return () => window.removeEventListener(CHANGE_EVENT, handler);
}

export function openQueuePanel(): void {
  if (typeof window !== "undefined") {
    window.dispatchEvent(new Event(OPEN_EVENT));
  }
}

export function onQueueOpen(handler: () => void): () => void {
  if (typeof window === "undefined") return () => {};
  window.addEventListener(OPEN_EVENT, handler);
  return () => window.removeEventListener(OPEN_EVENT, handler);
}

async function request(url: string, init?: RequestInit): Promise<QueueSnapshot> {
  const response = await fetch(url, { ...init, cache: "no-store" });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return (await response.json()) as QueueSnapshot;
}

export function loadQueue(): Promise<QueueSnapshot> {
  return request("/api/queue");
}

export async function addToQueue(item: QueueInput): Promise<QueueSnapshot> {
  const snapshot = await request("/api/queue", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(item),
  });
  emitChange();
  return snapshot;
}

export async function removeFromQueue(id: string): Promise<QueueSnapshot> {
  const snapshot = await request(`/api/queue?id=${encodeURIComponent(id)}`, {
    method: "DELETE",
  });
  emitChange();
  return snapshot;
}

export async function clearQueue(): Promise<QueueSnapshot> {
  const snapshot = await request("/api/queue", { method: "DELETE" });
  emitChange();
  return snapshot;
}

export async function startQueue(): Promise<QueueSnapshot> {
  const snapshot = await request("/api/queue/start", { method: "POST" });
  emitChange();
  return snapshot;
}

export async function stopQueue(): Promise<QueueSnapshot> {
  const snapshot = await request("/api/queue/start", { method: "DELETE" });
  emitChange();
  return snapshot;
}
