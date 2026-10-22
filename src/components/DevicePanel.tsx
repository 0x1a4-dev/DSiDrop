"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import Icon from "./Icon";
import { forgetDevice, formatMac, loadDevices, onDevicesChange, onDevicesOpen, pauseDevice, removeDeviceTask, renameDevice, retryDeviceTask, sendDeviceQueue, type DeviceSelection } from "@/lib/device-client";
import type { DeviceSnapshot, DeviceTask, DsiDevice } from "@/lib/device-types";
import { formatBytes } from "@/lib/roms";
import { onQueueOpen } from "@/lib/queue-client";

const STATUS = { queued: "Waiting", downloading: "Downloading to SD", done: "Saved to SD", error: "Failed", canceled: "Canceled" };

export default function DevicePanel() {
  const [open, setOpen] = useState(false);
  const [snapshot, setSnapshot] = useState<DeviceSnapshot | null>(null);
  const [selection, setSelection] = useState<DeviceSelection | undefined>();
  const [chosen, setChosen] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const panel = useRef<HTMLDivElement>(null);
  const close = useRef<HTMLButtonElement>(null);
  const refresh = useCallback(() => { void loadDevices().then(setSnapshot).catch((err: unknown) => setError(err instanceof Error ? err.message : "Could not load your devices.")); }, []);

  useEffect(() => {
    const remove = onDevicesOpen((pending) => { setSelection(pending); setNotice(null); setError(null); setOpen(true); refresh(); });
    const changed = onDevicesChange(refresh);
    const hide = () => setOpen(false);
    const queue = onQueueOpen(hide);
    return () => { remove(); changed(); queue(); };
  }, [refresh]);

  useEffect(() => {
    if (!open) return;
    const timer = window.setInterval(refresh, 3000);
    const previousFocus = document.activeElement as HTMLElement | null;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    close.current?.focus();
    const key = (event: KeyboardEvent) => {
      if (event.key === "Escape") setOpen(false);
      if (event.key !== "Tab") return;
      const controls = [...panel.current?.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled), select:not(:disabled), a[href]') ?? []].filter((element) => element.getClientRects().length);
      const first = controls[0], last = controls[controls.length - 1];
      if (!first || !last) return;
      if (event.shiftKey && (document.activeElement === first || !panel.current?.contains(document.activeElement))) { event.preventDefault(); last.focus(); }
      else if (!event.shiftKey && (document.activeElement === last || !panel.current?.contains(document.activeElement))) { event.preventDefault(); first.focus(); }
    };
    document.addEventListener("keydown", key);
    return () => { window.clearInterval(timer); document.body.style.overflow = previousOverflow; document.removeEventListener("keydown", key); previousFocus?.focus(); };
  }, [open, refresh]);

  const devices = snapshot?.devices ?? [];
  const current = devices.find((device) => device.mac === chosen) ?? devices.find((device) => device.online) ?? devices[0];
  const run = async (action: () => Promise<DeviceSnapshot>, success?: string) => {
    setBusy(true); setError(null); setNotice(null);
    try { setSnapshot(await action()); if (success) setNotice(success); return true; }
    catch (err) { setError(err instanceof Error ? err.message : "The device could not be updated."); return false; }
    finally { setBusy(false); }
  };
  if (!open) return null;

  return <div className="fixed inset-0 z-[65] flex items-end justify-center bg-black/80 backdrop-blur-md sm:items-center sm:p-5" role="presentation" onClick={() => setOpen(false)}>
    <div ref={panel} role="dialog" aria-modal="true" aria-labelledby="devices-heading" onClick={(event) => event.stopPropagation()} className="animate-pop flex max-h-[96dvh] w-full max-w-5xl flex-col overflow-hidden rounded-t-3xl border border-white/10 bg-ink-900 shadow-2xl sm:max-h-[92dvh] sm:rounded-3xl">
      <header className="flex items-start justify-between gap-4 border-b border-white/[0.07] px-6 py-6 sm:px-8"><div><p className="eyebrow">YOUR HANDHELDS / LOCAL NETWORK</p><h2 id="devices-heading" className="mt-2 text-2xl font-semibold tracking-tight text-white">DSi devices</h2><p className="mt-2 max-w-xl text-xs leading-relaxed text-white/45">Open DSiDrop 1.10.1 or newer on your DSi. The app registers with this PC at startup and appears here automatically.</p></div><button ref={close} type="button" aria-label="Close DSi devices" onClick={() => setOpen(false)} className="grid size-9 shrink-0 place-items-center rounded-full border border-white/10 text-white/50 hover:bg-white/10 hover:text-white"><Icon name="close" className="size-4" /></button></header>
      {error ? <p role="alert" className="border-b border-rose-500/20 bg-rose-500/10 px-6 py-3 text-xs text-rose-200">{error}</p> : null}
      {notice ? <p role="status" className="border-b border-emerald-500/20 bg-emerald-500/10 px-6 py-3 text-xs text-emerald-200">{notice}</p> : null}
      <div className="min-h-0 flex-1 overflow-y-auto">
        {selection ? <section className="border-b border-brand-400/15 bg-brand-500/[0.06] px-6 py-5 sm:px-8"><p className="text-[10px] font-semibold uppercase tracking-wider text-brand-300">READY TO SEND</p><p className="mt-2 text-sm font-medium text-white/85">{selection.label}</p><div className="mt-3 flex flex-wrap items-center gap-3"><p className="flex-1 text-xs leading-relaxed text-white/40">{selection.items.length} {selection.items.length === 1 ? "game" : "games"} will be downloaded in order. Offline devices begin when they reconnect.</p><button type="button" disabled={busy || !current || selection.items.length > (snapshot?.limit ?? 32)} onClick={() => { if (current) void run(() => sendDeviceQueue(current.mac, selection.items), `Sent to ${current.name}. Downloads start when the app is connected and ready.`).then((sent) => { if (sent) setSelection(undefined); }); }} className="upload-primary disabled:opacity-40"><Icon name="device" className="size-4" />{busy ? "Sending…" : current ? `Send to ${current.name}` : "Choose a device"}</button></div>{selection.items.length > (snapshot?.limit ?? 32) ? <p className="mt-2 text-xs text-amber-200">Send up to 32 games at a time.</p> : null}</section> : null}

        {!snapshot ? <p className="p-16 text-center text-sm text-white/40">Looking for your devices…</p> : !devices.length ? <div className="flex flex-col items-center px-6 py-12 text-center"><span className="grid size-16 place-items-center rounded-2xl border border-brand-400/20 bg-brand-500/10 text-brand-300"><Icon name="device" className="size-8" /></span><h3 className="mt-5 text-lg font-semibold">Waiting for your first DSi.</h3><p className="mt-3 max-w-md text-xs leading-relaxed text-white/45">Replace DSiDrop.nds with the latest app and use the same Wi-Fi network. On the DSi, press SELECT, then X to enter this server’s local IP and port. Settings shows your app version, MAC and registration result. WEB OK means the server confirmed registration.</p><a href="/downloads/DSiDrop.nds" className="upload-primary mt-6"><Icon name="download" className="size-4" />Get the latest DSi app</a><button type="button" onClick={refresh} className="mt-4 text-xs text-white/45 hover:text-white">Refresh devices</button></div> : <div className="device-workspace">
          <nav aria-label="Your DSi devices" className="device-sidebar">{devices.map((device) => <button key={device.mac} type="button" onClick={() => setChosen(device.mac)} aria-pressed={current?.mac === device.mac} className={`device-picker ${current?.mac === device.mac ? "is-selected" : ""}`}><span className={`device-status-dot ${device.online ? "is-online" : ""}`} /><span className="min-w-0 flex-1"><span className="block truncate text-xs font-semibold">{device.name}</span><span className="mt-1 block text-[10px] text-white/35">{device.online ? device.state === "downloading" ? "Downloading" : device.state === "busy" ? "Using the app" : "Connected" : "Offline"}</span></span><Icon name="chevron" className="size-3" /></button>)}</nav>
          {current ? <section className="min-w-0 p-5 sm:p-7" aria-label={`${current.name} device details`}>
            <div className="flex flex-wrap items-center gap-3"><span className={`device-connection-badge ${current.online ? "is-online" : ""}`}>{current.online ? "Online" : "Offline"}</span><span className="text-[10px] text-white/35">{current.version ? `App ${current.version}` : "App version unknown"}{current.ip ? ` · ${current.ip}` : ""}</span></div>
            <DeviceName key={current.mac + current.name} device={current} busy={busy} onSave={(value) => void run(() => renameDevice(current.mac, value), "Device name saved.")} />
            <p className="mt-2 font-mono text-[10px] text-white/30">MAC {formatMac(current.mac)}{!current.online && current.lastSeen ? ` · Last seen ${new Date(current.lastSeen).toLocaleString("en-US")}` : ""}</p>
            <div className="mt-6 flex flex-wrap items-center justify-between gap-3 border-y border-white/[0.07] py-4"><div><h3 className="text-sm font-semibold text-white/85">Device download queue</h3><p className="mt-1 text-[11px] text-white/40">{current.tasks.filter((task) => task.status === "queued" || task.status === "downloading").length} of {snapshot.limit} slots · {current.paused ? "Paused after the current transfer" : "Downloads start automatically"}</p></div><button type="button" disabled={busy} onClick={() => void run(() => pauseDevice(current.mac, !current.paused))} className="rounded-lg border border-white/10 px-3 py-2 text-xs text-white/65 hover:bg-white/5 disabled:opacity-40">{current.paused ? "Resume queue" : "Pause queue"}</button></div>
            {!current.tasks.length ? <div className="rounded-xl border border-dashed border-white/10 px-4 py-8 text-center"><Icon name="queue" className="mx-auto size-6 text-brand-400/70" /><p className="mt-3 text-sm text-white/70">Ready for your next game.</p><p className="mt-2 text-xs leading-relaxed text-white/35">Use Send to DSi on an NDS, GBA or Others game, or send the compatible games from your web queue.</p></div> : <ol className="mt-4 space-y-2.5">{current.tasks.map((task) => <DeviceTaskRow key={task.id} task={task} busy={busy} onRemove={() => void run(() => removeDeviceTask(current.mac, task.id))} onRetry={() => void run(() => retryDeviceTask(current.mac, task.id))} />)}</ol>}
            {!current.online && !current.tasks.some((task) => task.status === "downloading") ? <button type="button" disabled={busy} onClick={() => void run(() => forgetDevice(current.mac), "Device forgotten. It will be registered again when the app reconnects.")} className="mt-6 text-[11px] text-white/35 hover:text-rose-300 disabled:opacity-40">Forget this offline device</button> : null}
          </section> : null}
        </div>}
      </div>
      <footer className="border-t border-white/[0.07] bg-black/15 px-6 py-4 text-[10px] leading-relaxed text-white/35 sm:px-8">Names are remembered by MAC address. Keep DSiDrop open on the handheld; local downloads finish before remote jobs begin. Cancel an active transfer with B on the DSi.</footer>
    </div>
  </div>;
}

