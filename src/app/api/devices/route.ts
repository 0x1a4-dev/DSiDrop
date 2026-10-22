import { deviceBody, deviceError, getDevices } from "@/lib/server/devices";

const json = (value: unknown) => Response.json(value, { headers: { "Cache-Control": "no-store" } });

export async function GET() {
  try { return json(await getDevices().snapshot()); }
  catch (error) { return deviceError(error); }
}

export async function PATCH(request: Request) {
  try { return json(await getDevices().update(await deviceBody(request))); }
  catch (error) { return deviceError(error); }
}

export async function DELETE(request: Request) {
  try { return json(await getDevices().forget(await deviceBody(request))); }
  catch (error) { return deviceError(error); }
}
