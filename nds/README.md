# DSiDrop for Nintendo DSi
Start the server from the repository root with `docker compose up -d`.
Copy [DSiDrop.nds](../public/downloads/DSiDrop.nds) to your DSi SD card and launch it with TWiLight Menu++ in DSi mode.
Connect both devices to the same network; on the DSi, press SELECT then X to enter your PC's local IP and port (default: 3000).
To build from source, install devkitPro with nds-dev and run `.\nds\build.ps1` from the repository root; see [third-party notices](THIRD-PARTY.txt).
