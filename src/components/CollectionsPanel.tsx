"use client";

import { useEffect, useRef, useState } from "react";
import Image from "next/image";
import type { CollectionItem, GameCollection } from "@/lib/collection-types";
import type { DeviceQueueInput } from "@/lib/device-types";
import type { DatasetMeta, Rom } from "@/lib/types";
import { openDevices } from "@/lib/device-client";
import Icon from "./Icon";

const OPEN = "dsidrop-collections-open";
type Selection = { item: DeviceQueueInput; title: string };

export function CollectionsButton() {
  return <button type="button" className="header-button" title="Collections" aria-label="Collections" onClick={() => window.dispatchEvent(new CustomEvent(OPEN))}><Icon name="grid" className="size-4" /><span>Collections</span></button>;
}

export function AddToCollectionButton({ meta, rom, full = false }: { meta: DatasetMeta; rom: Rom; full?: boolean }) {
  const system = meta.id;
  if (system !== "nds" && system !== "gba" && system !== "others") return null;
  return <button type="button" title="Add to collection" aria-label={`Add ${rom.t} to collection`} className={full ? "toolbar-button w-full" : "grid size-9 place-items-center rounded-lg border border-white/15 bg-black/75 text-white/80 backdrop-blur hover:bg-brand-500/50"} onClick={(event) => { event.preventDefault(); event.stopPropagation(); window.dispatchEvent(new CustomEvent(OPEN, { detail: { item: { system, id: rom.m }, title: rom.t } satisfies Selection })); }}><Icon name="grid" className="size-4" />{full ? "Add to collection" : null}</button>;
}

