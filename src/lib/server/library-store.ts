import { randomUUID } from "node:crypto";
import { mkdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import path from "node:path";

export class LibraryStore<Data> {
  private tail: Promise<unknown> = Promise.resolve();
  constructor(private file: string, private initial: () => Data) {}
  run<Result>(action: (data: Data) => Promise<Result>, write = true): Promise<Result> {
    const operation = this.tail.then(async () => {
      let data: Data;
      try { data = JSON.parse(await readFile(this.file, "utf8")) as Data; }
      catch (error) { if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error; data = this.initial(); }
      const result = await action(data);
      if (write) {
        await mkdir(path.dirname(this.file), { recursive: true });
        const temporary = `${this.file}.${randomUUID()}.tmp`;
        try { await writeFile(temporary, JSON.stringify(data)); await rename(temporary, this.file); }
        finally { await rm(temporary, { force: true }); }
      }
      return result;
    });
    this.tail = operation.catch(() => undefined);
    return operation;
  }
}