"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import { useRouter, useSearchParams } from "next/navigation";
import Image from "next/image";
import Link from "next/link";
import Icon from "./Icon";
import SystemPicker from "./SystemPicker";
import { loadQueue, onQueueChange, onQueueOpen, openQueuePanel } from "@/lib/queue-client";
import UploadLibrary from "./UploadLibrary";
import DSiDropPanel from "./DSiDropPanel";
import DevicesButton from "./DevicesButton";
import ThemeExplorer from "./ThemeExplorer";
import CollectionsPanel, { CollectionsButton } from "./CollectionsPanel";
import DeleteUploadDialog from "./DeleteUploadDialog";
import { onDevicesOpen } from "@/lib/device-client";

import FilterPanel, { EMPTY_FILTERS, type FilterState } from "./FilterPanel";
import RomCard from "./RomCard";
import RomDetail from "./RomDetail";
import {
  CATEGORY_ORDER,
  PAGE_SIZE,
  normalizeQuery,
  romCategory,
  searchIndex,
  sortRoms,
} from "@/lib/roms";
import type { Dataset, Rom, SortKey, SystemInfo } from "@/lib/types";

const SORT_OPTIONS: { key: SortKey; label: string }[] = [
  { key: "title-asc", label: "Title: A–Z" },
  { key: "title-desc", label: "Title: Z–A" },
  { key: "size-desc", label: "Size: largest first" },
  { key: "size-asc", label: "Size: smallest first" },
];

const OTHERS_SYSTEM: SystemInfo = {
  id: "others", label: "Others", short: "Others", data: "/api/library/others",
  ext: "nds", arch: null, out: "nds", checksum: "md5", total: 0, withCover: 0, generatedAt: "",
};