export default function CollectionsPanel() {
  const [open, setOpen] = useState(false);
  const [collections, setCollections] = useState<GameCollection[] | null>(null);
  const [selection, setSelection] = useState<Selection | null>(null);
  const [chosen, setChosen] = useState("");
  const [name, setName] = useState("");
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [busy, setBusy] = useState(false);
  const [deleting, setDeleting] = useState(false);
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const listener = (event: Event) => { setSelection((event as CustomEvent<Selection>).detail || null); setError(""); setNotice(""); setOpen(true); };
    window.addEventListener(OPEN, listener);
    return () => window.removeEventListener(OPEN, listener);
  }, []);
  useEffect(() => {
    if (!open) { dialog.current?.close(); return; }
    dialog.current?.showModal();
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    const controller = new AbortController();
    fetch("/api/collections", { cache: "no-store", signal: controller.signal }).then(async (response) => {
      const data = await response.json(); if (!response.ok) throw new Error(data.error || "Collections unavailable"); setCollections(data.collections);
    }).catch((error: Error) => { if (!controller.signal.aborted) setError(error.message); });
    return () => { controller.abort(); document.body.style.overflow = previousOverflow; };
  }, [open]);
  async function change(body: Record<string, unknown>) {
    setBusy(true); setError(""); setNotice("");
    try {
      const response = await fetch("/api/collections", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
      const data = await response.json(); if (!response.ok) throw new Error(data.error || "Collection change failed");
      setCollections(data.collections); setChosen(data.selected || ""); setDeleting(false);
      if (body.action === "create") setName("");
      if (body.action === "add") { setSelection(null); setNotice("Added to collection"); }
    } catch (error) { setError(error instanceof Error ? error.message : "Collection change failed"); }
    finally { setBusy(false); }
  }
  const current = collections?.find((entry) => entry.id === chosen) ?? collections?.[0];
  const included = selection && current?.items.some((entry) => entry.system === selection.item.system && entry.id === selection.item.id);
  return <dialog ref={dialog} className="collections-dialog" aria-labelledby="collections-heading" onCancel={() => setOpen(false)} onClose={() => setOpen(false)} onClick={(event) => { if (event.target === event.currentTarget) setOpen(false); }}>
    <header className="flex shrink-0 items-center justify-between gap-4 border-b border-white/[0.07] px-6 py-6 sm:px-8"><h2 id="collections-heading" className="text-2xl font-semibold text-white">Collections</h2><button type="button" className="grid size-9 shrink-0 place-items-center rounded-full border border-white/10 text-white/50 hover:bg-white/10 hover:text-white" aria-label="Close collections" onClick={() => setOpen(false)}><Icon name="close" className="size-4" /></button></header>
    <div className="min-h-0 flex-1 space-y-5 overflow-y-auto px-6 py-5 sm:px-8 sm:py-6">
      {error ? <p role="alert" className="break-words text-sm text-rose-300">{error}</p> : null}{notice ? <p role="status" className="text-xs text-emerald-300">{notice}</p> : null}
      <form className="flex items-end gap-2" onSubmit={(event) => { event.preventDefault(); void change({ action: "create", name }); }}><label className="min-w-0 flex-1 text-xs text-white/50">New collection<input aria-label="New collection name" maxLength={64} required value={name} onChange={(event) => setName(event.target.value)} className="mt-2 w-full rounded-md border border-white/15 bg-black/20 p-2.5 text-sm text-white" /></label><button type="submit" title="Create collection" aria-label="Create collection" className="toolbar-button !h-10" disabled={busy || !name.trim()}><Icon name="plus" className="size-4" /></button></form>
      {collections?.length ? <label className="block text-xs text-white/50">Collection<select aria-label="Collection" value={current?.id} onChange={(event) => { setChosen(event.target.value); setDeleting(false); setNotice(""); }} className="mt-2 w-full min-w-0 rounded-md border border-white/15 bg-ink-900 p-2.5 text-sm text-white">{collections.map((entry) => <option key={entry.id} value={entry.id}>{entry.name} ({entry.items.length}/32)</option>)}</select></label> : null}
      {selection ? <div className="flex flex-wrap items-center gap-3 border-y border-white/10 py-4"><p className="min-w-0 flex-1 break-words text-sm">{selection.title}</p><button type="button" className="toolbar-button" disabled={busy || !current || !!included} onClick={() => { if (current) void change({ action: "add", id: current.id, items: [selection.item] }); }}><Icon name={included ? "check" : "plus"} className="size-4" />{included ? "Already included" : "Add to collection"}</button></div> : null}
      {current ? <><CollectionName key={`${current.id}:${current.name}`} collection={current} busy={busy} onSave={(name) => void change({ action: "rename", id: current.id, name })} />
        <div className="flex flex-wrap items-center justify-between gap-3"><p className="text-xs text-white/45">{current.items.length} / 32 games</p><button type="button" disabled={busy || !current.items.length} className="toolbar-button !text-brand-300" onClick={() => { setOpen(false); openDevices({ items: current.items.map(({ system, id }) => ({ system, id })), label: current.name }); }}><Icon name="device" className="size-4" />Send all to DSi</button></div>
        <ol className="divide-y divide-white/[0.07]">{current.items.map((item) => <li key={`${item.system}:${item.id}`} className="flex items-center gap-3 py-3"><a className="shrink-0 rounded-md" href={`/?sys=${item.system}&rom=${item.id}`} aria-label={`View ${item.title}`}><CollectionCover item={item} /></a><div className="min-w-0 flex-1"><a className="block break-words text-sm font-medium leading-relaxed text-white/85 hover:text-brand-300" href={`/?sys=${item.system}&rom=${item.id}`}>{item.title}</a><p className="mt-1 text-[10px] text-white/40">{item.system.toUpperCase()} / {item.region}</p></div><button type="button" disabled={busy} title="Remove from collection" aria-label={`Remove ${item.title} from collection`} className="toolbar-button shrink-0" onClick={() => void change({ action: "remove", id: current.id, game: item.id, system: item.system })}><Icon name="close" className="size-4" /></button></li>)}</ol>
        {!current.items.length ? <p className="text-sm text-white/40">No games in this collection.</p> : null}
        <div className="flex flex-wrap items-center gap-3 border-t border-white/10 pt-4">{deleting ? <><span className="text-xs text-rose-200">Delete collection?</span><button type="button" className="toolbar-button" disabled={busy} onClick={() => void change({ action: "delete", id: current.id })}>Delete</button><button type="button" className="toolbar-button" onClick={() => setDeleting(false)}>Cancel</button></> : <button type="button" className="toolbar-button !text-rose-200" disabled={busy} onClick={() => setDeleting(true)}><Icon name="trash" className="size-4" />Delete collection</button>}</div>
      </> : <p className="text-sm text-white/40">{collections ? "No collections yet." : "Loading collections..."}</p>}
    </div>
  </dialog>;
}

function CollectionCover({ item }: { item: CollectionItem }) {
  const [failedUrl, setFailedUrl] = useState<string | null>(null);
  const missing = !item.cover || failedUrl === item.cover;
  return <div className="relative grid h-24 w-20 place-items-center overflow-hidden rounded-md border border-white/[0.07] bg-ink-950 sm:h-28 sm:w-24">
    {missing ? <Icon name="play" className="size-6 text-brand-400/40" /> : <Image src={item.cover!} alt={`${item.title} cover art`} fill sizes="(min-width: 640px) 96px, 80px" className="object-contain p-1" onError={() => setFailedUrl(item.cover!)} />}
  </div>;
}

function CollectionName({ collection, busy, onSave }: { collection: GameCollection; busy: boolean; onSave: (name: string) => void }) {
  const [name, setName] = useState(collection.name);
  return <form className="flex items-end gap-2" onSubmit={(event) => { event.preventDefault(); onSave(name); }}><label className="min-w-0 flex-1 text-xs text-white/50">Name<input aria-label="Collection name" maxLength={64} required value={name} onChange={(event) => setName(event.target.value)} className="mt-2 w-full rounded-md border border-white/15 bg-black/20 p-2.5 text-sm text-white" /></label><button type="submit" className="toolbar-button !h-10" title="Save collection name" aria-label="Save collection name" disabled={busy || !name.trim() || name.trim() === collection.name}><Icon name="check" className="size-4" /></button></form>;
}