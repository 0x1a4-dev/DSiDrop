"use client";

import { useEffect, useId, useRef, useState } from "react";
import Icon from "./Icon";
import type { SystemInfo } from "@/lib/types";

function menuPosition(button: HTMLButtonElement) {
  const bounds = button.getBoundingClientRect();
  const width = Math.min(352, document.documentElement.clientWidth - 24);
  const below = window.innerHeight - bounds.bottom - 20;
  const above = bounds.top - 20;
  const downward = below >= 300 || below >= above;
  const maxHeight = Math.max(0, Math.min(540, downward ? below : above));
  return {
    width,
    maxHeight,
    left: Math.max(12, Math.min(bounds.left, document.documentElement.clientWidth - width - 12)),
    ...(downward ? { top: bounds.bottom + 8 } : { bottom: window.innerHeight - bounds.top + 8 }),
  };
}

export default function SystemPicker({ systems, selectedId, onSelect }: {
  systems: SystemInfo[];
  selectedId?: string;
  onSelect: (id: string) => void;
}) {
  const id = useId();
  const trigger = useRef<HTMLButtonElement>(null);
  const menu = useRef<HTMLDivElement>(null);
  const search = useRef({ text: "", time: 0 });
  const [open, setOpen] = useState(false);
  const [position, setPosition] = useState<ReturnType<typeof menuPosition>>({ width: 352, maxHeight: 540, left: 12, top: 0 });
  const selected = systems.find((system) => system.id === selectedId);

  useEffect(() => {
    if (!open) return;
    const reposition = (event: Event) => {
      if (event.target instanceof Node && menu.current?.contains(event.target)) return;
      if (trigger.current) setPosition(menuPosition(trigger.current));
    };
    window.addEventListener("resize", reposition);
    window.addEventListener("scroll", reposition, true);
    return () => {
      window.removeEventListener("resize", reposition);
      window.removeEventListener("scroll", reposition, true);
    };
  }, [open]);

  return <div className={`system-tab system-picker ${selected ? "is-active" : ""}`}>
    <button ref={trigger} type="button" className="system-trigger" popoverTarget={id} aria-label={`Game system: ${selected?.label ?? "choose a system"}`} aria-haspopup="listbox" aria-controls={id} aria-expanded={open} disabled={!systems.length}
      onClick={() => { if (trigger.current) setPosition(menuPosition(trigger.current)); }}
      onKeyDown={(event) => {
        if (event.key === "ArrowDown" || event.key === "ArrowUp") {
          event.preventDefault();
          if (!open) trigger.current?.click();
        }
      }}>
      <Icon name="grid" className="system-trigger-icon" />
      <span className="system-trigger-name">{selected?.short ?? "Games"}</span>
      {selected ? <span className="system-count">{selected.total.toLocaleString("en-US")}</span> : null}
      <Icon name="chevron" className="system-trigger-chevron" />
    </button>
    <div ref={menu} id={id} popover="auto" role="listbox" aria-label="Game system" className="system-menu" style={position}
      onToggle={(event) => {
        const opening = event.newState === "open";
        setOpen(opening);
        search.current = { text: "", time: 0 };
        if (opening) requestAnimationFrame(() => {
          if (!menu.current?.matches(":popover-open")) return;
          const option = menu.current.querySelector<HTMLButtonElement>('[role="option"][aria-selected="true"]') ?? menu.current.querySelector<HTMLButtonElement>('[role="option"]');
          option?.focus({ preventScroll: true });
          option?.scrollIntoView({ block: "nearest" });
        });
      }}
      onKeyDown={(event) => {
        const options = [...menu.current?.querySelectorAll<HTMLButtonElement>('[role="option"]') ?? []];
        const current = options.indexOf(document.activeElement as HTMLButtonElement);
        let next = -1;
        if (event.key === "ArrowDown") next = (current + 1) % options.length;
        if (event.key === "ArrowUp") next = (current - 1 + options.length) % options.length;
        if (event.key === "Home") next = 0;
        if (event.key === "End") next = options.length - 1;
        if (event.key.length === 1 && /^[a-z0-9]$/i.test(event.key) && !event.ctrlKey && !event.altKey && !event.metaKey) {
          const letter = event.key.toLowerCase();
          const text = Date.now() - search.current.time < 700 ? search.current.text + letter : letter;
          search.current = { text, time: Date.now() };
          const prefix = [...text].every((character) => character === letter) ? letter : text;
          for (let offset = 1; offset <= options.length; offset++) {
            const index = (current + offset) % options.length;
            if ([systems[index].short, systems[index].label].some((label) => label.toLowerCase().startsWith(prefix))) { next = index; break; }
          }
          event.preventDefault();
        }
        if (next >= 0) { event.preventDefault(); options[next]?.focus(); }
        if (event.key === "Escape" || event.key === "Tab") {
          menu.current?.hidePopover();
          if (event.key === "Escape") { event.preventDefault(); trigger.current?.focus({ preventScroll: true }); }
        }
      }}>
      <div className="system-menu-heading" role="presentation"><span>Game library</span><span>{systems.length} systems</span></div>
      <div className="system-menu-options" role="presentation">
        {systems.map((system) => <button key={system.id} type="button" role="option" aria-selected={system.id === selectedId} tabIndex={-1} data-system={system.id} className="system-option"
          onClick={() => { menu.current?.hidePopover(); trigger.current?.focus({ preventScroll: true }); onSelect(system.id); }}>
          <span className="system-option-badge" aria-hidden="true">{system.short}</span>
          <span className="system-option-copy"><span className="system-option-name">{system.label}</span><span className="system-option-count">{system.total.toLocaleString("en-US")} games</span></span>
          <Icon name="check" className="system-option-check" />
        </button>)}
      </div>
    </div>
  </div>;
}