import { catalog, dsiError, dsiText } from "@/lib/server/dsi";

export async function GET(request: Request) {
  try { return dsiText(await catalog(new URL(request.url).searchParams)); }
  catch (error) { return dsiError(error); }
}
