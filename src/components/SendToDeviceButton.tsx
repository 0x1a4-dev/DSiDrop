"use client";

import Icon from "./Icon";
import { openDevices } from "@/lib/device-client";
import type { DatasetMeta, Rom } from "@/lib/types";

export default function SendToDeviceButton({ meta, rom, full = false }: { meta: DatasetMeta; rom: Rom; full?: boolean }) {
  const system = meta.id;
  if (system !== "nds" && system !== "gba" && system !== "others") return null;
  return <button type="button" aria-label={full ? "Send to DSi" : `Send ${rom.t} to DSi`} title="Send to DSi" onClick={(event) => { event.preventDefault(); event.stopPropagation(); openDevices({ items: [{ system, id: rom.m }], label: rom.t }); }} className={full ? "inline-flex items-center justify-center gap-2 rounded-xl border border-brand-400/25 bg-brand-500/10 px-4 py-3 text-xs font-semibold text-brand-300 transition hover:bg-brand-500/20" : "grid size-9 place-items-center rounded-xl border border-brand-400/30 bg-black/75 text-brand-300 backdrop-blur transition hover:bg-brand-500/50 hover:text-white"}><Icon name="device" className="size-4" />{full ? "Send to DSi" : null}</button>;
}
