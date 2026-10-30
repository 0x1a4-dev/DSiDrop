const MEDIA_FORMATS = {
  videos: ["mp4", "avi", "mov", "mkv", "webm", "mpeg", "mpg", "dpg", "rvid"],
  music: ["mp3", "wav", "ogg", "aac", "m4a", "flac", "mod", "it", "s3m", "xm"],
  pictures: ["jpg", "jpeg", "png", "gif", "bmp", "webp"],
} as const;

export const MEDIA_ACCEPT = Object.values(MEDIA_FORMATS).flat().map((extension) => `.${extension}`).join(",");

export function supportsMultimedia(version: string): boolean {
  const match = /^(\d+)\.(\d+)(?:\.\d+)?$/.exec(version);
  return Boolean(match && (Number(match[1]) > 1 || (Number(match[1]) === 1 && Number(match[2]) >= 17)));
}

export function mediaInfo(name: string) {
  const extension = name.match(/\.([a-z0-9]+)$/i)?.[1].toLowerCase();
  if (!extension) return null;
  for (const [folder, extensions] of Object.entries(MEDIA_FORMATS)) {
    if ((extensions as readonly string[]).includes(extension)) {
      return { extension, directory: `/${folder}`, kind: folder === "videos" ? "Video" : folder === "music" ? "Music" : "Image" };
    }
  }
  return null;
}