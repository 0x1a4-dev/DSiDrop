import { dsiError, gameFile } from "@/lib/server/dsi";

export async function GET(request: Request) {
  try { return await gameFile(request); }
  catch (error) { return dsiError(error); }
}

export async function HEAD(request: Request) {
  try { return await gameFile(request, true); }
  catch (error) { return dsiError(error); }
}
