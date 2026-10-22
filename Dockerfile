FROM node:24-bookworm-slim AS dependencies
WORKDIR /app
ENV NEXT_TELEMETRY_DISABLED=1
COPY package.json package-lock.json ./
RUN npm ci --include=dev --no-audit --no-fund

FROM dependencies AS builder
COPY next.config.ts postcss.config.mjs tsconfig.json ./
COPY src ./src
COPY public ./public
COPY licenses ./licenses
COPY docker/collect-licenses.mjs ./docker/collect-licenses.mjs
RUN node docker/collect-licenses.mjs
RUN npm run build

FROM node:24-bookworm-slim AS runner
WORKDIR /app
ENV NODE_ENV=production \
    NEXT_TELEMETRY_DISABLED=1 \
    HOSTNAME=0.0.0.0 \
    PORT=3000 \
    ROMS_DIR=/data/roms
LABEL org.opencontainers.image.title="DSiDrop" \
      org.opencontainers.image.description="DSiDrop library and Nintendo DSi download server" \
      org.opencontainers.image.version="1.11.0"
COPY --from=builder --chown=node:node /app/.next/standalone ./
COPY --from=builder --chown=node:node /app/.next/static ./.next/static
COPY --from=builder --chown=node:node /app/public ./public
COPY --from=builder --chown=node:node /app/licenses ./licenses
RUN mkdir -p /data/roms /app/.next/cache \
    && chown -R node:node /data /app/.next/cache \
    && chmod +x /app/node_modules/7zip-bin/linux/*/7za
USER node
EXPOSE 3000
VOLUME ["/data"]
HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD ["node", "-e", "fetch('http://127.0.0.1:3000/api/health',{signal:AbortSignal.timeout(3000)}).then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"]
CMD ["node", "server.js"]
