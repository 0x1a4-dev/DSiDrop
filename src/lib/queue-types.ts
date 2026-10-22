export type QueueStatus =
  | "pending"
  | "downloading"
  | "extracting"
  | "done"
  | "error"
  | "canceled";

export type QueueAction = "download";

export type QueueItem = {
  id: string;
  name: string;
  title: string;
  region: string;
  system: string;
  systemId?: string;
  size: number;
  status: QueueStatus;
  action?: QueueAction;
  progress: number;
  received: number;
  total: number;
  url?: string;
  arch?: ArchiveKind | null;
  ext?: string;
  out?: string;
  message?: string;
  file?: string;
  addedAt: string;
  finishedAt?: string;
};

export type QueueStats = {
  total: number;
  pending: number;
  done: number;
  error: number;
};

export type QueueSnapshot = {
  running: boolean;
  directory: string;
  items: QueueItem[];
  stats: QueueStats;
};

export type ArchiveKind = "zip" | "7z";

export type QueueInput = {
  id: string;
  name: string;
  title?: string;
  region?: string;
  system?: string;
  systemId?: string;
  size?: number;
  url: string;
  arch?: ArchiveKind | null;
  ext?: string;
  out?: string;
  action?: QueueAction;
};
