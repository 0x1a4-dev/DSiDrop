"use client";

import { useCallback, useEffect, useState } from "react";
import Image from "next/image";
import type { DsiDevice } from "@/lib/device-types";
import type { DeviceGame, DeviceLibrarySnapshot } from "@/lib/device-library-types";
import type { SaveSnapshot } from "@/lib/save-types";
import { supportsAutomaticRestore } from "@/lib/device-library";
import { formatBytes } from "@/lib/roms";
import Icon from "./Icon";
import SaveBackupPanel, { type SaveAction } from "./SaveBackupPanel";

function GameCover({ game }: { game: DeviceGame }) {
  const [failed, setFailed] = useState(false);
  return <span className="device-game-cover">
    {game.cover && !failed ? <Image src={game.cover} alt={`${game.title} cover`} fill sizes="80px" className="object-contain p-1" onError={() => setFailed(true)} /> : <Icon name="device" className="size-6 text-white/20" />}
  </span>;
}

export default function DeviceLibraryPanel({ device, devices }: { device: DsiDevice; devices: DsiDevice[] }) {
  const [snapshot, setSnapshot] = useState<DeviceLibrarySnapshot | null>(null);
  const [saves, setSaves] = useState<SaveSnapshot | null>(null);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [busy, setBusy] = useState(false);
  const [dialog, setDialog] = useState<{ mode: SaveAction; game: DeviceGame } | null>(null);
  const [query, setQuery] = useState("");
  const [view, setView] = useState<"installed" | "archived">("installed");
  const [deleting, setDeleting] = useState("");
  const load = useCallback(async () => {
    const responses = await Promise.all([fetch(`/api/devices/library?mac=${device.mac}`, { cache: "no-store" }), fetch(`/api/saves?mac=${device.mac}`, { cache: "no-store" })]);
    const [library, backups] = await Promise.all(responses.map((response) => response.json()));
    if (!responses[0].ok || !responses[1].ok) throw new Error(library.error || backups.error || "Device library unavailable");
    setSnapshot(library); setSaves(backups);
  }, [device.mac]);
  useEffect(() => {
    const refresh = () => { void load().catch((error: Error) => setError(error.message)); };
    refresh(); const timer = window.setInterval(refresh, 3000);
    return () => window.clearInterval(timer);
  }, [load]);
  async function change(body: Record<string, unknown>) {
    setBusy(true); setError(""); setNotice("");
    try {
      const response = await fetch("/api/devices/library", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ mac: device.mac, ...body }) });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "Library operation failed");
      setDeleting(""); await load();
    } catch (error) { setError(error instanceof Error ? error.message : "Library operation failed"); }
    finally { setBusy(false); }
  }
  async function changeSave(body: Record<string, unknown>, message: string): Promise<boolean> {
    setBusy(true); setError(""); setNotice("");
    try {
      const response = await fetch("/api/saves", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ mac: device.mac, ...body }) });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "Save operation failed");
      setNotice(message); await load(); return true;
    } catch (error) { setError(error instanceof Error ? error.message : "Save operation failed"); return false; }
    finally { setBusy(false); }
  }
  function openSave(mode: SaveAction, game: DeviceGame) {
    setError(""); setNotice("");
    if (mode === "backup" && game.savePaths.length === 1) void changeSave({ action: "backup", path: game.savePaths[0] }, `Save backup queued for ${game.title}.`);
    else setDialog({ mode, game });
  }
  const [major, minor] = device.version.split(".").map(Number);
  const supported = major > 1 || major === 1 && minor >= 15;
  const saveSupported = major > 1 || major === 1 && minor >= 14;
  const restoreSupported = supportsAutomaticRestore(device.version);
  const storage = snapshot?.storage;
  const used = storage ? storage.total - storage.free : 0;
  const usedPercent = storage ? Math.round(used / storage.total * 1000) / 10 : 0;
  const games = snapshot?.games || [];
  const visible = games.filter((game) => game.installed === (view === "installed") && `${game.title} ${game.path || ""} ${game.savePaths.join(" ")}`.toLowerCase().includes(query.toLowerCase()));
  const task = snapshot?.task, pending = task?.status === "running" || task?.status === "queued";
  const saveTask = saves?.tasks.at(-1), savePending = saves?.tasks.some((entry) => entry.status === "queued" || entry.status === "running");
  const backupCounts = new Map<string, number>();
  for (const backup of saves?.backups || []) backupCounts.set(backup.path, (backupCounts.get(backup.path) || 0) + 1);
  return <section aria-label="Device games" className="pt-5">
    <section aria-label="SD card storage" className="mb-5 border-b border-white/10 pb-5">
      <div className="flex items-center justify-between gap-3"><h3 className="text-xs font-semibold text-white/75">SD card</h3>{storage ? <span className={`text-xs font-medium tabular-nums ${usedPercent >= 90 ? "text-rose-300" : usedPercent >= 75 ? "text-amber-200" : "text-emerald-300"}`}>{usedPercent.toLocaleString(undefined, { maximumFractionDigits: 1 })}% used</span> : null}</div>
      {storage ? <><div role="meter" aria-label="SD card usage" aria-valuemin={0} aria-valuemax={100} aria-valuenow={usedPercent} aria-valuetext={`${usedPercent}% used, ${formatBytes(storage.free)} free of ${formatBytes(storage.total)}`} className="mt-3 h-2 overflow-hidden rounded-full bg-white/10"><div className={`h-full rounded-full ${usedPercent >= 90 ? "bg-rose-400" : usedPercent >= 75 ? "bg-amber-300" : "bg-emerald-400"}`} style={{ width: `${usedPercent}%` }} /></div><dl className="mt-3 grid grid-cols-3 gap-3 text-xs"><div className="min-w-0"><dt className="text-[10px] text-white/35">Total capacity</dt><dd className="mt-1 break-words font-medium tabular-nums text-white/80">{formatBytes(storage.total)}</dd></div><div className="min-w-0"><dt className="text-[10px] text-white/35">Used</dt><dd className="mt-1 break-words font-medium tabular-nums text-white/65">{formatBytes(used)}</dd></div><div className="min-w-0"><dt className="text-[10px] text-white/35">Free</dt><dd className="mt-1 break-words font-medium tabular-nums text-emerald-200">{formatBytes(storage.free)}</dd></div></dl><p className="mt-2 text-[10px] text-white/30">{device.online ? "Last reported" : "Offline / last reported"} {new Date(storage.updatedAt).toLocaleString()}</p></> : <p className="mt-2 text-[11px] text-white/40">{snapshot ? "No SD storage data reported yet." : "Loading SD storage..."}</p>}
    </section>
    <div className="flex flex-wrap items-start justify-between gap-3"><div><h3 className="text-sm font-semibold">Games on SD</h3><p className="mt-1 text-[10px] text-white/40">{snapshot?.scannedAt ? `Scanned ${new Date(snapshot.scannedAt).toLocaleString()}${device.online ? "" : " / offline snapshot"}` : "No SD inventory yet"}</p></div><div className="flex flex-wrap gap-2"><button type="button" className="device-action" disabled={busy || savePending || !saveSupported} onClick={() => void changeSave({ action: "backup" }, "Full-device save backup queued.")}><Icon name="copy" className="size-3.5" />Back up all</button></div></div>
    {!restoreSupported ? <p className="mt-3 text-xs text-amber-200">SD storage and automatic restore require DSiDrop 1.16.0. <a className="underline" href="/downloads/DSiDrop.nds">Update app</a></p> : null}
    <div className="mt-4 flex flex-wrap items-center justify-between gap-3"><div role="group" aria-label="Game library view" className="flex flex-wrap gap-1 rounded-md bg-black/20 p-1">{(["installed", "archived"] as const).map((mode) => <button key={mode} type="button" aria-pressed={view === mode} onClick={() => { setView(mode); setDeleting(""); }} className={`rounded px-3 py-2 text-xs ${view === mode ? "bg-white/10 text-white" : "text-white/45 hover:text-white"}`}>{mode === "installed" ? "Installed" : "Save archive"} <span className="ml-1 text-white/35">{games.filter((game) => game.installed === (mode === "installed")).length}</span></button>)}</div><label className="flex items-center gap-2 text-[11px] text-white/50"><input type="checkbox" disabled={busy || !saves || !saveSupported} checked={!!saves?.automatic[device.mac]} onChange={(event) => void changeSave({ automatic: event.target.checked }, "Automatic backup preference saved.")} />Automatic backups</label></div>
    <label className="relative mt-3 block"><Icon name="search" className="pointer-events-none absolute left-3 top-3 size-4 text-white/35" /><input aria-label="Search device games" type="search" value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Search games or SD paths" className="w-full min-w-0 rounded-md border border-white/10 bg-black/20 py-2.5 pl-9 pr-3 text-xs outline-none focus:border-brand-400/60" /></label>
    {error ? <p role="alert" className="device-job-status text-rose-300">{error}</p> : null}
    {notice ? <p role="status" className="device-job-status text-emerald-200">{notice}</p> : null}
    {[{ name: "SD", task, channel: "library" }, { name: "Saves", task: saveTask, channel: "saves" }].map(({ name, task: operation, channel }) => operation ? <div key={channel} role="status" className={`device-job-status ${operation.status === "error" ? "text-rose-300" : "text-white/45"}`}><span className="min-w-0 flex-1 break-words">{name} / {operation.message}</span>{(operation.status === "queued" || operation.status === "running" && !device.online) ? <button type="button" className="device-action" disabled={busy} onClick={() => channel === "library" ? void change({ cancel: true }) : void changeSave({ cancel: true }, "Save job cleared.")}>Clear job</button> : null}</div> : null)}
    {visible.length ? <ul aria-label="Games on this device" className="mt-3 divide-y divide-white/10">{visible.map((game) => {
      const versions = game.savePaths.reduce((count, path) => count + (backupCounts.get(path) || 0), 0);
      return <li key={game.id} className="device-game-row">
        <GameCover key={`${game.id}:${game.cover}`} game={game} />
        <div className="device-game-info"><h4 className="break-words text-sm font-semibold leading-snug">{game.title}</h4><p className="mt-2 text-[10px] text-white/45">{game.system?.toUpperCase() || "SAVE"} / {game.installed ? formatBytes(game.size) : "ROM not installed"} / {versions} {versions === 1 ? "backup" : "backups"}</p><p className="mt-1 break-all text-[10px] leading-relaxed text-white/30">{game.path || game.savePaths[0]}</p></div>
        <div className="device-game-actions">
          <button type="button" className="device-action" title={game.savePaths.length ? "Back up this game's save" : "No save file reported"} disabled={busy || savePending || !supported || !saves || !game.savePaths.length} onClick={() => openSave("backup", game)}><Icon name="copy" className="size-3.5" />Backup Save</button>
          <button type="button" className="device-action" title={!restoreSupported ? "Update to DSiDrop 1.16.0 for automatic restore" : versions ? "Choose a save backup to restore" : "No save backups yet"} disabled={busy || savePending || !restoreSupported || !versions} onClick={() => openSave("restore", game)}><Icon name="download" className="size-3.5" />Save Restore</button>
          <button type="button" className="device-action" title="Transfer a save backup to another DSi" disabled={busy || !saves} onClick={() => openSave("transfer", game)}><Icon name="arrow" className="size-3.5" />Transfer save</button>
          <button type="button" className="device-action is-danger" title={game.installed ? "Delete only this ROM from SD" : "ROM is not installed"} disabled={busy || pending || !supported || !game.installed} onClick={() => setDeleting(game.id)}><Icon name="trash" className="size-3.5" />Delete ROM</button>
        </div>
        {deleting === game.id ? <div role="alert" className="device-game-confirm"><p className="break-words text-xs text-rose-200">Delete {game.title} from {device.name}?</p><p className="mt-1 text-[11px] text-white/50">Saves and backups are kept. No confirmation on the DSi.{!device.online ? " Deletion waits until the device reconnects." : ""}</p><div className="mt-3 flex flex-wrap gap-2"><button type="button" className="device-action is-danger" disabled={busy || pending} onClick={() => void change({ action: "delete", game: game.id, confirmed: true })}><Icon name="trash" className="size-3.5" />Delete from SD</button><button type="button" className="device-action" onClick={() => setDeleting("")}>Cancel</button></div></div> : null}
      </li>;
    })}</ul> : <p className="py-10 text-center text-xs text-white/40">{!snapshot ? "Loading games..." : query ? "No matching games." : view === "installed" ? "No games reported on this device." : "No archived save files."}</p>}
    {dialog && saves ? <SaveBackupPanel device={device} devices={devices} game={games.find((game) => game.id === dialog.game.id) || dialog.game} mode={dialog.mode} snapshot={saves} busy={busy} error={error} cover={<GameCover key={`${dialog.game.id}:${dialog.game.cover}:dialog`} game={dialog.game} />} onChange={changeSave} onClose={() => setDialog(null)} /> : null}
  </section>;
}