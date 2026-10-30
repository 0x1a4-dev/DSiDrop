import { deviceBody, deviceError, deviceSameOrigin } from "@/lib/server/devices";
import { setThemeFavorite, themeFavorites } from "@/lib/server/themes";

export async function POST(request: Request) {
  try {
    deviceSameOrigin(request);
    const params = new URL(request.url).searchParams;
    if (params.has("id")) {
      const id = params.get("id") || "";
      await setThemeFavorite(id, !(await themeFavorites()).includes(id));
      return new Response("OK\n", { headers: { "Cache-Control": "no-store" } });
    }
    const body = await deviceBody(request);
    if (typeof body.id !== "string" || typeof body.favorite !== "boolean") return Response.json({ error: "Invalid favorite" }, { status: 400 });
    return Response.json({ favorites: await setThemeFavorite(body.id, body.favorite) }, { headers: { "Cache-Control": "no-store" } });
  } catch (error) { return deviceError(error); }
}