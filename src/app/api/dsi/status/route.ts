import { dsiError, dsiText, getJob, statusLine } from "@/lib/server/dsi";

export async function GET(request: Request) {
  try {
    const params = new URL(request.url).searchParams;
    return dsiText(statusLine(await getJob(params.get("id"), params.get("system"))));
  }
  catch (error) { return dsiError(error); }
}
