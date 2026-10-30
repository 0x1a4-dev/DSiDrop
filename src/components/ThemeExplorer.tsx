"use client";

import { useEffect, useRef, useState } from "react";
import { THEME_STYLES, type MenuTheme } from "@/lib/theme-types";
import { openDevices } from "@/lib/device-client";
import Icon from "./Icon";

function Preview({ theme, shot = 0 }: { theme: MenuTheme; shot?: number }) {
  const [failed, setFailed] = useState(false);
  return <div className="theme-preview">{theme.screenshots[shot] && !failed
    // eslint-disable-next-line @next/next/no-img-element
    ? <img src={`/api/themes/preview?id=${theme.id}&shot=${shot}`} alt={`${theme.title}: ${theme.screenshots[shot].description}`} loading="lazy" onError={() => setFailed(true)} />
    : <span className="text-xs text-white/40">No preview available</span>}</div>;
}

export default function ThemeExplorer() {
  const [themes, setThemes] = useState<MenuTheme[] | null>(null);
  const [favorites, setFavorites] = useState<string[]>([]);
  const [query, setQuery] = useState("");
  const [style, setStyle] = useState("all");
  const [onlyFavorites, setOnlyFavorites] = useState(false);
  const [selected, setSelected] = useState<MenuTheme | null>(null);
  const [shot, setShot] = useState(0);
  const [page, setPage] = useState(0);
  const [pending, setPending] = useState<string | null>(null);
  const [error, setError] = useState("");
  const dialog = useRef<HTMLDialogElement>(null);

  useEffect(() => {
    const controller = new AbortController();
    fetch("/api/themes", { signal: controller.signal, cache: "no-store" }).then(async (response) => {
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "Theme catalog unavailable");
      setThemes(data.themes); setFavorites(data.favorites);
    }).catch((error: Error) => { if (!controller.signal.aborted) setError(error.message); });
    return () => controller.abort();
  }, []);

  useEffect(() => {
    if (selected) dialog.current?.showModal();
    else dialog.current?.close();
  }, [selected]);

  async function favorite(theme: MenuTheme) {
    if (pending) return;
    setPending(theme.id); setError("");
    try {
      const response = await fetch("/api/themes/favorite", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ id: theme.id, favorite: !favorites.includes(theme.id) }) });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "Could not save favorite");
      setFavorites(data.favorites);
    } catch (error) { setError(error instanceof Error ? error.message : "Could not save favorite"); }
    finally { setPending(null); }
  }

  const filtered = (themes || []).filter((theme) => (style === "all" || theme.style === style)
    && (!onlyFavorites || favorites.includes(theme.id))
    && query.toLowerCase().split(/\s+/).every((word) => `${theme.title} ${theme.author} ${theme.description}`.toLowerCase().includes(word)));
  const pages = Math.max(1, Math.ceil(filtered.length / 24));
  const currentPage = Math.min(page, pages - 1);

  function send(theme: MenuTheme) { setSelected(null); openDevices({ items: [{ system: "others", id: theme.id }], label: theme.title }); }

  return <section className="theme-explorer" aria-label="TWiLight Menu++ themes">
    <div className="flex flex-wrap items-center justify-between gap-3 py-5"><h2 className="text-lg font-semibold">TWiLight Menu++ Themes</h2><a href="https://skins.ds-homebrew.com/" target="_blank" rel="noreferrer" className="text-xs text-brand-400">DS-Homebrew<Icon name="arrow" className="ml-1 inline size-3" /></a></div>
    <div className="catalog-toolbar !mt-0"><div className="search-field"><Icon name="search" className="size-4 shrink-0" /><input type="search" aria-label="Search themes" placeholder="Search themes" value={query} onChange={(event) => { setQuery(event.target.value); setPage(0); }} /></div>
      <label className="sort-select">Menu style<select aria-label="Theme menu style" value={style} onChange={(event) => { setStyle(event.target.value); setPage(0); }}><option value="all">All styles</option>{Object.entries(THEME_STYLES).map(([key, label]) => <option key={key} value={key}>{label}</option>)}</select></label>
      <label className="flex items-center gap-2 text-xs text-white/70"><input type="checkbox" checked={onlyFavorites} onChange={(event) => { setOnlyFavorites(event.target.checked); setPage(0); }} />Favorites ({favorites.length})</label>
    </div>
    {error ? <p role="alert" className="py-3 text-sm text-rose-300">{error}</p> : null}
    <div className="results-heading"><h3>{themes ? `${filtered.length} themes` : "Loading themes..."}</h3><span className="text-xs text-white/40">{currentPage + 1} / {pages}</span></div>
    <div className="theme-grid">{filtered.slice(currentPage * 24, (currentPage + 1) * 24).map((theme) => <article key={theme.id} className="theme-card">
      <button type="button" className="block w-full" onClick={() => { setShot(0); setSelected(theme); }} aria-label={`Preview ${theme.title}`}><Preview theme={theme} /></button>
      <div className="p-3"><p className="text-[10px] text-brand-400">{THEME_STYLES[theme.style]}</p><button type="button" onClick={() => { setShot(0); setSelected(theme); }} className="my-1 block w-full break-words text-left text-sm font-semibold">{theme.title}</button><p className="truncate text-xs text-white/40" title={theme.author}>{theme.author}</p>
        <div className="mt-3 flex items-center justify-between"><button type="button" title={favorites.includes(theme.id) ? "Remove favorite" : "Save favorite"} aria-label={`Favorite ${theme.title}`} aria-pressed={favorites.includes(theme.id)} disabled={!!pending} onClick={() => void favorite(theme)} className="toolbar-button !rounded-md"><Icon name={favorites.includes(theme.id) ? "check" : "plus"} className="size-4" /></button><button type="button" className="toolbar-button !rounded-md" onClick={() => send(theme)}><Icon name="device" className="size-4" />Install on DSi</button></div>
      </div>
    </article>)}</div>
    {themes && !filtered.length ? <p className="py-12 text-center text-sm text-white/50">No themes found.</p> : null}
    {pages > 1 ? <nav aria-label="Theme pages" className="flex items-center justify-center gap-4 py-6"><button type="button" className="toolbar-button" disabled={!currentPage} onClick={() => setPage(currentPage - 1)}>Previous</button><span className="text-xs">{currentPage + 1} / {pages}</span><button type="button" className="toolbar-button" disabled={currentPage + 1 >= pages} onClick={() => setPage(currentPage + 1)}>Next</button></nav> : null}
    <dialog ref={dialog} className="theme-dialog" onCancel={() => setSelected(null)} onClose={() => setSelected(null)} onClick={(event) => { if (event.target === event.currentTarget) setSelected(null); }}>
      {selected ? <><div className="flex items-start justify-between gap-4 border-b border-white/10 p-4"><div className="min-w-0"><h3 className="break-words text-lg font-semibold">{selected.title}</h3><p className="text-xs text-white/50">{THEME_STYLES[selected.style]} / {selected.author} / {selected.version}</p></div><button type="button" className="toolbar-button shrink-0" aria-label="Close preview" onClick={() => setSelected(null)}><Icon name="close" className="size-4" /></button></div>
        <div className="theme-detail"><div><Preview key={`${selected.id}:${shot}`} theme={selected} shot={shot} />{selected.screenshots.length > 1 ? <select aria-label="Theme screenshot" className="mt-3 w-full rounded-md border border-white/15 bg-ink-900 p-2 text-xs" value={shot} onChange={(event) => setShot(Number(event.target.value))}>{selected.screenshots.map((screen, index) => <option key={index} value={index}>{screen.description}</option>)}</select> : null}</div>
          <div className="min-w-0 space-y-5"><p className="whitespace-pre-line break-words text-sm text-white/65">{selected.description}</p><div><h4 className="mb-2 text-xs text-white/40">SD destination</h4><p className="break-all font-mono text-xs text-brand-300">{selected.destination}/</p></div><button type="button" className="toolbar-button" onClick={() => void favorite(selected)} disabled={!!pending}><Icon name={favorites.includes(selected.id) ? "check" : "plus"} className="size-4" />{favorites.includes(selected.id) ? "Saved favorite" : "Save favorite"}</button><button type="button" className="toolbar-button w-full !border-brand-400/40 !text-brand-300" onClick={() => send(selected)}><Icon name="device" className="size-4" />Install on DSi</button><a className="inline-flex items-center gap-2 text-xs text-white/40" href={selected.archive}><Icon name="download" className="size-3" />Original archive .7z</a></div>
        </div></> : null}
    </dialog>
  </section>;
}