import { deviceError, deviceSameOrigin } from "@/lib/server/devices";
import { deviceLibrary } from "@/lib/server/device-library";

export async function POST(request: Request) {
  try {
    deviceSameOrigin(request);
    const text = await deviceLibrary.poll(new URL(request.url).searchParams);
    return new Response(text, { headers: { "Content-Type": "text/plain; charset=us-ascii", "Content-Length": String(Buffer.byteLength(text)), "Cache-Control": "no-store" } });
  } catch (error) { return deviceError(error); }
}
export async function PUT(request: Request) {
  try { deviceSameOrigin(request); await deviceLibrary.upload(request); return new Response("OK\n", { headers: { "Content-Length": "3", "Cache-Control": "no-store" } }); }
  catch (error) { return deviceError(error); }
}