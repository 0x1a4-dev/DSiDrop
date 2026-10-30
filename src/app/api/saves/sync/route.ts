import { deviceError, deviceSameOrigin } from "@/lib/server/devices";
import { saves } from "@/lib/server/saves";
export async function POST(request: Request) {
  try {
    deviceSameOrigin(request);
    const params = new URL(request.url).searchParams;
    const text = await saves.poll(params);
    return new Response(text, { headers: { "Content-Type": "text/plain; charset=utf-8", "Content-Length": String(Buffer.byteLength(text)), "Cache-Control": "no-store" } });
  } catch (error) { return deviceError(error); }
}
export async function PUT(request: Request) {
  try { deviceSameOrigin(request); await saves.upload(request); return new Response("OK\n", { headers: { "Content-Length": "3", "Cache-Control": "no-store" } }); }
  catch (error) { return deviceError(error); }
}