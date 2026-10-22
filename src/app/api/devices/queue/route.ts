import { deviceBody, deviceError, getDevices } from "@/lib/server/devices";

export async function POST(request: Request) {
  try { return Response.json(await getDevices().enqueue(await deviceBody(request)), { status: 201, headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return deviceError(error); }
}

export async function DELETE(request: Request) {
  try { return Response.json(await getDevices().removeTasks(await deviceBody(request)), { headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return deviceError(error); }
}
