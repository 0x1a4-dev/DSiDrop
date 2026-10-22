"use client";

import { useEffect, useRef, useState } from "react";
import Icon from "./Icon";
import { formatBytes } from "@/lib/roms";
import type { Dataset } from "@/lib/types";

type UploadResult = { uploaded: number; duplicates: number; dataset: Dataset };
const MAX_FILE = 512 * 1024 * 1024;
const MAX_BATCH = 1024 * 1024 * 1024;

export default function UploadLibrary({ onUploaded }: { onUploaded: (data: Dataset) => void }) {
  const input = useRef<HTMLInputElement>(null);
  const request = useRef<XMLHttpRequest | null>(null);
  const [dragging, setDragging] = useState(false);
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState(0);
  const [summary, setSummary] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [batch, setBatch] = useState({ count: 0, size: 0 });

  useEffect(() => () => request.current?.abort(), []);

  const upload = (files: File[]) => {
    if (!files.length || request.current) return;
    setError(null); setSummary(null);
    if (files.length > 20) { setError("Choose up to 20 NDS files at a time."); return; }
    if (files.some((file) => !/\.nds$/i.test(file.name))) { setError("Only .nds files can be added to Others. ZIP and 7z archives must be extracted first."); return; }
    if (files.some((file) => file.size > MAX_FILE)) { setError("Each NDS file must be 512 MB or smaller."); return; }
    const size = files.reduce((sum, file) => sum + file.size, 0);
    if (size > MAX_BATCH) { setError("Choose a smaller batch. The total upload limit is 1 GB."); return; }
    const form = new FormData();
    for (const file of files) form.append("files", file);
    const xhr = new XMLHttpRequest();
    request.current = xhr;
    setBatch({ count: files.length, size }); setBusy(true); setProgress(0);
    xhr.open("POST", "/api/library/others");
    xhr.responseType = "json";
    xhr.upload.onprogress = (event) => { if (event.lengthComputable) setProgress(Math.min(100, Math.round(event.loaded / event.total * 100))); };
    xhr.onload = () => {
      const result = xhr.response as (UploadResult & { error?: string }) | null;
      if (xhr.status >= 200 && xhr.status < 300 && result?.dataset) {
        onUploaded(result.dataset);
        setSummary(`${result.uploaded} ${result.uploaded === 1 ? "game added" : "games added"}${result.duplicates ? ` · ${result.duplicates} already in your library` : ""}. Ready in Others on your DSi.`);
      } else setError(result?.error ?? `Upload failed (HTTP ${xhr.status}). Please try again.`);
    };
    xhr.onerror = () => setError("The PC server could not be reached. Check your connection and try again.");
    xhr.onabort = () => setSummary("Upload canceled. Refresh the library to check for any files that finished just before cancellation.");
    xhr.onloadend = () => { request.current = null; setBusy(false); if (input.current) input.current.value = ""; };
    xhr.send(form);
  };

  return (
    <section aria-labelledby="upload-heading" className={`upload-library ${dragging ? "is-dragging" : ""}`} onDragOver={(event) => { event.preventDefault(); if (!busy) setDragging(true); }} onDragLeave={(event) => { if (!(event.relatedTarget instanceof Node) || !event.currentTarget.contains(event.relatedTarget)) setDragging(false); }} onDrop={(event) => { event.preventDefault(); setDragging(false); upload([...event.dataTransfer.files]); }}>
      <div className="upload-library-heading">
        <span className="upload-symbol"><Icon name="download" className="h-5 w-5 rotate-180" /></span>
        <div><p className="eyebrow">YOUR OWN COLLECTION</p><h2 id="upload-heading">Your files. Ready for your DSi.</h2><p>Add your own NDS games. They appear in Others on both the web and your DSi.</p></div>
      </div>
      <div className="upload-library-action">
        <input ref={input} type="file" accept=".nds" multiple aria-label="Choose NDS files to upload" className="sr-only" disabled={busy} onChange={(event) => upload([...event.target.files ?? []])} />
        <button type="button" onClick={() => input.current?.click()} disabled={busy} className="upload-primary"><Icon name="plus" className="h-4 w-4" />{busy ? "Uploading…" : "Upload NDS files"}</button>
        <p>Drop .nds files here · Up to 512 MB each</p>
      </div>
      {busy ? <div className="upload-progress"><div className="flex items-center justify-between gap-3"><p>{progress === 100 ? "Verifying and adding your games…" : `Uploading ${batch.count} ${batch.count === 1 ? "file" : "files"} · ${formatBytes(batch.size)}`}<span className="ml-2 font-mono text-brand-300">{progress}%</span></p><button type="button" onClick={() => request.current?.abort()} className="text-xs text-white/60 hover:text-white">Cancel</button></div><div role="progressbar" aria-label="NDS upload progress" aria-valuenow={progress} aria-valuemin={0} aria-valuemax={100} className="mt-3 h-1.5 overflow-hidden rounded-full bg-white/10"><div className="h-full bg-brand-400 transition-[width]" style={{ width: `${progress}%` }} /></div></div> : null}
      {summary ? <p role="status" className="upload-notice text-emerald-300"><Icon name="check" className="h-4 w-4 shrink-0" />{summary}</p> : null}
      {error ? <p role="alert" className="upload-notice text-rose-300"><Icon name="close" className="h-4 w-4 shrink-0" />{error}</p> : null}
    </section>
  );
}
