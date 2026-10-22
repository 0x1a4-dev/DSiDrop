import { deleteUploaded, receiveUploads, uploadedDataset, uploadError } from "@/lib/server/uploads";

export async function GET() {
  try { return Response.json(await uploadedDataset(), { headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return uploadError(error); }
}

export async function POST(request: Request) {
  try { return Response.json(await receiveUploads(request), { headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return uploadError(error); }
}

export async function DELETE(request: Request) {
  try { return Response.json(await deleteUploaded(request), { headers: { "Cache-Control": "no-store" } }); }
  catch (error) { return uploadError(error); }
}