export default function RomExplorer() {
  const router = useRouter();
  const searchParams = useSearchParams();

  const [systems, setSystems] = useState<SystemInfo[] | null>(null);
  const [systemId, setSystemId] = useState<string | null>(() =>
    searchParams.get("sys"),
  );
  const [data, setData] = useState<Dataset | null>(null);
  const [error, setError] = useState<string | null>(null);

  const [query, setQuery] = useState(() => searchParams.get("q") ?? "");
  const [filters, setFilters] = useState<FilterState>(() => {
    const cover = searchParams.get("cover");
    return {
      region: searchParams.get("region") ?? "all",
      type: searchParams.get("type") ?? "all",
      lang: searchParams.get("lang") ?? "all",
      cover: cover === "with" || cover === "without" ? cover : "all",
    };
  });
  const [sort, setSort] = useState<SortKey>(() => {
    const value = searchParams.get("sort");
    return SORT_OPTIONS.some((option) => option.key === value)
      ? (value as SortKey)
      : "title-asc";
  });
  const [page, setPage] = useState(1);
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [queueCount, setQueueCount] = useState(0);
  const [detailSuppressed, setDetailSuppressed] = useState(false);
  const [deletingRom, setDeletingRom] = useState<Rom | null>(null);
  const [libraryNotice, setLibraryNotice] = useState<string | null>(null);
  const closeDeletion = useCallback(() => setDeletingRom(null), []);

  useEffect(() => onQueueOpen(() => {
    setDetailSuppressed(true);
    const params = new URLSearchParams(searchParams.toString());
    if (!params.has("rom")) return;
    params.delete("rom");
    const qs = params.toString();
    router.replace(qs ? `/?${qs}` : "/", { scroll: false });
  }), [router, searchParams]);

  useEffect(() => onDevicesOpen(() => {
    setDetailSuppressed(true);
    const params = new URLSearchParams(searchParams.toString());
    if (!params.has("rom")) return;
    params.delete("rom");
    const qs = params.toString();
    router.replace(qs ? `/?${qs}` : "/", { scroll: false });
  }), [router, searchParams]);

  useEffect(() => {
    let active = true;
    const update = () => {
      void loadQueue().then((snapshot) => {
        if (active) setQueueCount(snapshot.stats.pending);
      }).catch(() => {});
    };
    update();
    const unsubscribe = onQueueChange(update);
    const timer = window.setInterval(update, 5000);
    return () => { active = false; unsubscribe(); window.clearInterval(timer); };
  }, []);

  useEffect(() => {
    let active = true;
    fetch("/data/systems.json")
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json() as Promise<SystemInfo[]>;
      })
      .then((list) => {
        if (!active || list.length === 0) return;
        const all = [...list, OTHERS_SYSTEM];
        setSystems(all);
        setSystemId((prev) =>
          prev && (prev === "dsidrop" || all.some((system) => system.id === prev))
            ? prev
            : list[0].id,
        );
        void fetch("/api/library/others", { cache: "no-store" }).then((response) => {
          if (!response.ok) throw new Error("Upload library unavailable");
          return response.json() as Promise<Dataset>;
        }).then((library) => {
          if (active) setSystems((current) => current?.map((system) => system.id === "others" ? { ...system, total: library.meta.total, generatedAt: library.meta.generatedAt } : system) ?? null);
        }).catch(() => {});
      })
      .catch((err) => {
        if (active) setError(err instanceof Error ? err.message : "Unknown error");
      });
    return () => {
      active = false;
    };
  }, []);

  const activeSystem = useMemo(() => {
    if (!systems || systems.length === 0 || systemId === "dsidrop") return null;
    return systems.find((system) => system.id === systemId) ?? systems[0];
  }, [systems, systemId]);

  useEffect(() => {
    if (!activeSystem) return;
    let active = true;
    fetch(activeSystem.data)
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json() as Promise<Dataset>;
      })
      .then((json) => {
        if (active) setData(json);
      })
      .catch((err) => {
        if (active) setError(err instanceof Error ? err.message : "Unknown error");
      });
    return () => {
      active = false;
    };
  }, [activeSystem]);

  const selectSystem = useCallback(
    (id: string) => {
      if (id === systemId) return;
      setData(null);
      setError(null);
      setQuery("");
      setFilters(EMPTY_FILTERS);
      setPage(1);
      setLibraryNotice(null);
      setSystemId(id);
      const params = new URLSearchParams(searchParams.toString());
      params.set("sys", id);
      for (const key of ["rom", "q", "region", "type", "lang", "cover"]) params.delete(key);
      router.replace(`/?${params.toString()}`, { scroll: false });
    },
    [systemId, router, searchParams],
  );

  const openRom = useCallback(
    (rom: Rom | null) => {
      setDetailSuppressed(false);
      const params = new URLSearchParams(searchParams.toString());
      if (rom) params.set("rom", rom.m);
      else params.delete("rom");
      const qs = params.toString();
      router.replace(qs ? `/?${qs}` : "/", { scroll: false });
    },
    [router, searchParams],
  );

  const normalizedQuery = normalizeQuery(query);

  const filtered = useMemo(() => {
    if (!data) return [] as Rom[];
    const terms = normalizedQuery.split(/\s+/).filter(Boolean);
    return data.roms.filter((rom) => {
      if (filters.region !== "all" && rom.r !== filters.region) return false;
      if (filters.type !== "all" && romCategory(rom) !== filters.type) return false;
      if (filters.lang !== "all" && !(rom.l ?? "").split(",").includes(filters.lang))
        return false;
      if (filters.cover === "with" && !rom.c) return false;
      if (filters.cover === "without" && rom.c) return false;
      if (terms.length) {
        const haystack = normalizeQuery(searchIndex(rom));
        for (const term of terms) {
          if (!haystack.includes(term)) return false;
        }
      }
      return true;
    });
  }, [data, filters, normalizedQuery]);

  const sorted = useMemo(() => sortRoms(filtered, sort), [filtered, sort]);

  const categoryCounts = useMemo(() => {
    const counts: Record<string, number> = {};
    for (const category of CATEGORY_ORDER) counts[category] = 0;
    if (!data) return counts;
    for (const rom of data.roms) {
      const key = romCategory(rom);
      counts[key] = (counts[key] ?? 0) + 1;
    }
    return counts;
  }, [data]);

  const totalPages = Math.max(1, Math.ceil(sorted.length / PAGE_SIZE));
  const safePage = Math.min(page, totalPages);
  const pageRoms = sorted.slice((safePage - 1) * PAGE_SIZE, safePage * PAGE_SIZE);

  const activeFilterCount =
    (filters.region !== "all" ? 1 : 0) +
    (filters.type !== "all" ? 1 : 0) +
    (filters.lang !== "all" ? 1 : 0) +
    (filters.cover !== "all" ? 1 : 0);

  const selected = useMemo(() => {
    const id = searchParams.get("rom");
    if (!id || !data) return null;
    return data.roms.find((rom) => rom.m === id) ?? null;
  }, [data, searchParams]);

  const siblings = useMemo(() => {
    if (!selected || !data) return [] as Rom[];
    return data.roms.filter((rom) => rom.t === selected.t && rom.m !== selected.m);
  }, [data, selected]);

  const setFilter = (patch: Partial<FilterState>) => {
    setFilters((prev) => ({ ...prev, ...patch }));
    setPage(1);
  };

  const resetFilters = () => {
    setFilters(EMPTY_FILTERS);
    setPage(1);
  };

  const randomRom = () => {
    if (!sorted.length) return;
    const rom = sorted[Math.floor(Math.random() * sorted.length)];
    openRom(rom);
  };

  const uploaded = (library: Dataset) => {
    setLibraryNotice(null);
    setError(null);
    setData(library);
    setQuery(""); setFilters({ ...EMPTY_FILTERS, type: filters.type === "Multimedia" ? "Multimedia" : "all" }); setPage(1);
    setSystems((current) => current?.map((system) => system.id === "others" ? { ...system, total: library.meta.total, generatedAt: library.meta.generatedAt } : system) ?? null);
  };

  const deleted = (library: Dataset) => {
    const title = deletingRom?.t ?? "Game";
    if (searchParams.get("rom") === deletingRom?.m) openRom(null);
    setDeletingRom(null);
    setData(library);
    setLibraryNotice(`${title} was deleted from Others.`);
    setSystems((current) => current?.map((system) => system.id === "others" ? { ...system, total: library.meta.total, generatedAt: library.meta.generatedAt } : system) ?? null);
  };

  return (
    <div className="library-shell">
      <header className="app-header">
        <div className="app-header-inner">
          <Link href="/" aria-label="DSiDrop home" className="brand-plate">
            <Image src="/brand/dsidrop-logo.png" alt="DSiDrop" width={2170} height={725} sizes="190px" className="h-auto w-[176px] sm:w-[190px]" preload />
          </Link>
          <nav aria-label="Main navigation" className="header-nav">
            <a href="#library" className="header-library"><Icon name="grid" className="h-4 w-4" />Library</a>
            <DevicesButton />
            <CollectionsButton />
            <button type="button" onClick={openQueuePanel} className="header-button">
              <Icon name="queue" className="h-4 w-4" /><span>Queue</span>
              {queueCount > 0 ? <span className="queue-badge">{queueCount}</span> : null}
            </button>

          </nav>
          <a className="get-app" href="/downloads/DSiDrop.nds" download="DSiDrop.nds"><Icon name="download" className="h-4 w-4" /><span>Get DSi app</span></a>
        </div>
      </header>

      <div className="library-content">
        {systemId !== "dsidrop" ? <section className="library-intro" aria-labelledby="library-heading">
          <div>
            <p className="eyebrow"><span className="brand-dot" />{systemId === "dsidrop" ? "MEET YOUR HANDHELD COMPANION" : systemId === "others" ? "YOUR UPLOAD LIBRARY" : "THE COLLECTION"}</p>
            <h1 id="library-heading">{systemId === "dsidrop" ? <>Your library. <span>On your DSi.</span></> : systemId === "others" ? <>Bring your <span>own favorites.</span></> : <>Find your next <span>favorite.</span></>}</h1>
            <p className="intro-description">{systemId === "others" ? "Upload once on your server. Browse and download on your DSi." : systemId === "dsidrop" ? "Everything you need to get DSiDrop up and running." : "A new discovery. An old favorite. Your next game is here."}</p>
          </div>
          <div className="library-summary">
            <Stat label="games to explore" value={systems ? systems.reduce((total, system) => total + system.total, 0).toLocaleString("en-US") : "—"} />
            <span className="summary-divider" />
            <Stat label="classic systems" value={systems ? String(systems.filter((system) => system.id !== "others").length).padStart(2, "0") : "—"} />
          </div>
        </section> : null}

        <section id="library" className="catalog-section" aria-label="Game library">
          <div className="system-bar">
            <div role="group" aria-label="Choose a system" className="system-tabs">
              <SystemPicker systems={(systems ?? []).filter((system) => system.id !== "others")} selectedId={activeSystem?.id} onSelect={selectSystem} />
              {(systems ?? []).filter((system) => system.id === "others").map((system) => {
                const active = system.id === activeSystem?.id;
                return <button key={system.id} type="button" onClick={() => selectSystem(system.id)} aria-pressed={active} className={`system-tab ${system.id === "others" ? "personal-system-tab" : ""} ${active ? "is-active" : ""}`}>
                  <span>{system.short}</span><span className="system-count">{system.total.toLocaleString("en-US")}</span>
                </button>;
              })}
              <button type="button" onClick={() => selectSystem("dsidrop")} aria-pressed={systemId === "dsidrop"} className={`system-tab ${systemId === "dsidrop" ? "is-active" : ""}`}>DSiDrop<Icon name="download" className="h-3 w-3 text-brand-400" /></button>
            </div>
            <span className="catalog-label">{systemId === "dsidrop" ? "Latest app" : activeSystem?.label ?? "Loading collection"}</span>
          </div>

          {systemId === "dsidrop" ? <DSiDropPanel /> : <>
          {systemId === "others" ? <div role="group" aria-label="Others category" className="flex flex-wrap gap-1 border-b border-white/10 py-3">
            {[["all", "All"], ["Apps", "Apps"], ["Uploaded", "Your uploads"], ["Multimedia", "Multimedia"], ["Themes", "Themes"]].map(([value, label]) => <button key={value} type="button" aria-pressed={filters.type === value} onClick={() => setFilter({ type: value })} className={`inline-flex h-9 items-center gap-2 rounded-md px-4 text-xs font-medium transition ${filters.type === value ? "bg-brand-500/20 text-brand-400" : "text-white/55 hover:bg-white/5 hover:text-white"}`}><Icon name={value === "Apps" ? "grid" : value === "Uploaded" ? "plus" : value === "Multimedia" ? "play" : "filter"} className="size-3.5" />{label}{value !== "Themes" ? <span className="text-[10px] tabular-nums opacity-60">{value === "all" ? data?.meta.total ?? 0 : categoryCounts[value] ?? 0}</span> : null}</button>)}
          </div> : null}
          {systemId === "others" && filters.type === "Themes" ? <ThemeExplorer /> : <>
          {systemId === "others" ? <UploadLibrary key={filters.type === "Multimedia" ? "media" : "nds"} onUploaded={uploaded} multimedia={filters.type === "Multimedia"} /> : null}
          {systemId === "others" && libraryNotice ? <div role="status" className="mt-4 flex items-center gap-3 rounded-xl border border-emerald-400/15 bg-emerald-400/5 px-4 py-3 text-xs text-emerald-200"><Icon name="check" className="size-4 shrink-0" /><span className="min-w-0 flex-1 break-words">{libraryNotice}</span><button type="button" aria-label="Dismiss deletion notice" onClick={() => setLibraryNotice(null)} className="p-1 text-white/40 hover:text-white"><Icon name="close" className="size-4" /></button></div> : null}
          <div className="catalog-toolbar">
            <div className="search-field">
              <Icon name="search" className="h-5 w-5 shrink-0 text-white/40" />
              <input type="search" value={query} onChange={(event) => { setQuery(event.target.value); setPage(1); }} aria-label="Search games" placeholder={`Search ${activeSystem?.label ?? "the collection"} games…`} />
              {query ? <button type="button" aria-label="Clear search" onClick={() => { setQuery(""); setPage(1); }}><Icon name="close" className="h-4 w-4" /></button> : <kbd className="hidden sm:block">Search</kbd>}
            </div>
            <div className="toolbar-actions">
              <button type="button" aria-expanded={sidebarOpen} aria-controls="catalog-filters" onClick={() => setSidebarOpen((value) => !value)} className="toolbar-button mobile-filter-toggle"><Icon name="filter" className="h-4 w-4" />Filters{activeFilterCount ? ` (${activeFilterCount})` : ""}</button>
              <label className="sort-select"><span className="hidden sm:inline">Sort by</span><select value={sort} aria-label="Sort games" onChange={(event) => { setSort(event.target.value as SortKey); setPage(1); }}>{SORT_OPTIONS.map((option) => <option key={option.key} value={option.key}>{option.label}</option>)}</select></label>
              <button type="button" onClick={randomRom} disabled={!sorted.length} aria-label="Discover a random game" title="Discover a random game" className="toolbar-button shuffle-button"><Icon name="shuffle" className="h-4 w-4" /><span className="hidden xl:inline">Surprise me</span></button>
            </div>
          </div>

          <div className="catalog-layout">
            <aside id="catalog-filters" className={`${sidebarOpen ? "block" : "hidden"} min-w-0 lg:block`}>
              {data ? <div className="lg:sticky lg:top-6"><FilterPanel meta={data.meta} filters={filters} onChange={setFilter} activeCount={activeFilterCount} onReset={resetFilters} categoryCounts={categoryCounts} /></div> : null}
              <div className="companion-card hidden lg:block">
                <p className="eyebrow">TAKE IT WITH YOU</p>
                <h3>Made for your DSi.</h3>
                <p>Browse the catalog and download directly to your SD card with the DSiDrop app.</p>
                <a href="/downloads/DSiDrop.nds" download="DSiDrop.nds">Get the app<Icon name="arrow" className="h-4 w-4" /></a>
              </div>
            </aside>

            <section className="min-w-0" aria-label="Search results" aria-busy={!data && !error}>
              {error ? <div role="alert" className="rounded-2xl border border-rose-500/30 bg-rose-500/10 p-6 text-sm text-rose-200">{systemId === "others" ? <>Your upload library could not be loaded: {error}. Check that the PC server can access the upload folder, then refresh the page.</> : <>The collection could not be loaded: {error}. Build the catalog with <code className="rounded bg-black/30 px-1.5 py-0.5 font-mono">npm run index</code>, then refresh the page.</>}</div> : !data ? <SkeletonGrid /> : <>
                <div className="results-heading">
                  <h2><strong>{sorted.length.toLocaleString("en-US")}</strong> {sorted.length === 1 ? "game" : "games"}{normalizedQuery ? <> matching <span className="text-white/80">“{query}”</span></> : <span className="text-white/35"> · {activeSystem?.label}</span>}</h2>
                  <div className="flex items-center gap-4">{activeFilterCount || query ? <button type="button" className="text-xs font-medium text-brand-400 hover:text-white" onClick={() => { resetFilters(); setQuery(""); }}>Clear all</button> : null}<span className="text-xs text-white/35">Page {safePage} of {totalPages}</span></div>
                </div>
                {pageRoms.length === 0 ? <div className="empty-results"><span className="empty-results-icon"><Icon name={systemId === "others" && !data.roms.length ? "plus" : "search"} className="h-7 w-7" /></span><h3>{systemId === "others" && !data.roms.length ? "Your collection starts here." : "No games found"}</h3><p>{systemId === "others" && !data.roms.length ? "Use Upload NDS files above to add your first game." : "Try a different title or loosen your filters."}</p>{data.roms.length ? <button type="button" onClick={() => { resetFilters(); setQuery(""); }} className="toolbar-button mt-5">Reset search</button> : null}</div> : <div className="game-grid">{pageRoms.map((rom) => <RomCard key={`${rom.m}-${rom.n}`} meta={data.meta} rom={rom} onOpen={() => openRom(rom)} onDelete={data.meta.id === "others" && !rom.g?.includes("Apps") ? () => setDeletingRom(rom) : undefined} />)}</div>}
                {totalPages > 1 ? <Pagination page={safePage} totalPages={totalPages} onPage={(next) => { setPage(next); document.getElementById("library")?.scrollIntoView({ behavior: "smooth", block: "start" }); }} /> : null}
              </>}
            </section>
          </div>
          </>}
          </>}
        </section>
      </div>
      {selected && data && !detailSuppressed && !deletingRom ? <RomDetail meta={data.meta} rom={selected} siblings={siblings} onClose={() => openRom(null)} onSelect={(rom) => openRom(rom)} onDelete={data.meta.id === "others" && !selected.g?.includes("Apps") ? () => setDeletingRom(selected) : undefined} /> : null}
      {deletingRom ? <DeleteUploadDialog rom={deletingRom} onClose={closeDeletion} onDeleted={deleted} /> : null}
      <CollectionsPanel />
    </div>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return <div><p className="stat-value">{value}</p><p className="stat-label">{label}</p></div>;
}

