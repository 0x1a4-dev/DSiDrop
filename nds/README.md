# DSiDrop for Nintendo DSi
Start the server from the repository root with `docker compose up -d`.
Copy [DSiDrop.nds](../public/downloads/DSiDrop.nds) to your DSi SD card and launch it with TWiLight Menu++ in DSi mode.
Connect both devices to the same network; on the DSi, press SELECT then X to enter your PC's local IP and port (default: 3000).
To build from source, install devkitPro with nds-dev and run `.\nds\build.ps1` from the repository root; see [third-party notices](THIRD-PARTY.txt).

In **Others**, tap **Category** and select **Apps** to browse the `.nds` files in the server's `public/downloads/apps/` directory, or **Your uploads** for your own library. Downloads and the queue save the executable to `/roms/others/`. DSOrganize 3.2 additionally requires its `DSOrganize` directory at the SD root or under `/data/`; obtain the complete ZIP from its web details and copy that directory using your PC before launching it.

**Others > Category > Multimedia** lists uploaded videos, music and images. DSiDrop 1.17.0 saves them to `/videos/`, `/music/` and `/pictures/`, creating missing folders. Playback requires compatible homebrew; no media conversion is performed.

The bundled apps include local covers based on their original NDS icons. To update DSiDrop itself, update the PC server and select **SELECT > L / Update**. The app downloads and verifies the server's build, backs up the old executable as `.nds.bak`, and installs at its launch path. If the launcher did not supply a valid path, the confirmation instead shows `/DSiDrop.nds`. Close the completion message to exit and reopen the installed copy in TWiLight Menu++. Settings and queues are preserved. The updater-enabled build must be installed once before this option is available.

In **Others > Category > Themes**, choose Nintendo DSi, Nintendo 3DS, R4 Original or Wood UI, optionally with **X / Favorites only**. Select a theme to open its details: **Y** shows the original dual-screen preview when available, **X** saves or removes a shared server favorite, and **A** installs. The verified download is unpacked directly into `/_nds/TWiLightMenu/<menu>/themes/<theme>/`, creating missing folders. Existing themes are staged and backed up as `<theme>.dsidrop-bak` before replacement; keep that backup for recovery after a power interruption. Select the installed skin in TWiLight Menu++ Settings afterwards. TWiLight Menu++ itself is not installed or reconfigured.

From **1.14.0**, **Devices > Save backups** in the web app can request an SD scan and upload of nonempty `.sav` files. Keep DSiDrop open and idle; games and save operations run sequentially. Automatic backup on connection can be enabled per device in that panel. From **1.16.0**, choosing a dated version in the browser restores it automatically, without confirmation on the handheld. Transfers to another DSi use the same automatic restore on the destination, which must also run 1.16.0 or newer. The current save is backed up to the server first, and the restored file is checked again after writing to SD. A local `.sav.dsidrop-bak` is retained for recovery. Failures are reported to the web panel without blocking subsequent jobs with a handheld dialog.

From **1.15.0**, the app reports its installed `.nds` / `.gba` files and associated save paths to **DSi devices > Games on SD**, with catalog covers resolved by the server. From **1.18.0**, automatic inventory checks and enabled save backups run only at app startup when file metadata has changed; WLAN reconnects and downloads do not trigger another full scan. **Check ROMs & saves** beside the selected device requests a manual inventory check. **Delete ROM** is confirmed only in the browser and runs without a confirmation on the handheld. Saves and backups are preserved; the current app and system paths are protected, and changed ROM metadata aborts deletion. Keep DSiDrop open and idle for these operations.

From **1.16.0**, the app also reports SD filesystem capacity and available space approximately every 15 seconds while idle. The web device panel shows total, used and free space with a percentage bar; offline devices retain their last reported sample. Storage-query failures do not prevent inventory scans or save operations.

Web **Collections > Send all to DSi** sends up to 32 NDS, GBA and Others entries through the existing device queue; no separate native collection queue is required.

The build also requires Python 3, CMake and internet access on its first run. It downloads dswifi 2.1.0 at commit `ac8fbe9431d32cc4247df300d70c1688057da5e8`, verifies the affected source files and builds a project-local copy under `nds/build/dswifi`. The receive FIFO is 32 KiB; the advertised TCP window follows available FIFO space rather than the upstream 1,400-byte cap. MTU and TCP segment sizing are unchanged. The installed devkitPro libraries are not modified. Subsequent builds use the cached source archive; set `PYTHON` if Python is not on the devkitPro shell's PATH.

Run `make -C nds test-http` from the repository root in a devkitPro shell with a host C compiler (`HOST_CC`, default: `cc`). The HTTP regression test covers immediate and fragmented reads, buffered response data, cancellation, socket errors and elapsed-time connect/send/receive timeouts without DSi hardware.

Run `make -C nds test-update` for the focused host test of update installation, backups, rollback and preservation of the installed app on failure.

After building, run `python nds/tests/test_dswifi.py --check-arm` to check source verification, repeatable offline preparation and the TCP implementation linked into the ARM binary. The HTTP receive loop sleeps cooperatively for 1 ms when no data is available; input and cancellation remain time-based rather than tied to each network read.
