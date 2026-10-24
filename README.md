# DSiDrop

**Your library. On your DSi.**

A self-hosted game catalog and download manager with a companion app for Nintendo DSi.
## Join our Discord

[![Discord](https://img.shields.io/badge/Discord-Join%20our%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/AEZBum2yr)

## Web app

- Browse Nintendo DS, GBA, Game Boy, Game Boy Color, NES, SNES and Nintendo 64 catalogs.
- View cover art, file sizes, languages, regions, checksums and alternate versions.
- Search and filter by region, language, release type and cover availability.
- Sort by title or size, or discover a random game.
- Download individual files or use a saved server download queue with progress tracking.
- Upload your own `.nds` files to **Others**, then download or delete them from the library.
- Manage multiple DSi devices, rename them and monitor their connection and transfer status.
- Send NDS, GBA and Others downloads to a DSi, individually or as a queue.

## DSi app

- Browse NDS, GBA and Others with covers, search and region filters.
- Use touchscreen controls, physical buttons and an on-screen keyboard.
- Download over Wi-Fi directly to organized folders on the internal SD card.
- Pause and resume downloads, verify transfers with CRC32 and recognize completed files.
- Save a download queue of up to 32 games across app restarts.
- Receive downloads sent from the web app and report progress back to the browser.
- Configure WPA2/AES Wi-Fi, use existing system profiles and view connection diagnostics.

## Quick start

Install and start Docker with Linux containers and Docker Compose. Download or clone this repository, open a terminal in the folder containing `compose.yaml`, and run:

```sh
docker compose up -d --build
```

Open **[localhost:3000](http://localhost:3000)**. The first build may take a few minutes. Downloads and settings are stored in a persistent Docker volume.

To stop the server:

```sh
docker compose down
```

## Connect your DSi

You need a DSi or DSi XL, TWiLight Menu++, a writable internal FAT32 SD card and a running server on the same home network. Custom Wi-Fi profiles use 2.4 GHz WPA2-Personal/AES.

1. Download [DSiDrop.nds](public/downloads/DSiDrop.nds) from the web app or this repository and copy it to the DSi SD card.
2. Launch it through TWiLight Menu++ in **DSi mode**.
3. Press **SELECT → A** to configure Wi-Fi.
4. Press **SELECT → X** and enter your server PC's local IPv4 address and port (**3000** by default).

Launch downloaded games through TWiLight Menu++. GBA playback depends on your handheld setup.

## Configuration and source

- To change the port or timezone, copy [.env.example](.env.example) to `.env` and edit `DSIDROP_PORT` or `TZ`.
- Keep `.env`, Wi-Fi settings, downloaded games, device records and generated build files out of published copies.
- DSi client source and build command: [nds/README.md](nds/README.md).
 

Catalog data: [Internet Archive](https://archive.org). Cover art: [libretro-thumbnails](https://github.com/libretro-thumbnails). DSi library notices: [THIRD-PARTY.txt](nds/THIRD-PARTY.txt).
