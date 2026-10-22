"use client";

import { useEffect, useRef, useState } from "react";

import AddToQueueButton from "./AddToQueueButton";
import CoverImage from "./CoverImage";
import Icon from "./Icon";
import SendToDeviceButton from "./SendToDeviceButton";
import { categoryLabel, checksumLabel, downloadUrl, formatBytes, formatLanguages, romCategory, romFileName } from "@/lib/roms";
import type { DatasetMeta, Rom } from "@/lib/types";

function CopyRow({ label, value }: { label: string; value: string }) {
  const [state, setState] = useState<"idle" | "copied" | "error">("idle");
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  useEffect(() => () => { if (timer.current) clearTimeout(timer.current); }, []);
  if (!value) return null;
  return (
    <div className="flex items-center justify-between gap-3 rounded-xl border border-white/5 bg-black/15 px-3.5 py-3">
      <div className="min-w-0">
        <p className="mb-1 text-[9px] font-semibold uppercase tracking-[0.12em] text-white/35">{label}</p>
        <p title={value} className="truncate font-mono text-[11px] text-white/65">{value}</p>
      </div>
      <button
        type="button"
        aria-label={`Copy ${label}`}
        onClick={async () => {
          try {
            await navigator.clipboard.writeText(value);
            setState("copied");
          } catch {
            setState("error");
          }
          if (timer.current) clearTimeout(timer.current);
          timer.current = setTimeout(() => setState("idle"), 1600);
        }}
        className="flex shrink-0 items-center gap-1.5 rounded-lg px-2 py-2 text-[10px] font-medium text-white/50 transition hover:bg-white/5 hover:text-white"
      >
        <Icon name={state === "copied" ? "check" : "copy"} className="h-3.5 w-3.5" />
        <span aria-live="polite">{state === "copied" ? "Copied" : state === "error" ? "Copy failed" : "Copy"}</span>
      </button>
    </div>
  );
}

function Chip({ children, accent = false }: { children: React.ReactNode; accent?: boolean }) {
  return <span className={`rounded-md border px-2 py-1 text-[10px] font-medium ${accent ? "border-brand-400/20 bg-brand-500/10 text-brand-400" : "border-white/10 bg-white/[0.03] text-white/55"}`}>{children}</span>;
}

