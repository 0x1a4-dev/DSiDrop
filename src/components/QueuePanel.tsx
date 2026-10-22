"use client";

import { useCallback, useEffect, useRef, useState } from "react";

import Icon from "@/components/Icon";
import {
  clearQueue, loadQueue, onQueueChange, onQueueOpen, removeFromQueue,
  startQueue, stopQueue, type QueueItem, type QueueSnapshot, type QueueStatus,
} from "@/lib/queue-client";
import { formatBytes } from "@/lib/roms";
import { deviceQueueItems, onDevicesOpen, openDevices } from "@/lib/device-client";

const STATUS_LABEL: Record<QueueStatus, string> = {
  pending: "Waiting", downloading: "Downloading", extracting: "Extracting",
  done: "Complete", error: "Failed", canceled: "Canceled",
};
const STATUS_TONE: Record<QueueStatus, string> = {
  pending: "bg-white/5 text-white/50",
  downloading: "bg-brand-500/15 text-brand-300",
  extracting: "bg-brand-500/15 text-brand-300",
  done: "bg-emerald-400/10 text-emerald-300",
  error: "bg-rose-400/10 text-rose-300",
  canceled: "bg-white/5 text-white/40",
};

export default function QueuePanel() {
  const [snapshot, setSnapshot] = useState<QueueSnapshot | null>(null);
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const panelRef = useRef<HTMLElement>(null);
  const closeRef = useRef<HTMLButtonElement>(null);

  const refresh = useCallback(async () => {
    try { setSnapshot(await loadQueue()); } catch {}
  }, []);

  useEffect(() => {
    const unsubscribe = onQueueChange(() => void refresh());
    const unsubscribeOpen = onQueueOpen(() => setOpen(true));
    const unsubscribeDevices = onDevicesOpen(() => setOpen(false));
    const initial = window.setTimeout(() => void refresh(), 0);
    return () => { unsubscribe(); unsubscribeOpen(); unsubscribeDevices(); window.clearTimeout(initial); };
  }, [refresh]);

  const running = snapshot?.running ?? false;
  useEffect(() => {
    const interval = window.setInterval(() => void refresh(), running ? 900 : 2500);
    return () => window.clearInterval(interval);
  }, [refresh, running]);

  useEffect(() => {
    if (!open) return;
    const previousFocus = document.activeElement as HTMLElement | null;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    closeRef.current?.focus();
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") setOpen(false);
      if (event.key !== "Tab") return;
      const elements = panelRef.current?.querySelectorAll<HTMLElement>("button:not(:disabled), a[href], input:not(:disabled), [tabindex='0']");
      if (!elements?.length) return;
      const first = elements[0];
      const last = elements[elements.length - 1];
      if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
      else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
    };
    document.addEventListener("keydown", onKey);
    return () => {
      document.body.style.overflow = previousOverflow;
      document.removeEventListener("keydown", onKey);
      previousFocus?.focus();
    };
  }, [open]);

  const run = async (action: () => Promise<QueueSnapshot>) => {
    setBusy(true); setError(null);
    try { setSnapshot(await action()); }
    catch (err) { setError(err instanceof Error ? err.message : "Something went wrong. Please try again."); }
    finally { setBusy(false); }
  };

  if (!open) return null;
  const items = snapshot?.items ?? [];
  const pendingCount = snapshot?.stats.pending ?? 0;
  const dsiItems = deviceQueueItems(items);

  return (
    <div className="fixed inset-0 z-50 flex justify-end bg-black/75 backdrop-blur-sm" onClick={() => setOpen(false)} role="presentation">
      <aside ref={panelRef} role="dialog" aria-modal="true" aria-labelledby="queue-heading" onClick={(event) => event.stopPropagation()}
        className="animate-pop flex h-full w-full max-w-lg flex-col border-l border-white/10 bg-ink-900 shadow-2xl shadow-black/60">
        <header className="border-b border-white/[0.07] px-6 pb-6 pt-7 sm:px-8">
          <div className="flex items-start justify-between gap-4">
            <div>
              <p className="mb-2 text-[10px] font-semibold uppercase tracking-[0.2em] text-brand-300">Your next collection</p>
              <h2 id="queue-heading" className="text-2xl font-semibold tracking-tight text-white">Download queue<span className="ml-2 text-base font-normal text-white/30">{items.length}</span></h2>
            </div>
            <button ref={closeRef} type="button" onClick={() => setOpen(false)} aria-label="Close download queue" className="grid size-9 shrink-0 place-items-center rounded-full border border-white/10 text-white/55 transition hover:bg-white/10 hover:text-white">
              <Icon name="close" className="size-4" />
            </button>
          </div>
          <div className="mt-5 grid grid-cols-3 gap-2 text-center">
            <QueueStat value={pendingCount} label="Waiting" />
            <QueueStat value={snapshot?.stats.done ?? 0} label="Complete" />
            <QueueStat value={snapshot?.stats.error ?? 0} label="Failed" />
          </div>
          <div className="mt-4 flex items-center gap-2 text-xs text-white/45"><span className={`size-1.5 rounded-full ${running ? "animate-pulse bg-brand-400" : "bg-white/25"}`} />{running ? "Downloading one game at a time" : "Ready when you are"}</div>
        </header>

        {error ? <p role="alert" className="border-b border-rose-500/20 bg-rose-500/10 px-6 py-3 text-sm text-rose-200">{error}</p> : null}

        <div className="min-h-0 flex-1 overflow-y-auto px-4 py-4 sm:px-6">
          {!snapshot ? <p className="py-16 text-center text-sm text-white/45">Loading your queue…</p> : items.length === 0 ? (
            <div className="flex h-full min-h-64 flex-col items-center justify-center px-5 pb-10 text-center">
              <div className="mb-5 grid size-16 place-items-center rounded-2xl border border-brand-400/20 bg-brand-500/10 text-brand-300"><Icon name="queue" className="size-7" /></div>
              <h3 className="text-lg font-semibold tracking-tight text-white">Make room for your next favorite.</h3>
              <p className="mt-2 max-w-xs text-sm leading-relaxed text-white/45">Add games from the catalog, then start the queue to download them in order.</p>
              <button type="button" onClick={() => setOpen(false)} className="mt-6 rounded-xl border border-white/10 px-5 py-2.5 text-sm font-medium text-white/80 transition hover:border-brand-400/40 hover:bg-brand-500/10">Explore the catalog</button>
            </div>
          ) : <ol className="space-y-3">{items.map((item, index) => <QueueRow key={item.id} item={item} position={index + 1} disabled={busy} onRemove={() => void run(() => removeFromQueue(item.id))} />)}</ol>}
        </div>

        <footer className="border-t border-white/[0.07] bg-black/15 px-6 py-5 sm:px-8">
          {dsiItems.length ? <div className="mb-5 rounded-xl border border-brand-400/20 bg-brand-500/[0.06] p-3.5"><div className="flex items-center gap-3"><Icon name="device" className="size-5 shrink-0 text-brand-300" /><div className="min-w-0 flex-1"><p className="text-xs font-semibold text-white/80">Take this queue to your DSi</p><p className="mt-1 text-[10px] leading-relaxed text-white/40">{dsiItems.length} compatible {dsiItems.length === 1 ? "game" : "games"}{items.length > dsiItems.length ? ` · ${items.length - dsiItems.length} unsupported or duplicate entries excluded` : ""}. Your PC queue stays unchanged.</p></div></div><button type="button" onClick={() => openDevices({ items: dsiItems, label: `${dsiItems.length} ${dsiItems.length === 1 ? "game" : "games"} from your web download queue` })} className="mt-3 inline-flex w-full items-center justify-center gap-2 rounded-lg border border-brand-400/25 bg-brand-500/10 py-2.5 text-xs font-semibold text-brand-300 hover:bg-brand-500/20"><Icon name="arrow" className="size-3.5" />Send queue to DSi</button></div> : null}
          {snapshot ? <div className="mb-4"><p className="text-[10px] font-semibold uppercase tracking-[0.15em] text-white/35">Download location</p><p className="mt-1 truncate font-mono text-[11px] text-white/50" title={snapshot.directory}>{snapshot.directory}</p><p className="mt-1 text-[11px] text-white/30">Games are organized into folders by system.</p></div> : null}
          <div className="flex gap-3">
            <button type="button" disabled={busy || (!running && pendingCount === 0)} onClick={() => void run(running ? stopQueue : startQueue)}
              className={`flex flex-1 items-center justify-center gap-2 rounded-xl px-4 py-3 text-sm font-semibold transition disabled:cursor-not-allowed disabled:opacity-35 ${running ? "border border-rose-400/25 bg-rose-500/10 text-rose-200 hover:bg-rose-500/20" : "bg-brand-500 text-white shadow-lg shadow-brand-500/15 hover:bg-brand-400"}`}>
              {running ? <span aria-hidden className="size-2.5 rounded-sm bg-current" /> : <Icon name="download" className="size-4" />}{busy ? "Please wait…" : running ? "Stop downloads" : "Start downloads"}
            </button>
            <button type="button" disabled={busy || items.length === 0} onClick={() => void run(clearQueue)} className="rounded-xl border border-white/10 px-4 py-3 text-sm font-medium text-white/60 transition hover:bg-white/5 hover:text-white disabled:cursor-not-allowed disabled:opacity-35">Clear</button>
          </div>
        </footer>
      </aside>
    </div>
  );
}

