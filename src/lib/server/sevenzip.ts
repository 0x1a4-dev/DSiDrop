import { execFile } from "node:child_process";
import { mkdir, readdir } from "node:fs/promises";
import path from "node:path";
import { promisify } from "node:util";

import { path7za } from "7zip-bin";

const execFileAsync = promisify(execFile);

async function listFiles(dir: string): Promise<string[]> {
  const entries = await readdir(dir, { withFileTypes: true });
  const files: string[] = [];
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      files.push(...(await listFiles(full)));
    } else if (entry.isFile()) {
      files.push(full);
    }
  }
  return files;
}

export async function extract7z(
  archivePath: string,
  destDir: string,
): Promise<string[]> {
  await mkdir(destDir, { recursive: true });
  await execFileAsync(
    path7za,
    ["x", archivePath, `-o${destDir}`, "-y", "-bso0", "-bsp0"],
    { windowsHide: true },
  );
  return listFiles(destDir);
}
