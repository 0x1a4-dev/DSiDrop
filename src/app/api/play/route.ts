const unavailable = () => Response.json({
  error: 'PC emulator playback is available in the desktop edition. This server provides library downloads and DSi transfers.',
}, { status: 501, headers: { 'Cache-Control': 'no-store' } });

export const GET = unavailable;
export const POST = unavailable;
