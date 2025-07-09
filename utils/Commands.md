## show runtime dependancies
lld ./build/bin/server


###     DOCKER      ###
Remember that a new tag equals a new release

make docker-build

## clean all the images
docker system prune -a --volumes

## build docker for server with nginx in front
docker build --no-cache -t homeserver-all-in-one:latest .

## run it in background
docker run -d --name homeserver -p 80:80 homeserver-all-in-one:latest

## run it interactively
docker run --rm -it -p 80:80 --entrypoint sh homeserver-all-in-one:latest

## run the server interactively
./build/bin/server

sudo systemctl stop nginx
sudo systemctl disable nginx