function QueueStat({ value, label }: { value: number; label: string }) {
  return <div className="rounded-xl border border-white/[0.05] bg-white/[0.025] py-3"><p className="text-xl font-semibold tracking-tight text-white">{value}</p><p className="mt-0.5 text-[10px] font-medium text-white/35">{label}</p></div>;
}

function QueueRow({ item, position, disabled, onRemove }: { item: QueueItem; position: number; disabled: boolean; onRemove: () => void }) {
  const active = item.status === "downloading" || item.status === "extracting";
  const percent = Math.max(0, Math.min(100, Math.round(item.progress * 100)));
  return (
    <li className={`rounded-2xl border p-4 ${active ? "border-brand-400/25 bg-brand-500/[0.06]" : "border-white/[0.07] bg-white/[0.025]"}`}>
      <div className="flex items-start gap-3">
        <span className={`mt-0.5 grid size-8 shrink-0 place-items-center rounded-lg text-[11px] font-semibold ${item.status === "done" ? "bg-emerald-400/10 text-emerald-300" : "bg-white/5 text-white/35"}`}>{item.status === "done" ? <Icon name="check" className="size-4" /> : String(position).padStart(2, "0")}</span>
        <div className="min-w-0 flex-1">
          <p className="line-clamp-2 text-sm font-medium leading-snug text-white/90">{item.title}</p>
          <p className="mt-1.5 text-[11px] text-white/40">{[item.system, item.region, formatBytes(item.size)].filter(Boolean).join(" · ")}</p>
          <div className="mt-3 flex items-center justify-between gap-2"><span className={`rounded-md px-2 py-1 text-[10px] font-medium ${STATUS_TONE[item.status]}`}>{STATUS_LABEL[item.status]}</span>{active ? <span className="font-mono text-[11px] text-brand-300">{percent}%</span> : null}</div>
          {active || item.status === "done" ? <div role="progressbar" aria-label={`${item.title} download progress`} aria-valuemin={0} aria-valuemax={100} aria-valuenow={item.status === "done" ? 100 : percent} className="mt-2.5 h-1 w-full overflow-hidden rounded-full bg-white/10"><div className={`h-full rounded-full transition-all ${item.status === "done" ? "bg-emerald-400" : "bg-brand-400"}`} style={{ width: `${item.status === "done" ? 100 : percent}%` }} /></div> : null}
          {item.status === "downloading" && item.total > 0 ? <p className="mt-2 font-mono text-[10px] text-white/35">{formatBytes(item.received)} / {formatBytes(item.total)}</p> : null}
          {item.status === "error" && item.message ? <p className="mt-2 text-xs leading-relaxed text-rose-300/90">{item.message}</p> : null}
          {item.status === "done" && (item.file || item.message) ? <p className="mt-2 truncate text-[10px] text-emerald-300/60" title={item.file}>{item.file ?? item.message}</p> : null}
        </div>
        <button type="button" onClick={onRemove} disabled={disabled || active} aria-label={`Remove ${item.title} from queue`} className="grid size-7 shrink-0 place-items-center rounded-lg text-white/30 transition hover:bg-white/10 hover:text-white disabled:cursor-not-allowed disabled:opacity-20"><Icon name="close" className="size-3.5" /></button>
      </div>
    </li>
  );
}
