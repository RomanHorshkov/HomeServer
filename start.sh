#!/bin/sh
set -e

# Start nginx in the foreground
nginx -g 'daemon off;' &

# Drop to nobody for the server
exec su nobody -s /bin/sh -c "/usr/local/bin/server --port ${SERVER_PORT}"
