import { uploadError, uploadFileResponse } from "@/lib/server/uploads";

export async function GET(request: Request) {
  try { return await uploadFileResponse(request); }
  catch (error) { return uploadError(error); }
}

export async function HEAD(request: Request) {
  try { return await uploadFileResponse(request, true); }
  catch (error) { return uploadError(error); }
}
