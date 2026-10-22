"use client";

import { useState } from "react";

import Icon from "@/components/Icon";

import { addToQueue } from "@/lib/queue-client";
import { downloadUrl, romExt } from "@/lib/roms";
import type { DatasetMeta, Rom } from "@/lib/types";

type AddState = "idle" | "busy" | "done" | "error";

export default function AddToQueueButton({
  meta,
  rom,
  variant = "icon",
  className = "",
}: {
  meta: DatasetMeta;
  rom: Rom;
  variant?: "icon" | "full";
  className?: string;
}) {
  const [state, setState] = useState<AddState>("idle");

  const add = async () => {
    if (state === "busy") return;
    setState("busy");
    try {
      await addToQueue({
        id: rom.m,
        name: rom.n,
        title: rom.t,
        region: rom.r,
        system: meta.short,
        systemId: meta.id,
        size: rom.s,
        url: downloadUrl(meta, rom),
        arch: meta.arch,
        ext: romExt(meta, rom),
        out: meta.out,
      });
      setState("done");
      window.setTimeout(() => setState("idle"), 1500);
    } catch {
      setState("error");
      window.setTimeout(() => setState("idle"), 2200);
    }
  };

  if (variant === "icon") {
    return (
      <button
        type="button"
        onClick={(event) => {
          event.preventDefault();
          event.stopPropagation();
          void add();
        }}
        aria-label="Add to download queue"
        disabled={state === "busy"}
        title="Add to download queue"
        className={`grid size-9 place-items-center rounded-xl border text-sm font-bold backdrop-blur transition disabled:opacity-50 ${
          state === "done"
            ? "border-emerald-400/50 bg-emerald-500/25 text-emerald-200"
            : state === "error"
              ? "border-rose-400/50 bg-rose-500/25 text-rose-200"
              : "border-white/15 bg-black/75 text-white/85 hover:border-brand-400/50 hover:bg-brand-500/50 hover:text-white"
        } ${className}`}
      >
        {state === "busy" ? <span className="size-3.5 animate-spin rounded-full border-2 border-white/30 border-t-white" /> : state === "error" ? "!" : <Icon name={state === "done" ? "check" : "queue"} className="size-4" />}
      </button>
    );
  }

  const label =
    state === "done"
      ? "Added to queue"
      : state === "error"
        ? "Try again"
        : state === "busy"
          ? "Adding…"
          : "Add to queue";

  return (
    <button
      type="button"
      onClick={(event) => {
        event.preventDefault();
        event.stopPropagation();
        void add();
      }}
      disabled={state === "busy"}
      className={`inline-flex items-center justify-center gap-2 rounded-xl border px-4 py-3 text-sm font-medium transition disabled:opacity-60 ${
        state === "done"
          ? "border-emerald-400/40 bg-emerald-400/10 text-emerald-200"
          : state === "error"
            ? "border-rose-400/40 bg-rose-400/10 text-rose-200"
            : "border-white/10 text-white/75 hover:bg-white/5"
      } ${className}`}
    >
      <Icon name={state === "done" ? "check" : "queue"} className="size-4" />
      {label}
    </button>
  );
}
