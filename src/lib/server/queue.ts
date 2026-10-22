import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";

import {
  DownloadCanceledError,
  ROMS_DIR,
  fetchRom,
  type RomProgress,
} from "./rom-file";
import type { ArchiveKind, QueueItem, QueueSnapshot, QueueStatus, QueueInput as ClientQueueInput } from "../queue-types";

export type { ArchiveKind, QueueAction, QueueItem, QueueSnapshot, QueueStats, QueueStatus } from "../queue-types";
export type QueueInput = Omit<ClientQueueInput, "url"> & { url?: string };

const ARCHIVE_ID =
  process.env.ARCHIVE_ID ?? "pack-roms-nintendo-ds-eu-usa-jap-rabbits-games";
const ARCHIVE_BASE =
  process.env.ARCHIVE_BASE ?? `https://archive.org/download/${ARCHIVE_ID}`;

const STATE_FILE = path.join(ROMS_DIR, ".queue.json");

const ACTIVE: QueueStatus[] = ["downloading", "extracting"];

class QueueManager {
  private items = new Map<string, QueueItem>();
  private order: string[] = [];
  private running = false;
  private stopRequested = false;
  private loaded = false;
  private abort: AbortController | null = null;

  private async ensureLoaded(): Promise<void> {
    await mkdir(ROMS_DIR, { recursive: true });
    if (this.loaded) return;
    this.loaded = true;
    try {
      const raw = await readFile(STATE_FILE, "utf8");
      const parsed = JSON.parse(raw) as { items?: QueueItem[] };
      for (const item of parsed.items ?? []) {
        if (!item?.id) continue;
        item.action = "download";

        if (ACTIVE.includes(item.status)) {
          item.status = "pending";
          item.progress = 0;
          item.received = 0;
        }
        this.items.set(item.id, item);
        this.order.push(item.id);
      }
    } catch {}
  }

  private async persist(): Promise<void> {
    await mkdir(ROMS_DIR, { recursive: true });
    const items = this.order
      .map((id) => this.items.get(id))
      .filter((item): item is QueueItem => Boolean(item));
    await writeFile(STATE_FILE, JSON.stringify({ items }, null, 2), "utf8");
  }

  private list(): QueueItem[] {
    return this.order
      .map((id) => this.items.get(id))
      .filter((item): item is QueueItem => Boolean(item));
  }

  async snapshot(): Promise<QueueSnapshot> {
    await this.ensureLoaded();
    const items = this.list().map((item) => ({ ...item }));
    return {
      running: this.running,
      directory: ROMS_DIR,
      items,
      stats: {
        total: items.length,
        pending: items.filter(
          (item) =>
            item.status === "pending" ||
            item.status === "downloading" ||
            item.status === "extracting",
        ).length,
        done: items.filter((item) => item.status === "done").length,
        error: items.filter((item) => item.status === "error").length,
      },
    };
  }

  async add(input: QueueInput): Promise<void> {
    await this.ensureLoaded();
    const existing = this.items.get(input.id);
    if (existing) {
      if (input.action) existing.action = input.action;
      if (input.systemId) existing.systemId = input.systemId;

      if (existing.status === "error" || existing.status === "canceled") {
        existing.status = "pending";
        existing.progress = 0;
        existing.received = 0;
        existing.message = undefined;
      }
      await this.persist();
      return;
    }
    const item: QueueItem = {
      id: input.id,
      name: input.name,
      title: input.title?.trim() || input.name,
      region: input.region ?? "",
      system: input.system ?? "",
      systemId: input.systemId,
      size: input.size ?? 0,
      status: "pending",
      action: input.action ?? "download",
      progress: 0,
      received: 0,
      total: input.size ?? 0,
      url: input.url,
      arch: input.arch,
      ext: input.ext,
      out: input.out,
      addedAt: new Date().toISOString(),
    };
    this.items.set(item.id, item);
    this.order.push(item.id);
    await this.persist();
  }

  async remove(id: string): Promise<void> {
    await this.ensureLoaded();
    const item = this.items.get(id);
    if (!item) return;
    if (this.running && ACTIVE.includes(item.status)) return;
    this.items.delete(id);
    this.order = this.order.filter((entry) => entry !== id);
    await this.persist();
  }

  async clear(): Promise<void> {
    await this.ensureLoaded();
    const keep = this.order.filter((id) => {
      const status = this.items.get(id)?.status;
      return status ? ACTIVE.includes(status) : false;
    });
    for (const id of this.order) {
      if (!keep.includes(id)) this.items.delete(id);
    }
    this.order = keep;
    await this.persist();
  }

  async start(): Promise<void> {
    await this.ensureLoaded();
    if (this.running) return;
    const queue = this.order.filter((id) => {
      const status = this.items.get(id)?.status;
      return status === "pending" || status === "error" || status === "canceled";
    });
    if (queue.length === 0) return;
    this.running = true;
    this.stopRequested = false;
    void this.run(queue).finally(() => {
      this.running = false;
      this.abort = null;
      void this.persist().catch(() => {});
    });
  }

  async stop(): Promise<void> {
    this.stopRequested = true;
    this.abort?.abort();
  }

  private async run(ids: string[]): Promise<void> {
    await mkdir(ROMS_DIR, { recursive: true });
    for (const id of ids) {
      if (this.stopRequested) break;
      const item = this.items.get(id);
      if (!item) continue;
      try {
        await this.process(item);
      } catch (error) {
        if (this.stopRequested || error instanceof DownloadCanceledError) {
          item.status = "pending";
          item.progress = 0;
          item.received = 0;
        } else {
          item.status = "error";
          item.message =
            error instanceof Error ? error.message : String(error);
        }
      }
      await this.persist();
    }
  }

  private async process(item: QueueItem): Promise<void> {
    const url =
      item.url || `${ARCHIVE_BASE}/${encodeURIComponent(`${item.name}.zip`)}`;

    const arch: ArchiveKind | null =
      item.arch === undefined ? "zip" : item.arch;
    const controller = new AbortController();
    this.abort = controller;

    item.status = "downloading";
    item.progress = 0;
    item.received = 0;
    item.total = item.size || 0;
    item.message = undefined;
    item.file = undefined;

    const onProgress = (progress: RomProgress) => {
      item.received = progress.received;
      item.total = progress.total || item.size || 0;
      item.progress = progress.progress;
      if (progress.phase === "extracting") item.status = "extracting";
    };

    const saved = await fetchRom(
      {
        id: item.id,
        name: item.name,
        systemId: item.systemId,
        url,
        arch,
        ext: item.ext,
        out: item.out,
      },
      {
        signal: controller.signal,
        onProgress,
        isCanceled: () => this.stopRequested,
      },
    );

    const relative = path.relative(ROMS_DIR, saved).split(path.sep).join("/");
    item.file = relative;
    item.status = "done";
    item.progress = 1;
    item.finishedAt = new Date().toISOString();
    item.message = `Saved: ${relative}`;
  }
}

type GlobalWithQueue = typeof globalThis & {
  __romsFinderQueue?: QueueManager;
};

export function getQueue(): QueueManager {
  const globalRef = globalThis as GlobalWithQueue;
  globalRef.__romsFinderQueue ??= new QueueManager();
  return globalRef.__romsFinderQueue;
}
