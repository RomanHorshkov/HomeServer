#!/bin/bash

NUM_CLIENTS=11
PORT=3490
HOST=localhost
ALIVE_PIDS=()

echo "Connecting $NUM_CLIENTS clients to $HOST:$PORT..."

for i in $(seq 1 $NUM_CLIENTS); do
  {
    # Keep the connection open (no input)
    nc $HOST $PORT &
    pid=$!
    sleep 0.5  # small delay between launches
    if ps -p $pid > /dev/null; then
      echo "Client $i connected (PID $pid)"
      ALIVE_PIDS+=($pid)
    else
      echo "Client $i was REFUSED or disconnected immediately."
    fi
  } &
done

# Wait for all background jobs to finish launching
wait

echo "Sleeping for 1 second..."
sleep 1

# Check which clients are still alive
echo "Checking alive connections after 3 seconds..."
ALIVE=0
for pid in "${ALIVE_PIDS[@]}"; do
  if ps -p $pid > /dev/null; then
    echo "Client with PID $pid is still alive."
    ((ALIVE++))
  else
    echo "Client with PID $pid is no longer alive."
  fi
done

echo "Result: $ALIVE clients are still connected."

if [ "$ALIVE" -eq 0 ]; then
  echo "No clients are alive. Exiting."
  exit 1
else
  echo "Some clients are still alive. Exiting."
  exit 0
fi