function DeviceName({ device, busy, onSave }: { device: DsiDevice; busy: boolean; onSave: (name: string) => void }) {
  const [name, setName] = useState(device.name);
  return <form className="mt-4 flex items-center gap-2" onSubmit={(event) => { event.preventDefault(); if (name.trim()) onSave(name.trim()); }}><input aria-label="Device name" value={name} onChange={(event) => setName(event.target.value)} maxLength={64} required className="min-w-0 flex-1 rounded-xl border border-white/10 bg-black/20 px-3 py-2.5 text-base font-semibold tracking-tight text-white focus:border-brand-400/60 focus:outline-none" /><button type="submit" disabled={busy || !name.trim() || name.trim() === device.name} className="rounded-lg border border-white/10 px-3 py-2.5 text-[11px] text-white/65 hover:bg-white/5 disabled:opacity-30">Save name</button></form>;
}

function DeviceTaskRow({ task, busy, onRemove, onRetry }: { task: DeviceTask; busy: boolean; onRemove: () => void; onRetry: () => void }) {
  const active = task.status === "downloading", done = task.status === "done";
  const percent = done ? 100 : task.size ? Math.min(100, Math.round(task.received / task.size * 100)) : 0;
  return <li className={`rounded-xl border p-3.5 ${active ? "border-brand-400/25 bg-brand-500/[0.07]" : "border-white/[0.07] bg-white/[0.02]"}`}><div className="flex items-start gap-3"><span className="mt-0.5 grid size-7 shrink-0 place-items-center rounded-lg bg-white/5 text-white/40"><Icon name={done ? "check" : "download"} className={`size-3.5 ${done ? "text-emerald-300" : ""}`} /></span><div className="min-w-0 flex-1"><p className="line-clamp-2 text-xs font-medium leading-relaxed text-white/85">{task.title}</p><p className="mt-1 text-[10px] text-white/35">{task.system === "others" ? "Others · NDS upload" : task.system.toUpperCase()}{task.region ? ` · ${task.region}` : ""}</p><div className="mt-2 flex items-center justify-between gap-2"><span className={`text-[10px] ${done ? "text-emerald-300/80" : task.status === "error" || task.status === "canceled" ? "text-rose-300/80" : active ? "text-brand-300" : "text-white/40"}`}>{STATUS[task.status]}</span>{active || done ? <span className="font-mono text-[10px] text-white/40">{percent}%</span> : null}</div>{active || done ? <div role="progressbar" aria-label={`${task.title} SD progress`} aria-valuemin={0} aria-valuemax={100} aria-valuenow={percent} className="mt-2 h-1 overflow-hidden rounded-full bg-white/10"><div className={`h-full ${done ? "bg-emerald-400/70" : "bg-brand-400"}`} style={{ width: `${percent}%` }} /></div> : null}{active ? <p className="mt-2 font-mono text-[9px] text-white/35">{formatBytes(task.received)} / {formatBytes(task.size)}</p> : null}{task.status === "error" || task.status === "canceled" ? <p className="mt-2 text-[10px] leading-relaxed text-rose-200/70">{task.message}</p> : null}</div>{!active ? <button type="button" disabled={busy} onClick={onRemove} aria-label={`Remove ${task.title} from DSi queue`} className="p-1 text-white/25 hover:text-white disabled:opacity-30"><Icon name="close" className="size-3.5" /></button> : null}</div>{task.status === "error" || task.status === "canceled" ? <button type="button" disabled={busy} onClick={onRetry} className="mt-3 text-[10px] font-medium text-brand-300 hover:text-white disabled:opacity-40">Retry download</button> : null}</li>;
}
