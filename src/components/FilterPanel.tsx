"use client";

import { useId } from "react";

import { CATEGORY_ORDER, categoryLabel, languageLabel } from "@/lib/roms";
import type { CoverFilter, DatasetMeta } from "@/lib/types";
import Icon from "./Icon";

export type FilterState = {
  region: string;
  type: string;
  lang: string;
  cover: CoverFilter;
};

export const EMPTY_FILTERS: FilterState = {
  region: "all",
  type: "all",
  lang: "all",
  cover: "all",
};

const number = new Intl.NumberFormat("en-US");
const selectStyle = "mt-2 w-full cursor-pointer rounded-xl border border-white/10 bg-ink-900 px-3 py-2.5 text-xs text-white/80 outline-none transition hover:border-white/25 focus:border-brand-400 focus:ring-2 focus:ring-brand-400/15";

export default function FilterPanel({
  meta,
  filters,
  onChange,
  activeCount,
  onReset,
  categoryCounts,
}: {
  meta: DatasetMeta;
  filters: FilterState;
  onChange: (patch: Partial<FilterState>) => void;
  activeCount: number;
  onReset: () => void;
  categoryCounts: Record<string, number>;
}) {
  const id = useId();
  return (
    <section aria-label="Catalog filters" className="rounded-2xl border border-white/[0.07] bg-ink-850/75 p-5">
      <div className="mb-6 flex items-center justify-between gap-2">
        <h2 className="flex items-center gap-2 text-sm font-semibold text-white/90">
          <Icon name="filter" className="h-4 w-4 text-brand-400" />
          Filters
        </h2>
        {activeCount > 0 ? (
          <button type="button" onClick={onReset} className="rounded-md px-1 py-1 text-[11px] font-medium text-brand-400 transition hover:text-white">
            Reset ({activeCount})
          </button>
        ) : null}
      </div>
      <div className="space-y-6">
        <fieldset>
          <legend className="text-[11px] font-semibold uppercase tracking-[0.12em] text-white/40">Cover art</legend>
          <div className="mt-2 grid grid-cols-3 gap-1 rounded-xl border border-white/5 bg-black/20 p-1">
            {([ ["all", "Any"], ["with", "Available"], ["without", "Missing"] ] as const).map(([value, label]) => (
              <button
                type="button"
                key={value}
                aria-pressed={filters.cover === value}
                onClick={() => onChange({ cover: value })}
                className={`rounded-lg px-1 py-2 text-[10px] font-medium transition ${filters.cover === value ? "bg-brand-500/20 text-brand-400 shadow-sm" : "text-white/45 hover:bg-white/5 hover:text-white/80"}`}
              >
                {label}
              </button>
            ))}
          </div>
        </fieldset>
        <div>
          <label htmlFor={`${id}-region`} className="text-[11px] font-semibold uppercase tracking-[0.12em] text-white/40">Region</label>
          <select id={`${id}-region`} value={filters.region} onChange={(event) => onChange({ region: event.target.value })} className={selectStyle}>
            <option value="all">All regions</option>
            {meta.regions.map((region) => <option key={region.name} value={region.name}>{region.name} ({number.format(region.count)})</option>)}
          </select>
        </div>
        <fieldset>
          <legend className="text-[11px] font-semibold uppercase tracking-[0.12em] text-white/40">Release type</legend>
          <div className="mt-2 space-y-1">
            {["all", ...CATEGORY_ORDER].map((category) => {
              const count = category === "all" ? meta.total : categoryCounts[category] ?? 0;
              if (!count) return null;
              const active = filters.type === category;
              return (
                <button
                  key={category}
                  type="button"
                  aria-pressed={active}
                  onClick={() => onChange({ type: category })}
                  className={`flex w-full items-center gap-2 rounded-xl px-3 py-2.5 text-left text-xs transition ${active ? "bg-brand-500/12 text-brand-400" : "text-white/60 hover:bg-white/5 hover:text-white/90"}`}
                >
                  <span className={`grid h-3.5 w-3.5 shrink-0 place-items-center rounded border ${active ? "border-brand-400/70 bg-brand-500/20" : "border-white/20"}`}>
                    {active ? <Icon name="check" className="h-2.5 w-2.5" /> : null}
                  </span>
                  <span className="flex-1">{category === "all" ? "All releases" : categoryLabel(category)}</span>
                  <span className={`text-[10px] tabular-nums ${active ? "text-brand-400/70" : "text-white/30"}`}>{number.format(count)}</span>
                </button>
              );
            })}
          </div>
        </fieldset>
        <div>
          <label htmlFor={`${id}-language`} className="text-[11px] font-semibold uppercase tracking-[0.12em] text-white/40">Language</label>
          <select id={`${id}-language`} value={filters.lang} onChange={(event) => onChange({ lang: event.target.value })} className={selectStyle}>
            <option value="all">All languages</option>
            {meta.languages.map((language) => <option key={language.name} value={language.name}>{languageLabel(language.name)} ({number.format(language.count)})</option>)}
          </select>
        </div>
      </div>
    </section>
  );
}