export default function RomDetail({
  meta,
  rom,
  siblings,
  onClose,
  onSelect,
  onDelete,
}: {
  meta: DatasetMeta;
  rom: Rom;
  siblings: Rom[];
  onClose: () => void;
  onSelect: (rom: Rom) => void;
  onDelete?: () => void;
}) {
  const panel = useRef<HTMLDivElement>(null);
  const closeButton = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    const previousFocus = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    closeButton.current?.focus();
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        event.preventDefault();
        onClose();
      }
      if (event.key !== "Tab" || !panel.current) return;
      const controls = [...panel.current.querySelectorAll<HTMLElement>('a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex="0"]')];
      const first = controls[0];
      const last = controls[controls.length - 1];
      if (!first || !last) return;
      if (event.shiftKey && (document.activeElement === first || !panel.current.contains(document.activeElement))) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && (document.activeElement === last || !panel.current.contains(document.activeElement))) {
        event.preventDefault();
        first.focus();
      }
    };
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("keydown", onKey);
      document.body.style.overflow = previousOverflow;
      previousFocus?.focus();
    };
  }, [onClose]);

  const category = romCategory(rom);
  const languages = formatLanguages(rom.l);
  const fileName = romFileName(meta, rom);
  const downloadExt = rom.e ?? meta.ext;

  return (
    <div role="presentation" className="fixed inset-0 z-50 flex items-center justify-center bg-black/80 p-3 backdrop-blur-md sm:p-6" onClick={onClose}>
      <div
        ref={panel}
        role="dialog"
        aria-modal="true"
        aria-labelledby="rom-detail-title"
        onClick={(event) => event.stopPropagation()}
        className="animate-pop relative max-h-[calc(100dvh-1.5rem)] w-full max-w-4xl overflow-y-auto rounded-[24px] border border-white/10 bg-ink-900 shadow-[0_30px_120px_rgba(0,0,0,0.6)] sm:max-h-[calc(100dvh-3rem)]"
      >
        <button ref={closeButton} type="button" onClick={onClose} aria-label="Close game details" className="absolute right-4 top-4 z-10 grid h-9 w-9 place-items-center rounded-full border border-white/10 bg-black/50 text-white/60 backdrop-blur transition hover:bg-white/10 hover:text-white">
          <Icon name="close" className="h-4 w-4" />
        </button>

        <div className="grid gap-6 p-5 sm:grid-cols-[240px_1fr] sm:gap-7 sm:p-7">
          <div className="min-w-0">
            <CoverImage meta={meta} rom={rom} className="mx-auto aspect-square w-48 rounded-2xl border border-white/[0.08] bg-ink-850 sm:mt-1 sm:w-full" sizes="240px" />
            <div className="mt-4 hidden rounded-xl border border-white/5 bg-white/[0.02] p-3.5 sm:block">
              <p className="text-[9px] font-semibold uppercase tracking-[0.15em] text-white/35">Platform</p>
              <p className="mt-1 text-xs font-medium text-white/80">{meta.id === "others" ? "Nintendo DS · Your upload" : meta.label}</p>
            </div>
          </div>

          <div className="flex min-w-0 flex-col gap-5">
            <div>
              <div className="mb-3 flex flex-wrap gap-1.5 pr-8">
                <Chip accent>{meta.short}</Chip>
                <Chip>{rom.r}</Chip>
                <Chip>{meta.id === "others" ? "Your upload" : categoryLabel(category)}</Chip>
              </div>
              <h2 id="rom-detail-title" className="pr-4 text-2xl font-semibold leading-tight tracking-tight text-white sm:text-[28px]">{rom.t}</h2>
              {(rom.g ?? []).length > 0 ? <p className="mt-2 text-xs text-white/40">{rom.g?.join(" · ")}</p> : null}
            </div>

            <dl className="grid grid-cols-2 gap-3 border-y border-white/[0.06] py-4">
              <div>
                <dt className="mb-1 text-[10px] font-medium uppercase tracking-wider text-white/35">Download size</dt>
                <dd className="text-sm font-medium text-white/80">{formatBytes(rom.s)}</dd>
              </div>
              <div>
                <dt className="mb-1 text-[10px] font-medium uppercase tracking-wider text-white/35">Languages</dt>
                <dd className="text-xs leading-relaxed text-white/80">{languages || "Not specified"}</dd>
              </div>
            </dl>

            <div className="space-y-2">
              <div className="mb-3">
                <p className="mb-1 text-[10px] font-medium uppercase tracking-wider text-white/35">File</p>
                <p className="break-all font-mono text-[10px] leading-relaxed text-white/55">{fileName}</p>
                {meta.arch ? <p className="mt-1 text-[11px] text-white/35">Extracts to .{meta.out}</p> : null}
              </div>
              <CopyRow key={`checksum-${rom.m}`} label={checksumLabel(meta)} value={rom.m} />
              {rom.h ? <CopyRow key={`sha1-${rom.h}`} label="SHA1" value={rom.h} /> : null}
            </div>

            <div className="space-y-2">
              <SendToDeviceButton meta={meta} rom={rom} full />
              <div className="flex flex-col gap-2 sm:flex-row">
                <a href={downloadUrl(meta, rom)} target="_blank" rel="noopener noreferrer" className="inline-flex flex-1 items-center justify-center gap-2 rounded-xl bg-brand-500 px-4 py-3 text-xs font-semibold text-white shadow-[0_4px_20px_-6px_rgba(139,61,255,0.65)] transition hover:bg-brand-400">
                  <Icon name="download" className="h-4 w-4" />Download .{downloadExt}
                </a>
                {meta.id === "others" ? null : <AddToQueueButton meta={meta} rom={rom} variant="full" className="flex-1" />}
              </div>
              <div className="flex flex-col gap-2 sm:flex-row">

                {meta.id === "others" && onDelete ? <button type="button" onClick={onDelete} className="inline-flex flex-1 items-center justify-center gap-2 rounded-xl border border-rose-300/20 px-4 py-3 text-xs font-medium text-rose-200/80 transition hover:border-rose-400/50 hover:bg-rose-500/10 hover:text-rose-100"><Icon name="trash" className="size-4" />Delete from Others</button> : null}
                {meta.id !== "others" ? <a href={meta.source} target="_blank" rel="noopener noreferrer" className="inline-flex items-center justify-center gap-2 rounded-xl border border-white/10 px-4 py-3 text-xs font-medium text-white/60 transition hover:bg-white/5 hover:text-white">
                  View source<Icon name="arrow" className="h-3.5 w-3.5 -rotate-45" />
                </a> : null}
              </div>
              {meta.id === "others" ? <p className="text-[11px] leading-relaxed text-white/40">Already stored on this server. In DSiDrop, switch to Others to download or queue this game on your SD card.</p> : null}
            </div>
          </div>
        </div>

        {siblings.length > 0 ? (
          <div className="border-t border-white/[0.06] bg-white/[0.015] p-5 sm:px-7">
            <div className="mb-3 flex items-center gap-2">
              <h3 className="text-xs font-semibold text-white/70">Other versions & regions</h3>
              <span className="rounded-md bg-white/5 px-1.5 py-0.5 text-[10px] text-white/35">{siblings.length}</span>
            </div>
            <div className="flex flex-wrap gap-2">
              {siblings.map((sibling) => <button key={`${sibling.m}-${sibling.n}`} type="button" onClick={() => onSelect(sibling)} className="rounded-xl border border-white/10 px-3 py-2 text-[11px] text-white/60 transition hover:border-brand-400/30 hover:bg-brand-500/10 hover:text-white">{sibling.r}{(sibling.g ?? []).length ? ` · ${sibling.g?.join(", ")}` : ""}</button>)}
            </div>
          </div>
        ) : null}
      </div>
    </div>
  );
}
