import { createHash } from "node:crypto";
import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import path from "node:path";
import { THEME_STYLES, type MenuTheme, type ThemeStyle } from "../theme-types";
import { ROMS_DIR } from "./rom-file";

const SOURCE = "https://raw.githubusercontent.com/DS-Homebrew/twlmenu-extras/master/";
const DIRECTORY = path.join(ROMS_DIR, ".themes");
const FAVORITES = path.join(DIRECTORY, "favorites.json");
let cached: { themes: MenuTheme[]; time: number } | undefined;
let loading: Promise<MenuTheme[]> | undefined;
let writing: Promise<unknown> = Promise.resolve();

export function themePath(value: string): string {
  if (!value || value.length > 220 || /[\x00-\x1f\x7f\\:<>"|?*]/.test(value)) throw new Error("Invalid theme path");
  if (value.split("/").some((part) => !part || part === "." || part === ".." || /[. ]$/.test(part)
    || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part))) throw new Error("Unsafe theme path");
  return value;
}

function sourceUrl(value: unknown): string {
  if (typeof value !== "string") throw new Error("Invalid theme source");
  const url = new URL(value);
  if (!url.href.startsWith(SOURCE) || url.username || url.password || url.search || url.hash) throw new Error("Unsupported theme source");
  themePath(decodeURIComponent(url.pathname.slice("/DS-Homebrew/twlmenu-extras/master/".length)));
  return url.href;
}

export function parseThemes(value: unknown): MenuTheme[] {
  const content = (value as { storeContent?: unknown[] })?.storeContent;
  if (!Array.isArray(content)) throw new Error("Invalid theme catalog");
  const themes = new Map<string, MenuTheme>();
  for (const raw of content) {
    try {
      const entry = raw as Record<string, unknown>;
      const info = entry.info as Record<string, unknown>;
      const commands = Object.entries(entry).find(([key, commands]) => key !== "info" && Array.isArray(commands))?.[1] as Record<string, unknown>[];
      if (!info || !commands) continue;
      const download = commands.find((command) => command.type === "downloadFile");
      const extract = commands.find((command) => command.type === "extractFile");
      if (!download || !extract || extract.file !== download.output || typeof extract.output !== "string" || typeof extract.input !== "string") continue;
      const destination = themePath(extract.output.replace(/^\//, "").replace(/\/$/, ""));
      const match = /^_nds\/TWiLightMenu\/(dsimenu|3dsmenu|r4menu|akmenu)\/themes\/([^/]+)$/.exec(destination);
      if (!match) continue;
      const style = match[1] as ThemeStyle;
      if (info.console !== THEME_STYLES[style]) continue;
      const archive = sourceUrl(download.file);
      if (!decodeURIComponent(archive).endsWith(`/${destination}.7z`)) continue;
      const input = extract.input.replace(/\/$/, "");
      if (input) themePath(input);
      const id = createHash("md5").update(destination).digest("hex");
      themes.set(id, {
        id, style, destination: `/${destination}`, archive, input,
        title: String(info.title || match[2]).slice(0, 200), author: String(info.author || "Unknown").slice(0, 200),
        description: String(info.description || "").slice(0, 4000), version: String(info.version || "").slice(0, 80),
        screenshots: (Array.isArray(info.screenshots) ? info.screenshots : []).slice(0, 20).flatMap((shot) => {
          try { return [{ url: sourceUrl(shot.url), description: String(shot.description || "Preview").slice(0, 120) }]; }
          catch { return []; }
        }),
      });
    } catch { }
  }
  return [...themes.values()].sort((left, right) => left.title.localeCompare(right.title, "en") || left.id.localeCompare(right.id));
}

export async function themeCatalog(): Promise<MenuTheme[]> {
  if (cached && Date.now() - cached.time < 60 * 60_000) return cached.themes;
  if (loading) return loading;
  loading = (async () => {
    try {
      const response = await fetch(`${SOURCE}unistore/twlmenu-skins.unistore`, { signal: AbortSignal.timeout(30_000), cache: "no-store" });
      if (!response.ok) throw new Error(`Theme source: HTTP ${response.status}`);
      const body = await response.text();
      if (Buffer.byteLength(body) > 8 * 1024 * 1024) throw new Error("Theme catalog too large");
      const themes = parseThemes(JSON.parse(body));
      if (!themes.length) throw new Error("Theme catalog is empty");
      await mkdir(DIRECTORY, { recursive: true });
      await writeFile(path.join(DIRECTORY, "catalog.json"), body);
      cached = { themes, time: Date.now() };
      return themes;
    } catch (error) {
      if (cached) return cached.themes;
      try {
        const themes = parseThemes(JSON.parse(await readFile(path.join(DIRECTORY, "catalog.json"), "utf8")));
        if (themes.length) { cached = { themes, time: Date.now() - 55 * 60_000 }; return themes; }
      } catch { }
      throw error;
    }
  })().finally(() => { loading = undefined; });
  return loading;
}

export async function findTheme(id: string): Promise<MenuTheme | undefined> {
  if (!/^[a-f0-9]{32}$/.test(id)) return undefined;
  return (await themeCatalog()).find((theme) => theme.id === id);
}

export async function themeFavorites(): Promise<string[]> {
  try {
    const data: unknown = JSON.parse(await readFile(FAVORITES, "utf8"));
    return Array.isArray(data) ? data.filter((id) => typeof id === "string" && /^[a-f0-9]{32}$/.test(id)).slice(0, 2000) : [];
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") return [];
    throw error;
  }
}

export function setThemeFavorite(id: string, favorite: boolean): Promise<string[]> {
  const operation = writing.catch(() => {}).then(async () => {
    if (!await findTheme(id)) throw new Error("Theme not found");
    const favorites = new Set(await themeFavorites());
    if (favorite) favorites.add(id); else favorites.delete(id);
    if (favorites.size > 2000) throw new Error("Too many favorites");
    await mkdir(DIRECTORY, { recursive: true });
    await writeFile(`${FAVORITES}.tmp`, JSON.stringify([...favorites]));
    await rename(`${FAVORITES}.tmp`, FAVORITES);
    return [...favorites];
  });
  writing = operation;
  return operation;
}