import { mediaThumbnail } from "@/lib/server/media-thumbnail";
import { uploadError } from "@/lib/server/uploads";

async function thumbnail(request: Request, head = false) {
  try {
    const image = await mediaThumbnail(new URL(request.url).searchParams.get("id") || "");
    if (!image) return new Response(null, { status: 404 });
    return new Response(head ? null : new Uint8Array(image), { headers: {
      "Content-Type": "image/jpeg", "Content-Length": String(image.length),
      "Cache-Control": "private, max-age=86400", "X-Content-Type-Options": "nosniff",
    } });
  } catch (error) { return uploadError(error); }
}

export const GET = (request: Request) => thumbnail(request);
export const HEAD = (request: Request) => thumbnail(request, true);