# ── builder stage ─────────────────────────────────────────────────────────────
FROM debian:trixie-slim AS builder

# Install build tools
RUN apt-get update && \
    apt-get install -y gcc make libc6-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/HomeServer
COPY . .

# Build your server
RUN make release

# ── final image with nginx & server ───────────────────────────────────────────
FROM debian:trixie-slim AS final

# Expose HTTP & your app port
ENV SERVER_PORT=3490
EXPOSE 80 3490

# Install nginx, tini, and necessary tools
RUN apt-get update && \
    apt-get install -y --no-install-recommends nginx tini && \
    rm -rf /var/lib/apt/lists/*

# Disable default nginx user directive and prepare directories
RUN sed -i 's/^user .*;/# disabled user directive;/' /etc/nginx/nginx.conf && \
    mkdir -p /var/lib/nginx/body /var/cache/nginx /var/log/nginx && \
    chown -R www-data:www-data /var/lib/nginx /var/cache/nginx /var/log/nginx

# Copy the server binary into PATH
COPY --from=builder /usr/src/HomeServer/build/bin/server /usr/local/bin/server

# Copy private data (owned by nobody)
COPY --from=builder /usr/src/HomeServer/var/lib /var/lib/HomeServer

# Copy public assets (read-only)
COPY --from=builder /usr/src/HomeServer/var/www /srv/HomeServer/www

# Copy nginx site config
COPY nginx/default.conf /etc/nginx/conf.d/default.conf

# Copy and configure start script
COPY start.sh /usr/local/bin/start.sh
RUN chmod +x /usr/local/bin/start.sh

# Set permissions for server and data
ENV PERM_SCRIPT="\
  chown root:root /usr/local/bin/server && \
  chmod 555 /usr/local/bin/server && \
  chown -R nobody:nogroup /var/lib/HomeServer && chmod -R 700 /var/lib/HomeServer && \
  chown -R nobody:nogroup /srv/HomeServer/www && chmod -R 555 /srv/HomeServer/www"
RUN sh -c "$PERM_SCRIPT"

# Use tini to start both services under root; start.sh will drop privileges for server
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/start.sh"]
