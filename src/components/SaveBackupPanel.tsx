"use client";

import { useEffect, useRef, useState, type ReactNode } from "react";
import type { DsiDevice } from "@/lib/device-types";
import type { SaveSnapshot } from "@/lib/save-types";
import type { DeviceGame, DeviceLibrarySnapshot } from "@/lib/device-library-types";
import { gameSaveTargets, matchingDeviceGame, supportsAutomaticRestore } from "@/lib/device-library";
import { formatBytes } from "@/lib/roms";
import Icon from "./Icon";

export type SaveAction = "backup" | "restore" | "transfer";
type Props = { device: DsiDevice; devices: DsiDevice[]; game: DeviceGame; mode: SaveAction; snapshot: SaveSnapshot; busy: boolean; error: string; cover: ReactNode; onChange: (body: Record<string, unknown>, message: string) => Promise<boolean>; onClose: () => void };

export default function SaveBackupPanel({ device, devices, game, mode, snapshot, busy, error, cover, onChange, onClose }: Props) {
  const dialog = useRef<HTMLDialogElement>(null);
  const [chosen, setChosen] = useState("");
  const [savePath, setSavePath] = useState("");
  const [deleting, setDeleting] = useState("");
  const [targetMac, setTargetMac] = useState("");
  const [targetId, setTargetId] = useState("");
  const [targetPath, setTargetPath] = useState("");
  const [target, setTarget] = useState<DeviceLibrarySnapshot | null>(null);
  const [targetSaves, setTargetSaves] = useState<SaveSnapshot | null>(null);
  const [targetError, setTargetError] = useState("");
  useEffect(() => {
    const element = dialog.current;
    element?.showModal();
    return () => element?.close();
  }, []);
  useEffect(() => {
    if (mode !== "transfer" || !targetMac) return;
    const controller = new AbortController();
    void (async () => {
      try {
        const responses = await Promise.all([fetch(`/api/devices/library?mac=${targetMac}`, { cache: "no-store", signal: controller.signal }), fetch(`/api/saves?mac=${targetMac}`, { cache: "no-store", signal: controller.signal })]);
        const [library, saves] = await Promise.all(responses.map((response) => response.json()));
        if (!responses[0].ok || !responses[1].ok) throw new Error(library.error || saves.error || "Destination unavailable");
        if (!controller.signal.aborted) { setTarget(library); setTargetSaves(saves); }
      } catch (error) { if (!controller.signal.aborted) setTargetError(error instanceof Error ? error.message : "Destination unavailable"); }
    })();
    return () => controller.abort();
  }, [mode, targetMac]);
  const paths = game.savePaths;
  const selectedPath = paths.includes(savePath) ? savePath : paths[0];
  const versions = snapshot.backups.filter((entry) => paths.includes(entry.path)).sort((first, second) => second.createdAt.localeCompare(first.createdAt));
  const selected = versions.find((backup) => backup.id === chosen) || versions[0];
  const pending = snapshot.tasks.some((task) => task.status === "queued" || task.status === "running");
  const [major, minor] = device.version.split(".").map(Number);
  const targeted = major > 1 || major === 1 && minor >= 15;
  const destinations = devices.filter((entry) => entry.mac !== device.mac);
  const destination = destinations.find((entry) => entry.mac === targetMac);
  const matches = target?.games.filter((entry) => matchingDeviceGame(game, entry)) || [];
  const targetGame = matches.find((entry) => entry.id === targetId) || matches[0];
  const targetPaths = targetGame ? gameSaveTargets(targetGame) : [];
  const destinationPath = targetPaths.includes(targetPath) ? targetPath : targetPaths[0];
  const destinationPending = targetSaves?.tasks.some((task) => task.status === "queued" || task.status === "running");
  const title = mode === "backup" ? "Backup Save" : mode === "restore" ? "Save Restore" : "Transfer save";
  async function submit() {
    if (mode === "backup") {
      if (await onChange({ action: "backup", path: selectedPath }, `Save backup queued for ${game.title}.`)) onClose();
    } else if (mode === "restore" && selected) {
      if (await onChange({ action: "restore", backup: selected.id }, `Automatic restore queued for ${game.title}. No confirmation needed on ${device.name}.`)) onClose();
    } else if (mode === "transfer" && selected && targetGame && destination) {
      if (await onChange({ action: "transfer", backup: selected.id, game: game.id, targetMac, targetGame: targetGame.id, path: destinationPath }, `Automatic save transfer queued for ${destination.name}. No confirmation needed on the destination DSi.`)) onClose();
    }
  }
  return <dialog ref={dialog} className="device-save-dialog" aria-labelledby="device-save-heading" onCancel={(event) => { event.preventDefault(); if (!busy) onClose(); }} onClick={(event) => { if (event.target === event.currentTarget && !busy) onClose(); }}>
    <header className="flex items-start justify-between gap-4 border-b border-white/10 p-5"><div className="min-w-0"><h3 id="device-save-heading" className="text-base font-semibold">{title}</h3><p className="mt-1 break-words text-xs text-white/45">{device.name}</p></div><button type="button" className="device-icon-button" title="Close save dialog" aria-label="Close save dialog" disabled={busy} onClick={onClose}><Icon name="close" className="size-4" /></button></header>
    <div className="min-h-0 overflow-y-auto p-5">
      <div className="flex items-center gap-4">{cover}<div className="min-w-0"><h4 className="break-words text-sm font-semibold">{game.title}</h4><p className="mt-2 break-all text-[10px] text-white/40">{game.path || game.savePaths[0]}</p></div></div>
      {error || targetError ? <p role="alert" className="mt-4 break-words text-xs text-rose-300">{error || targetError}</p> : null}
      {mode === "backup" ? <label className="device-save-field">Save file<select value={selectedPath || ""} onChange={(event) => setSavePath(event.target.value)}>{paths.map((path) => <option key={path} value={path}>{path}</option>)}</select></label> : <>
        <fieldset className="mt-5"><legend className="text-xs font-medium text-white/70">Backup version <span className="ml-2 text-white/35">{versions.length}</span></legend>
          {versions.length ? <ol className="mt-2 max-h-64 divide-y divide-white/10 overflow-y-auto">{versions.map((backup, index) => <li key={backup.id} className="py-3"><div className="flex items-start gap-2"><label className="flex min-w-0 flex-1 cursor-pointer items-start gap-3"><input type="radio" name="save-backup" className="mt-1" checked={selected?.id === backup.id} onChange={() => { setChosen(backup.id); setDeleting(""); }} /><span className="min-w-0"><span className="block text-xs">{new Date(backup.createdAt).toLocaleString()}{index === 0 ? <span className="ml-2 text-[10px] text-emerald-300">Latest</span> : null}</span><span className="mt-1 block text-[10px] text-white/40">{formatBytes(backup.size)} / {backup.automatic ? "Automatic" : "Manual / pre-restore"}</span><span className="mt-1 block break-all text-[10px] text-white/30">{backup.path}</span></span></label><a className="device-icon-button" title="Download backup" aria-label={`Download backup ${backup.id}`} href={`/api/saves/file?mac=${device.mac}&id=${backup.id}`}><Icon name="download" className="size-3.5" /></a><button type="button" className="device-icon-button" title="Delete backup" aria-label={`Delete backup ${backup.id}`} disabled={busy || pending} onClick={() => setDeleting(backup.id)}><Icon name="trash" className="size-3.5" /></button></div>{deleting === backup.id ? <div className="mt-3 flex flex-wrap items-center gap-2 text-xs text-rose-200"><span>Delete backup?</span><button type="button" className="device-action is-danger" disabled={busy || pending} onClick={async () => { if (await onChange({ delete: backup.id }, "Backup deleted.")) setDeleting(""); }}>Delete</button><button type="button" className="device-action" onClick={() => setDeleting("")}>Cancel</button></div> : null}</li>)}</ol> : <div className="py-5"><p className="text-xs text-white/45">No save backups yet.</p>{selectedPath ? <button type="button" className="device-action mt-3" disabled={busy || pending || !targeted} onClick={() => void onChange({ action: "backup", path: selectedPath }, "Save backup queued.")}><Icon name="copy" className="size-3.5" />Back up current save</button> : null}</div>}
        </fieldset>
        {mode === "transfer" ? <div className="mt-4 border-t border-white/10 pt-1"><label className="device-save-field">Destination DSi<select value={targetMac} onChange={(event) => { setTargetMac(event.target.value); setTargetId(""); setTargetPath(""); setTarget(null); setTargetSaves(null); setTargetError(""); }}><option value="">Choose DSi</option>{destinations.map((entry) => <option key={entry.mac} value={entry.mac} disabled={!supportsAutomaticRestore(entry.version)}>{entry.name} / {supportsAutomaticRestore(entry.version) ? entry.online ? "Online" : "Offline" : "Update to 1.16.0 required"}</option>)}</select></label>
          {!destinations.length ? <p className="mt-3 text-xs text-white/45">No other DSi registered.</p> : null}
          {targetMac && !target && !targetError ? <p className="mt-3 text-xs text-white/45">Loading destination games...</p> : null}
          {target ? matches.length ? <><label className="device-save-field">Destination game<select value={targetGame?.id || ""} onChange={(event) => { setTargetId(event.target.value); setTargetPath(""); }}>{matches.map((entry) => <option key={entry.id} value={entry.id}>{entry.title} / {entry.path}</option>)}</select></label><label className="device-save-field">Destination save file<select value={destinationPath || ""} onChange={(event) => setTargetPath(event.target.value)}>{targetPaths.map((path) => <option key={path} value={path}>{path}</option>)}</select></label></> : <p className="mt-3 text-xs text-amber-200">No matching installed game in this DSi&apos;s last inventory.</p> : null}
          {destinationPending ? <p role="status" className="mt-3 text-xs text-amber-200">The destination already has a pending save operation.</p> : null}
        </div> : null}
        <p className="mt-5 text-[11px] leading-relaxed text-white/45">{mode === "transfer" ? "Source backup is kept. The destination save is backed up before replacement." : "The current save is backed up before replacement."} Runs automatically without confirmation on the DSi.{mode === "transfer" ? destination && !destination.online ? " The transfer waits until the destination reconnects." : "" : !device.online ? " Restoration waits until the device reconnects." : ""}</p>
      </>}
    </div>
    <footer className="flex flex-wrap justify-end gap-2 border-t border-white/10 p-4"><button type="button" className="device-action" disabled={busy} onClick={onClose}>Cancel</button><button type="button" className="device-action is-primary" disabled={busy || (mode === "backup" ? pending || !selectedPath : !selected || mode === "restore" && pending || mode === "transfer" && (!destination || !targetGame || !targetSaves || destinationPending || !!targetError))} onClick={() => void submit()}><Icon name={mode === "backup" ? "copy" : mode === "restore" ? "download" : "arrow"} className="size-3.5" />{busy ? "Saving..." : title}</button></footer>
  </dialog>;
}