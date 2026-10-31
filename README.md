
# DSiDrop

**Your library. On your DSi.**

A self-hosted game catalog and download manager with a companion app for Nintendo DSi.

## Join our Discord

[![Discord](https://img.shields.io/badge/Discord-Join%20our%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/AEZBum2yr)
## Features

- Browse classic Nintendo games with cover art, search and filters.
- Download games through your browser or directly to your DSi over Wi-Fi.
- Upload your own games, media and folders.
- Explore homebrew apps and TWiLight Menu++ themes.
- Organize games into collections and download queues.
- Manage installed games on SD and multiple DSi devices.
- Back up, restore and transfer saves from your DSi or browser.
- Update the DSi app from your own server.


<kbd><img width="1154" height="839" alt="DSiDrop_Preview" src="https://github.com/user-attachments/assets/5c2e4431-1f5d-4dd7-8618-522b96ea5b19" /></kbd>


<kbd><img width="256" height="384" alt="catalog" src="https://github.com/user-attachments/assets/253ba999-f41e-4634-a70c-f17b084fe24c" /></kbd>&emsp;&emsp;<kbd><img width="256" height="384" alt="backup1" src="https://github.com/user-attachments/assets/2fa56a5c-35ca-4a9d-abc6-1d78bfc71073" /></kbd>

## Quick start

Install Docker with Linux containers and Docker Compose. In the project folder, run:

```sh
docker compose up -d --build
```

Open **[localhost:3000](http://localhost:3000)**. The first build may take a few minutes. Downloads and settings are stored in a persistent Docker volume.

To stop the server:

```sh
docker compose down
```

## Connect your DSi

You need a DSi or DSi XL with TWiLight Menu++, a writable FAT32 SD card and a running server on the same home network.

1. Download [DSiDrop.nds](public/downloads/DSiDrop.nds) from the web app or this repository and copy it to the DSi SD card.
2. Launch it through TWiLight Menu++ in **DSi mode**.
3. Press **SELECT**, choose **Settings**, then **Wi-Fi network** to configure Wi-Fi.
4. Choose **Server address** and enter your server PC's local IPv4 address and port (**3000** by default).

Launch downloaded games through TWiLight Menu++. GBA playback depends on your handheld setup.

## Configuration and source

- To change the port or timezone, copy [.env.example](.env.example) to `.env` and edit `DSIDROP_PORT` or `TZ`.
- Use a trusted local network. Device actions and save backups are not authenticated or encrypted.
- DSi client source and build command: [nds/README.md](nds/README.md).

Catalog data: [Internet Archive](https://archive.org). Cover art: [libretro-thumbnails](https://github.com/libretro-thumbnails). DSi library notices: [THIRD-PARTY.txt](nds/THIRD-PARTY.txt).

Homebrew sources: [notPipe DSi](https://l-i-o.itch.io/notpipe-dsi), [DSOrganize](https://dragonminded.com/ndsdev/dsorganize/), [pkmn-chest](https://github.com/Universal-Team/pkmn-chest/). [Homebrew license and source notices](public/downloads/apps/THIRD-PARTY.txt).
