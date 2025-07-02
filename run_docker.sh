docker build -t my-c-server:latest .

docker run -it \
  --name homeserver \
  -p 127.0.0.1:3490:3490 \
  --entrypoint sh \
  my-c-server:latest

# rebuild
docker build \
  --no-cache \
  --pull \
  --force-rm \
  -t my-c-server:latest \
  .
