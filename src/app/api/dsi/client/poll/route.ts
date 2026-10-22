import { clientPoll, DeviceError, deviceSameOrigin, getDevices } from "@/lib/server/devices";
import { dsiError, dsiText } from "@/lib/server/dsi";

export async function POST(request: Request) {
  try {
    deviceSameOrigin(request);
    if (request.url.length > 2048) throw new DeviceError("The client request is too large.", 413);
    return dsiText(await getDevices().poll(clientPoll(new URL(request.url).searchParams)));
  } catch (error) {
    if (error instanceof DeviceError) return dsiText(`RFERROR1\t${error.message}\n`, error.status);
    return dsiError(error);
  }
}
