import { NextResponse } from "next/server";

import { getQueue } from "@/lib/server/queue";

export async function GET() {
  return NextResponse.json(await getQueue().snapshot());
}

export async function POST(request: Request) {
  const body = (await request.json().catch(() => null)) as
    | {
        id?: unknown;
        name?: unknown;
        title?: unknown;
        region?: unknown;
        system?: unknown;
        systemId?: unknown;
        size?: unknown;
        url?: unknown;
        arch?: unknown;
        ext?: unknown;
        out?: unknown;
      }
    | null;

  if (!body || typeof body.id !== "string" || typeof body.name !== "string") {
    return NextResponse.json({ error: "Invalid request" }, { status: 400 });
  }

  const arch =
    body.arch === "zip" || body.arch === "7z" || body.arch === null
      ? body.arch
      : undefined;

  const queue = getQueue();
  await queue.add({
    id: body.id,
    name: body.name,
    title: typeof body.title === "string" ? body.title : body.name,
    region: typeof body.region === "string" ? body.region : "",
    system: typeof body.system === "string" ? body.system : "",
    systemId: typeof body.systemId === "string" ? body.systemId : undefined,
    size: typeof body.size === "number" ? body.size : 0,
    url: typeof body.url === "string" ? body.url : undefined,
    arch,
    ext: typeof body.ext === "string" ? body.ext : undefined,
    out: typeof body.out === "string" ? body.out : undefined,
  });
  return NextResponse.json(await queue.snapshot(), { status: 201 });
}

export async function DELETE(request: Request) {
  const id = new URL(request.url).searchParams.get("id");
  const queue = getQueue();
  if (id) await queue.remove(id);
  else await queue.clear();
  return NextResponse.json(await queue.snapshot());
}
