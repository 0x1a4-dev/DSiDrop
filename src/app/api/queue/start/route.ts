import { NextResponse } from "next/server";

import { getQueue } from "@/lib/server/queue";

export async function POST() {
  const queue = getQueue();
  await queue.start();
  return NextResponse.json(await queue.snapshot());
}

export async function DELETE() {
  const queue = getQueue();
  await queue.stop();
  return NextResponse.json(await queue.snapshot());
}
