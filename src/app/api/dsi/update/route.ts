import { appUpdate, dsiError } from "@/lib/server/dsi";

export async function GET(request: Request) {
  try { return await appUpdate(request); }
  catch (error) { return dsiError(error); }
}

export async function HEAD(request: Request) {
  try { return await appUpdate(request, true); }
  catch (error) { return dsiError(error); }
}