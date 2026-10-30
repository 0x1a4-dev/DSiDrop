import { deviceError } from "@/lib/server/devices";
import { saves } from "@/lib/server/saves";
export async function GET(request: Request) {
  try {
    const params = new URL(request.url).searchParams, { backup, bytes } = await saves.file(params.get("mac"), params.get("id"), params.get("job"));
    return new Response(new Uint8Array(bytes), { headers: { "Content-Type": "application/octet-stream", "Content-Length": String(bytes.length), "Content-Disposition": `attachment; filename="${backup.id}.sav"`, "Cache-Control": "no-store" } });
  } catch (error) { return deviceError(error); }
}