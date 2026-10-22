# DSiDrop

1. Install and start Docker.
2. Download the project and open a terminal in the folder containing `compose.yaml`.
3. Run:

   ```sh
   docker compose up -d
   ```

4. Open [localhost:3000](http://localhost:3000) in your browser.

The first start may take a few minutes. Your downloads and settings are saved automatically.

To customize the port or timezone, copy `.env.example` to `.env` and edit the
local copy. The default timezone is UTC. Keep `.env` files, downloaded games,
device records, download queues, caches, logs and exported Docker images out of
published copies. The included ignore files exclude these local files from Git
and the Docker build context; publish `.env.example` as the configuration template.

To connect a DSi, download `DSiDrop.nds` from the web app and launch it with
TWiLight Menu++. Under **SELECT → X**, enter your computer's local IP and port
`3000`. Both devices must be on the same home network.

To stop:

```sh
docker compose down
```
