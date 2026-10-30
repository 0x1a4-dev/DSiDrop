import { randomBytes } from "node:crypto";
import path from "node:path";
import { unstable_rethrow } from "next/navigation";
import type { CollectionItem, GameCollection } from "../collection-types";
import type { DeviceSystem } from "../device-types";
import { coverUrl } from "../roms";
import { DeviceError } from "./devices";
import { lookup } from "./dsi";
import { LibraryStore } from "./library-store";
import { ROMS_DIR } from "./rom-file";

type ResolveGame = (id: string, system: DeviceSystem) => ReturnType<typeof lookup>;
export class CollectionLibrary {
  private store: LibraryStore<{ collections: GameCollection[] }>;
  constructor(file: string, private resolveGame: ResolveGame = lookup) { this.store = new LibraryStore(file, () => ({ collections: [] })); }
  snapshot() {
    return this.store.run(async (data) => ({ collections: await Promise.all(data.collections.map(async (collection) => ({
      ...collection,
      items: await Promise.all(collection.items.map(async (item) => {
        try {
          const { rom, data } = await this.resolveGame(item.id, item.system);
          return { ...item, cover: coverUrl(data.meta, rom) };
        } catch (error) { unstable_rethrow(error); return { ...item, cover: null }; }
      })),
    }))) }), false);
  }
  change(body: Record<string, unknown>) {
    return this.store.run(async (data) => {
      let collection = data.collections.find((entry) => entry.id === body.id);
      const stamp = new Date().toISOString();
      if (body.action === "create" || body.action === "rename") {
        if (typeof body.name !== "string" || !body.name.trim() || body.name.trim().length > 64 || /[\x00-\x1f\x7f]/.test(body.name)) throw new DeviceError("Enter a collection name with 1-64 characters.");
        if (data.collections.some((entry) => entry.id !== collection?.id && entry.name.toLowerCase() === (body.name as string).trim().toLowerCase())) throw new DeviceError("This collection name already exists.", 409);
        if (body.action === "create") {
          if (data.collections.length >= 100) throw new DeviceError("Up to 100 collections are supported.", 409);
          collection = { id: randomBytes(16).toString("hex"), name: body.name.trim(), items: [], updatedAt: stamp };
          data.collections.push(collection);
        } else { if (!collection) throw new DeviceError("Collection not found.", 404); collection.name = body.name.trim(); }
      } else {
        if (!collection) throw new DeviceError("Collection not found.", 404);
        if (body.action === "delete") data.collections = data.collections.filter((entry) => entry !== collection);
        else if (body.action === "remove") collection.items = collection.items.filter((entry) => entry.id !== body.game || entry.system !== body.system);
        else if (body.action === "add") {
          if (!Array.isArray(body.items) || !body.items.length || body.items.length > 32) throw new DeviceError("Choose 1-32 games.");
          const prepared = await Promise.all(body.items.map(async (value: unknown): Promise<CollectionItem> => {
            if (!value || typeof value !== "object") throw new DeviceError("Invalid collection game.");
            const { id, system } = value as { id: unknown; system: unknown };
            if ((system !== "nds" && system !== "gba" && system !== "others") || typeof id !== "string" || !(system === "gba" ? /^[a-f0-9]{8}$/ : /^[a-f0-9]{32}$/).test(id)) throw new DeviceError("Collections support NDS, GBA and Others games.");
            const { rom } = await this.resolveGame(id, system);
            return { id, system, title: rom.t, region: rom.r };
          }));
          const unique = new Map([...collection.items, ...prepared].map((entry) => [`${entry.system}:${entry.id}`, entry]));
          if (unique.size > 32) throw new DeviceError("A collection holds up to 32 games, matching the DSi queue.", 409);
          collection.items = [...unique.values()];
        } else throw new DeviceError("Choose a collection operation.");
      }
      if (collection) collection.updatedAt = stamp;
      return { collections: data.collections, selected: collection?.id };
    }).then(async (result) => ({ ...result, ...await this.snapshot() }));
  }
}
const globalCollections = globalThis as typeof globalThis & { collectionLibrary?: CollectionLibrary };
export const collections = globalCollections.collectionLibrary ??= new CollectionLibrary(path.join(ROMS_DIR, ".collections.json"));