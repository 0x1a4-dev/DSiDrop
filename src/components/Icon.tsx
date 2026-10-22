import type { SVGProps } from "react";

export type IconName = "search" | "grid" | "download" | "queue" | "settings" | "chevron" | "close" | "check" | "shuffle" | "filter" | "arrow" | "copy" | "play" | "plus" | "device" | "trash";

const paths: Record<IconName, React.ReactNode> = {
  search: <><circle cx="10.5" cy="10.5" r="6.5" /><path d="m16 16 4.5 4.5" /></>,
  grid: <><rect x="3" y="3" width="7" height="7" rx="1.5" /><rect x="14" y="3" width="7" height="7" rx="1.5" /><rect x="3" y="14" width="7" height="7" rx="1.5" /><rect x="14" y="14" width="7" height="7" rx="1.5" /></>,
  download: <><path d="M12 3v12m-5-5 5 5 5-5M4 16v4a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-4" /></>,
  queue: <><path d="M4 5h16M4 12h10M4 19h7m7-4v6m-3-3h6" /></>,
  settings: <><path d="m9 3-.6 2.3-2 .9L4.2 6 2 10l1.6 1.8v2.4L2 16l2.2 4 2.2-.2 2 .9L9 23h6l.6-2.3 2-.9 2.2.2 2.2-4-1.6-1.8v-2.4L22 10l-2.2-4-2.2.2-2-.9L15 3Z" transform="translate(1 0) scale(.9)" /><circle cx="12" cy="12" r="3" /></>,
  chevron: <path d="m9 5 7 7-7 7" />,
  close: <path d="m6 6 12 12M6 18 18 6" />,
  check: <path d="m5 12 4 4L19 6" />,
  shuffle: <><path d="M3 6h3c4 0 8 12 12 12h3m-4-4 4 4-4 4M3 18h3c1.5 0 3-1.8 4.5-4m3-4c1.5-2.2 3-4 4.5-4h3m-4-4 4 4-4 4" /></>,
  filter: <><path d="M3 6h18M3 12h18M3 18h18" /><path d="M8 3v6m8 0v6M9 15v6" /></>,
  arrow: <path d="M4 12h16m-6-6 6 6-6 6" />,
  copy: <><rect x="8" y="8" width="12" height="13" rx="2" /><path d="M16 8V3H3v13h5" /></>,
  play: <path d="m8 4 13 8-13 8Z" />,
  plus: <path d="M12 4v16M4 12h16" />,
  trash: <><path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7M14 10v7" /></>,
  device: <><rect x="5" y="2" width="14" height="8" rx="1.5" /><rect x="4" y="13" width="16" height="9" rx="1.5" /><path d="M8 16v3m-1.5-1.5h3M17 16h.01M16 19h.01" /><rect x="11" y="15" width="3" height="5" rx=".5" /></>,
};

export default function Icon({ name, ...props }: SVGProps<SVGSVGElement> & { name: IconName }) {
  return <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" {...props}>{paths[name]}</svg>;
}
