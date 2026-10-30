import { collections } from "@/lib/server/collections";
import { deviceBody, deviceError } from "@/lib/server/devices";
export async function GET() {
  try { return Response.json(await collections.snapshot(), { headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return deviceError(error); }
}
export async function POST(request: Request) {
  try { return Response.json(await collections.change(await deviceBody(request)), { headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return deviceError(error); }
}