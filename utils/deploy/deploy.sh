#!/usr/bin/env bash
set -euo pipefail

# Define user and group name in a variable
USER_GROUP_NAME="home_server"


# Save current working directory
ORIG_DIR="$(pwd)"
USR="$(id -u -n)"
PROJ_DIR="/home/$USR/HomeServer"
bin="server"
dest="/srv/home_server/bin"
# Print info
echo "Deploying HomeServer as user $USR from $ORIG_DIR"


# Go to the HomeServer root
cd $ORIG_DIR
echo "pwd: $(pwd)"

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
  echo "Not root." >&2
  exit 1
fi

# Check if group exists
if getent group "$USER_GROUP_NAME" >/dev/null 2>&1; then
  echo "Group '$USER_GROUP_NAME' exists."
else
  echo "Group '$USER_GROUP_NAME' does NOT exist, creating..."
  groupadd $USER_GROUP_NAME
fi

# Check if user exists
if id -u "$USER_GROUP_NAME" >/dev/null 2>&1; then
  echo "User '$USER_GROUP_NAME' exists."
else
  echo "User '$USER_GROUP_NAME' does NOT exist, creating..."
  useradd -g "$USER_GROUP_NAME" "$USER_GROUP_NAME"
fi

# Lock user's shell
desired_shell="/usr/sbin/nologin"

if [ "$(getent passwd "$USER_GROUP_NAME" | cut -d: -f7)" != "$desired_shell" ]; then
  echo "Dropping '$USER_GROUP_NAME' privileges to $desired_shell"
  usermod -s "$desired_shell" "$USER_GROUP_NAME"
else
  echo "User '$USER_GROUP_NAME' shell set to $desired_shell."
fi

# Check user's permissions on /srv
echo "chown -R /srv/"$USER_GROUP_NAME" to "$USER_GROUP_NAME":"$USER_GROUP_NAME""
chown -R "$USER_GROUP_NAME:$USER_GROUP_NAME" /srv/home_server
echo "chmod -R 750 /srv/"$USER_GROUP_NAME""
chmod -R 750 /srv/$USER_GROUP_NAME
ls -l /srv/$USER_GROUP_NAME

# Deploy frontend
echo "Deploying frontend to /srv/home_server/pub/"
rm -rf /srv/home_server/pub/*
cp -r frontend/dist/* /srv/home_server/pub/


# Deploy backend
pwd
make clean
make all

install -D -m 0755 "build/bin/$bin" "$dest/$bin"

# Run as home_server (nologin is fine for non-interactive exec)
echo "Starting $bin as $USER_GROUP_NAME on port 3491…"
exec sudo -u "$USER_GROUP_NAME" -H \
  env HOME="/srv/home_server" \
  bash -c "umask 027; cd /srv/home_server; exec '$dest/$bin' 3491 >> /srv/home_server/pri/server.log 2>&1"
