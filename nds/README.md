# DSiDrop for Nintendo DSi
Start the server from the repository root with `docker compose up -d`.
Copy [DSiDrop.nds](../public/downloads/DSiDrop.nds) to your DSi SD card and launch it with TWiLight Menu++ in DSi mode.
Connect both devices to the same network; on the DSi, press SELECT then X to enter your PC's local IP and port (default: 3000).
To build from source, install devkitPro with nds-dev and run `.\nds\build.ps1` from the repository root; see [third-party notices](THIRD-PARTY.txt).

The build also requires Python 3, CMake and internet access on its first run. It downloads dswifi 2.1.0 at commit `ac8fbe9431d32cc4247df300d70c1688057da5e8`, verifies the affected source files and builds a project-local copy under `nds/build/dswifi`. The receive FIFO is 32 KiB; the advertised TCP window follows available FIFO space rather than the upstream 1,400-byte cap. MTU and TCP segment sizing are unchanged. The installed devkitPro libraries are not modified. Subsequent builds use the cached source archive; set `PYTHON` if Python is not on the devkitPro shell's PATH.

Run `make -C nds test-http` from the repository root in a devkitPro shell with a host C compiler (`HOST_CC`, default: `cc`). The HTTP regression test covers immediate and fragmented reads, buffered response data, cancellation, socket errors and elapsed-time connect/send/receive timeouts without DSi hardware.

After building, run `python nds/tests/test_dswifi.py --check-arm` to check source verification, repeatable offline preparation and the TCP implementation linked into the ARM binary. The HTTP receive loop sleeps cooperatively for 1 ms when no data is available; input and cancellation remain time-based rather than tied to each network read.
