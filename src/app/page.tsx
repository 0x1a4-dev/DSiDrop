import { Suspense } from "react";

import QueuePanel from "@/components/QueuePanel";
import RomExplorer from "@/components/RomExplorer";
import DevicePanel from "@/components/DevicePanel";

export default function Home() {
  return (
    <main className="flex-1">
      <Suspense fallback={<ExplorerFallback />}>
        <RomExplorer />
      </Suspense>
      <QueuePanel />
      <DevicePanel />
      <footer className="app-footer">
        <span className="font-semibold text-white/65">DSiDrop</span>
        <p>Catalog: <a href="https://archive.org" target="_blank" rel="noopener noreferrer">Internet Archive</a><span className="mx-2 text-white/20">/</span>Cover art: <a href="https://github.com/libretro-thumbnails" target="_blank" rel="noopener noreferrer">libretro-thumbnails</a></p>
        <span>For preservation &amp; discovery.</span>
      </footer>
    </main>
  );
}

function ExplorerFallback() {
  return (
    <div className="mx-auto w-full max-w-[1536px] px-4 py-8 sm:px-8" role="status" aria-label="Loading DSiDrop">
      <div className="mb-8 h-64 animate-pulse rounded-3xl border border-white/5 bg-ink-850/50" />
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
    </div>
  );
}
