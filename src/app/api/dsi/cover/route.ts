import { dsiCover } from "@/lib/server/dsi-cover";
import { dsiError } from "@/lib/server/dsi";

export async function GET(request: Request) {
  try { return await dsiCover(new URL(request.url).searchParams); }
  catch (error) { return dsiError(error); }
}
