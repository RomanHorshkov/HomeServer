#!/bin/bash

# =============================
# memcheck.sh
# =============================
# This script runs Valgrind's Memcheck tool
# on your compiled server binary to detect
# memory leaks, invalid accesses, and other
# related issues.
#
# USAGE:
#   ./memcheck.sh
#
# REQUIREMENTS:
#   - You must have Valgrind installed.
#   - The compiled binary must exist in:
#       build/bin/server
#   - Your server must listen on a known port (default: 3490)
#   - The server must accept 'q' input from the *same* terminal
#     where it's running (not via a socket connection)
#
# WHAT IT DOES:
#   - Runs Valgrind on the server
#   - Logs output to memcheck.log
#   - Prints a short summary to the terminal
#   - Automatically sends 'q' to the Valgrind process's stdin
#     after a few seconds to shut down the server
# =============================

BINARY="/home/roman/HomeServer/build/bin/server"
LOGFILE="memcheck.log"
PORT=3490

# Check that the binary exists
if [ ! -f "$BINARY" ]; then
  echo "❌ Error: $BINARY not found. Did you run 'make'?"
  exit 1
fi

# Check that Valgrind is installed
if ! command -v valgrind &> /dev/null; then
  echo "❌ Error: valgrind is not installed."
  echo "👉 Install it with: sudo apt install valgrind"
  exit 1
fi

# Create a FIFO pipe for stdin redirection
PIPE="/tmp/memcheck_pipe_$$"
mkfifo "$PIPE"

# Start Valgrind and feed it from the pipe
valgrind \
  --tool=memcheck \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --log-file="$LOGFILE" \
  "$BINARY" < "$PIPE" &
VALGRIND_PID=$!

# Wait a moment to let server start
sleep 2

# Send 'q' to pipe to quit the server
echo "⌛ Sending 'q' to terminate the server gracefully..."
echo "q" > "$PIPE"

# Wait for Valgrind to finish
wait $VALGRIND_PID
STATUS=$?

# Clean up the pipe
rm -f "$PIPE"

# Final report
if [ $STATUS -eq 0 ]; then
  echo "✅ Valgrind finished. See $LOGFILE for full output."
else
  echo "⚠️ Valgrind completed with errors. Check $LOGFILE."
fi

