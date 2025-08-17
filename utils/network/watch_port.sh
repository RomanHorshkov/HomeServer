#!/bin/bash

# Watch a port (TCP) for listening and connections

PORT="$1"

if [[ -z "$PORT" ]]; then
    echo "Usage: $0 <port>"
    exit 1
fi

clear_screen() {
    # Clear only if running in terminal
    [[ -t 1 ]] && clear
}

while true; do
    clear_screen
    echo "=== 📡 Watching TCP port $PORT ($(date +"%H:%M:%S")) ==="
    echo

    echo "🔍 Listening socket:"
    ss -tlnp | grep ":$PORT" || echo "Nothing is listening on port $PORT"

    echo
    echo "🔗 Active connections:"
    ss -tnp | grep ":$PORT" || echo "No active TCP connections"

    echo
    echo "📦 Process using the port (via lsof):"
    sudo lsof -iTCP:$PORT -sTCP:LISTEN -P -n || echo "No process found"

    sleep 1
done

