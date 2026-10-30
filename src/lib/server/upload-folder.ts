import { createHash, randomUUID } from "node:crypto";
import { createReadStream } from "node:fs";
import { open, rm } from "node:fs/promises";
import path from "node:path";
import { folderDestination, MAX_FOLDER_SIZE, type UploadFolder } from "../upload-folder";

type FolderItem = { name: string; size: number; crc: number; temporary: string };

export async function buildUploadFolder(items: FolderItem[], multimedia: boolean, directory: string, signal: AbortSignal) {
  const target = folderDestination(items.map((item) => item.name), multimedia);
  const names = new Set<string>();
  const files = [...items].sort((first, second) => first.name.localeCompare(second.name, "en"));
  const root = Buffer.from(target.destination);
  const size = 12 + root.length + files.reduce((total, file) => total + 10 + Buffer.byteLength(file.name.slice(target.name.length + 1)) + file.size, 0);
  if (size > MAX_FOLDER_SIZE) throw new Error("A complete folder package must be 512 MiB or smaller.");
  const temporary = path.join(directory, `.folder-${randomUUID()}.part`);
  const output = await open(temporary, "wx");
  const digest = createHash("md5");
  const folder: UploadFolder = { destination: target.destination, category: multimedia ? "Multimedia" : "Uploaded", files: [] };
  let offset = 0;
  async function write(bytes: Buffer) {
    signal.throwIfAborted();
    await output.writeFile(bytes);
    digest.update(bytes); offset += bytes.length;
  }
  try {
    const header = Buffer.alloc(12);
    header.write("RFFILES1"); header.writeUInt16LE(root.length, 8); header.writeUInt16LE(files.length, 10);
    await write(header); await write(root);
    for (const file of files) {
      const relative = file.name.slice(target.name.length + 1);
      const key = relative.toLowerCase();
      if (names.has(key)) throw new Error("A folder contains duplicate FAT file names.");
      names.add(key);
      const name = Buffer.from(relative);
      const item = Buffer.alloc(10);
      item.writeUInt16LE(name.length); item.writeUInt32LE(file.size, 2); item.writeUInt32LE(file.crc, 6);
      await write(item); await write(name);
      folder.files.push({ name: relative, size: file.size, offset });
      const start = offset;
      for await (const chunk of createReadStream(file.temporary)) await write(chunk);
      if (offset - start !== file.size) throw new Error("A folder file changed during import.");
    }
    await output.sync();
    await output.close();
    return { id: digest.digest("hex"), name: `${target.name}.folder`, title: target.name, size, uploadedAt: new Date().toISOString(), temporary, folder };
  } catch (error) {
    await output.close().catch(() => undefined);
    await rm(temporary, { force: true });
    throw error;
  }
}