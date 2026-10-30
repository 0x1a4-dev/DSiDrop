import { themeCatalog, themeFavorites } from "@/lib/server/themes";

export async function GET() {
  try {
    return Response.json({ themes: await themeCatalog(), favorites: await themeFavorites() }, { headers: { "Cache-Control": "no-store" } });
  } catch (error) {
    return Response.json({ error: error instanceof Error ? error.message : "Theme catalog unavailable" }, { status: 503 });
  }
}