function Pagination({
  page,
  totalPages,
  onPage,
}: {
  page: number;
  totalPages: number;
  onPage: (page: number) => void;
}) {
  const pages = pageWindow(page, totalPages);
  return (
    <nav aria-label="Catalog pages" className="mt-8 flex flex-wrap items-center justify-center gap-1.5">
      <PageButton disabled={page <= 1} onClick={() => onPage(page - 1)}>
        ‹ Previous
      </PageButton>
      {pages.map((entry, index) =>
        entry === "…" ? (
          <span key={`gap-${index}`} className="px-2 text-white/30">
            …
          </span>
        ) : (
          <PageButton
            key={entry}
            active={entry === page}
            onClick={() => onPage(entry)}
          >
            {entry}
          </PageButton>
        ),
      )}
      <PageButton disabled={page >= totalPages} onClick={() => onPage(page + 1)}>
        Next ›
      </PageButton>
    </nav>
  );
}

function PageButton({
  children,
  active,
  disabled,
  onClick,
}: {
  children: React.ReactNode;
  active?: boolean;
  disabled?: boolean;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      disabled={disabled}
      aria-current={active ? "page" : undefined}
      onClick={onClick}
      className={`min-w-9 rounded-lg px-3 py-1.5 text-sm font-medium transition ${
        active
          ? "bg-brand-500 text-white"
          : "border border-white/10 text-white/70 hover:bg-white/5"
      } disabled:cursor-not-allowed disabled:opacity-40`}
    >
      {children}
    </button>
  );
}

function pageWindow(page: number, totalPages: number): (number | "…")[] {
  const result: (number | "…")[] = [];
  const push = (value: number | "…") => result.push(value);
  const range = 1;
  const from = Math.max(2, page - range);
  const to = Math.min(totalPages - 1, page + range);

  push(1);
  if (from > 2) push("…");
  for (let i = from; i <= to; i++) push(i);
  if (to < totalPages - 1) push("…");
  if (totalPages > 1) push(totalPages);
  return result;
}

function SkeletonGrid() {
  return (
    <div className="game-grid">
      {Array.from({ length: 10 }).map((_, index) => (
        <div
          key={index}
          className="animate-pulse overflow-hidden rounded-xl border border-white/5 bg-ink-850/60"
        >
          <div className="aspect-[10/9] w-full bg-white/5" />
          <div className="space-y-2 p-3">
            <div className="h-3 w-4/5 rounded bg-white/10" />
            <div className="h-3 w-2/5 rounded bg-white/5" />
          </div>
        </div>
      ))}
    </div>
  );
}
