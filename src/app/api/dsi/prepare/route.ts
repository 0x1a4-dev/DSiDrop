import { dsiError, dsiText, prepare, statusLine } from "@/lib/server/dsi";

export async function POST(request: Request) {
  try {
    const params = new URL(request.url).searchParams;
    return dsiText(statusLine(await prepare(params.get("id"), params.get("system"))));
  }
  catch (error) { return dsiError(error); }
}
