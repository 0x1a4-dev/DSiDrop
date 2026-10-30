export function GET() {
  return Response.json(
    { status: "ok", application: "DSiDrop", version: "1.19.0" },
    { headers: { "Cache-Control": "no-store" } },
  );
}
