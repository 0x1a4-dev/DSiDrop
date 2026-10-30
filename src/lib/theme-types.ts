export const THEME_STYLES = {
  dsimenu: "Nintendo DSi",
  "3dsmenu": "Nintendo 3DS",
  r4menu: "R4 Original",
  akmenu: "Wood UI",
} as const;

export type ThemeStyle = keyof typeof THEME_STYLES;
export type MenuTheme = {
  id: string;
  title: string;
  author: string;
  description: string;
  version: string;
  style: ThemeStyle;
  screenshots: { url: string; description: string }[];
  archive: string;
  input: string;
  destination: string;
};