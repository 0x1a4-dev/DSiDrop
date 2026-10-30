"use client";

import { useEffect, useRef, useState } from "react";
import Icon from "./Icon";
import { formatBytes } from "@/lib/roms";
import type { Dataset, Rom } from "@/lib/types";

export default function DeleteUploadDialog({ rom, onClose, onDeleted }: {
  rom: Rom;
  onClose: () => void;
  onDeleted: (dataset: Dataset) => void;
}) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const pending = useRef(false);
  const panel = useRef<HTMLDivElement>(null);
  const cancel = useRef<HTMLButtonElement>(null);
  const noun = rom.folder ? "folder" : rom.media ? "file" : "game";

  useEffect(() => {
    const previousFocus = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const overflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    cancel.current?.focus();
    const key = (event: KeyboardEvent) => {
      if (event.key === "Escape") { event.preventDefault(); if (!pending.current) onClose(); }
      if (event.key !== "Tab") return;
      const buttons = [...panel.current?.querySelectorAll<HTMLButtonElement>("button:not(:disabled)") ?? []];
      const first = buttons[0], last = buttons[buttons.length - 1];
      if (!first || !last) { event.preventDefault(); return; }
      if (event.shiftKey && (document.activeElement === first || !panel.current?.contains(document.activeElement))) { event.preventDefault(); last.focus(); }
      else if (!event.shiftKey && (document.activeElement === last || !panel.current?.contains(document.activeElement))) { event.preventDefault(); first.focus(); }
    };
    document.addEventListener("keydown", key);
    return () => {
      document.removeEventListener("keydown", key);
      document.body.style.overflow = overflow;
      if (previousFocus?.isConnected) previousFocus.focus();
    };
  }, [onClose]);

  const remove = async () => {
    if (pending.current) return;
    pending.current = true; setBusy(true); setError(null);
    try {
      const response = await fetch(`/api/library/others?id=${encodeURIComponent(rom.m)}`, { method: "DELETE" });
      const result = await response.json() as { dataset?: Dataset; error?: string };
      if (!response.ok || !result.dataset) {
        if (response.status === 404) {
          const latest = await fetch("/api/library/others", { cache: "no-store" });
          if (latest.ok) {
            const dataset = await latest.json() as Dataset;
            if (!dataset.roms.some((game) => game.m === rom.m)) { onDeleted(dataset); return; }
          }
        }
        throw new Error(result.error ?? `The ${noun} could not be deleted (HTTP ${response.status}).`);
      }
      onDeleted(result.dataset);
    } catch (err) { setError(err instanceof Error ? err.message : "The file could not be deleted. Please try again."); }
    finally { pending.current = false; setBusy(false); }
  };

  return <div role="presentation" className="fixed inset-0 z-[70] flex items-center justify-center bg-black/80 p-4 backdrop-blur-md" onClick={() => { if (!pending.current) onClose(); }}>
    <div ref={panel} role="dialog" aria-modal="true" aria-busy={busy} aria-labelledby="delete-upload-title" aria-describedby="delete-upload-description" onClick={(event) => event.stopPropagation()} className="animate-pop max-h-[calc(100dvh-2rem)] w-full max-w-md overflow-y-auto rounded-3xl border border-white/10 bg-ink-900 shadow-2xl">
      <div className="p-6 sm:p-8">
        <span className="grid size-12 place-items-center rounded-2xl border border-rose-400/20 bg-rose-500/10 text-rose-300"><Icon name="trash" className="size-5" /></span>
        <p className="eyebrow mt-6">YOUR UPLOAD / OTHERS</p>
        <h2 id="delete-upload-title" className="mt-2 text-2xl font-semibold tracking-tight text-white">Delete this {noun}?</h2>
        <div className="mt-5 rounded-xl border border-white/10 bg-white/[0.025] p-4"><p className="break-words text-sm font-medium leading-relaxed text-white/85">{rom.t}</p><p className="mt-1 text-[11px] text-white/40">{rom.folder ? `${rom.folder.count} files` : rom.media ? rom.e?.toUpperCase() : "Nintendo DS"} · {formatBytes(rom.s)}</p></div>
        <p id="delete-upload-description" className="mt-5 text-xs leading-relaxed text-white/50">This permanently removes the uploaded {rom.folder ? "folder and its contents" : "file"} from this PC and your Others library. Pending downloads will no longer work. Copies already saved on your DSi are kept.</p>
        {error ? <p role="alert" className="mt-4 rounded-xl border border-rose-400/20 bg-rose-500/10 p-3 text-xs leading-relaxed text-rose-200">{error}</p> : null}
      </div>
      <div className="grid grid-cols-2 gap-3 border-t border-white/[0.07] bg-black/15 p-5 sm:px-8">
        <button ref={cancel} type="button" disabled={busy} onClick={onClose} className="rounded-xl border border-white/10 px-3 py-3 text-xs font-medium text-white/70 transition hover:bg-white/5 disabled:opacity-40">Keep {noun}</button>
        <button type="button" disabled={busy} onClick={() => void remove()} className="inline-flex items-center justify-center gap-2 rounded-xl border border-rose-300/20 bg-rose-500 px-3 py-3 text-xs font-semibold text-white transition hover:bg-rose-400 disabled:opacity-50"><Icon name="trash" className="size-4" />{busy ? "Deleting…" : `Delete ${noun}`}</button>
      </div>
    </div>
  </div>;
}
