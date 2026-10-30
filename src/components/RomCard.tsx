"use client";

import AddToQueueButton from "./AddToQueueButton";
import CoverImage from "./CoverImage";
import Icon from "./Icon";
import SendToDeviceButton from "./SendToDeviceButton";
import { AddToCollectionButton } from "./CollectionsPanel";
import { categoryLabel, downloadUrl, formatBytes, romCategory } from "@/lib/roms";
import { mediaInfo } from "@/lib/media";
import type { DatasetMeta, Rom } from "@/lib/types";

export default function RomCard({ meta, rom, onOpen, onDelete }: { meta: DatasetMeta; rom: Rom; onOpen: () => void; onDelete?: () => void }) {
  const category = romCategory(rom);
  const media = mediaInfo(rom.media ?? "");
  const rev = (rom.g ?? []).find((tag) => tag.startsWith("Rev "));
  return (
    <article className="group relative flex min-w-0 flex-col">
      <button
        type="button"
        onClick={onOpen}
        aria-label={`View ${rom.t} details`}
        className="flex h-full w-full flex-col overflow-hidden rounded-2xl border border-white/[0.07] bg-ink-850 text-left transition duration-200 hover:-translate-y-1 hover:border-brand-400/40 hover:shadow-[0_16px_40px_-20px_rgba(139,61,255,0.45)] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-brand-400"
      >
        <div className="relative w-full border-b border-white/[0.04] bg-ink-900">
          <CoverImage meta={meta} rom={rom} className="aspect-[6/5] w-full" sizes="(max-width: 640px) 45vw, (max-width: 1024px) 30vw, 240px" />
          <span className="absolute bottom-2.5 left-3 rounded-md border border-white/10 bg-black/65 px-2 py-1 text-[9px] font-semibold tracking-wider text-white/80 backdrop-blur-md">{category === "Multimedia" ? "Multimedia" : meta.short}</span>
        </div>
        <div className="flex flex-1 flex-col p-3.5 sm:p-4">
          <p className="mb-1.5 text-[10px] font-medium tracking-wide text-white/40">{rom.folder ? `Folder · ${rom.folder.count} ${rom.folder.count === 1 ? "file" : "files"}` : media ? `${media.kind} · ${media.extension.toUpperCase()}` : meta.id === "others" ? `${category === "Apps" ? "Apps" : "Your upload"} · Nintendo DS` : rom.r}{rev ? ` · ${rev}` : ""}</p>
          <h3 className="line-clamp-2 min-h-[2.6rem] text-[13px] font-semibold leading-[1.6] text-white/90 transition group-hover:text-white">{rom.t}</h3>
          <div className="mt-3 flex items-center justify-between gap-2 border-t border-white/5 pt-3 text-[10px]">
            <span title={rom.folder?.destination} className={category === "Retail" ? "text-white/35" : "truncate font-medium text-brand-400"}>{rom.folder ? `${rom.folder.destination}/` : media ? `${media.directory}/` : meta.id === "others" && category !== "Apps" ? "Stored on this server" : categoryLabel(category)}</span>
            <span className="shrink-0 tabular-nums text-white/45">{formatBytes(rom.s)}</span>
          </div>
        </div>
      </button>
      {meta.id === "others" && onDelete ? <button type="button" onClick={onDelete} aria-label={`Delete ${rom.t} from Others`} title="Delete from Others" className="absolute left-2.5 top-2.5 z-10 grid size-9 place-items-center rounded-xl border border-rose-300/20 bg-black/75 text-rose-200/80 backdrop-blur transition hover:border-rose-400/60 hover:bg-rose-500/30 hover:text-white focus-visible:ring-2 focus-visible:ring-rose-400"><Icon name="trash" className="size-4" /></button> : null}
      <div className="absolute right-2.5 top-2.5 z-10 flex flex-col gap-1.5">
        <SendToDeviceButton meta={meta} rom={rom} />
        {!media && !rom.folder ? <AddToCollectionButton meta={meta} rom={rom} /> : null}

        {meta.id === "others" ? <a href={downloadUrl(meta, rom)} aria-label={`Download ${rom.t}`} title={rom.folder ? "Download folder transfer package" : media ? "Download media file" : "Download NDS file"} className="grid size-9 place-items-center rounded-xl border border-white/15 bg-black/75 text-white/85 backdrop-blur transition hover:border-brand-400/50 hover:bg-brand-500/50"><Icon name="download" className="size-4" /></a> : <AddToQueueButton meta={meta} rom={rom} />}
      </div>
    </article>
  );
}
