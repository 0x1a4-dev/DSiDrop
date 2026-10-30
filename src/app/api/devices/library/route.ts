import { deviceBody, deviceError } from "@/lib/server/devices";
import { deviceLibrary } from "@/lib/server/device-library";

export async function GET(request: Request) {
  try { return Response.json(await deviceLibrary.snapshot(new URL(request.url).searchParams.get("mac")), { headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return deviceError(error); }
}
export async function POST(request: Request) {
  try { return Response.json(await deviceLibrary.change(await deviceBody(request))); }
  catch (error) { return deviceError(error); }
}