"use client";

import Image from "next/image";
import { useState } from "react";

import { coverUrl } from "@/lib/roms";
import type { DatasetMeta, Rom } from "@/lib/types";

function initials(title: string): string {
  const words = title
    .replace(/[^\p{L}\p{N} ]/gu, " ")
    .split(/\s+/)
    .filter(Boolean);
  if (words.length === 0) return "?";
  if (words.length === 1) return words[0].slice(0, 2).toUpperCase();
  return (words[0][0] + words[1][0]).toUpperCase();
}

export default function CoverImage({
  meta,
  rom,
  sizes,
  className = "",
}: {
  meta: DatasetMeta;
  rom: Rom;
  sizes: string;
  className?: string;
}) {
  const url = coverUrl(meta, rom);
  const [failedUrl, setFailedUrl] = useState<string | null>(null);
  const missing = !url || failedUrl === url;

  return (
    <div className={`relative overflow-hidden bg-ink-900 ${className}`}>
      {missing ? (
        <div className="absolute inset-0 flex flex-col items-center justify-center gap-3 bg-[radial-gradient(ellipse_at_top_right,#26133d,#100c17_70%)]">
          <span className="text-5xl font-bold tracking-tighter text-brand-400/25">
            {initials(rom.t)}
          </span>
          <span className="rounded-full border border-white/10 px-2 py-0.5 text-[10px] font-medium uppercase tracking-wide text-white/35">
            No cover art
          </span>
        </div>
      ) : (
        <Image
          src={url}
          alt={`${rom.t} cover art`}
          fill
          sizes={sizes}
          className="object-contain p-3 transition-transform duration-500 group-hover:scale-[1.04] sm:p-4"
          onError={() => setFailedUrl(url)}
        />
      )}
      {rom.x === 1 && !missing ? (
        <span
          title="Closest available cover; artwork may be from another region"
          className="absolute left-2.5 top-2.5 rounded-md border border-white/10 bg-black/65 px-2 py-1 text-[9px] font-medium text-amber-200/85 backdrop-blur"
        >
          Alternate cover
        </span>
      ) : null}
    </div>
  );
}
