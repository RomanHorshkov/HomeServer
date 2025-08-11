# Step by step deploy of the web server on a host machine

## Folders setup

home_server

On host machine

```bash
sudo mkdir -p /srv/home_server/{pub,prv}
sudo chown -R $USER:www-data /srv/home_server
sudo chmod -R 750 /srv/home_server/prv
sudo chmod -R 750 /srv/home_server/pub
```

## Deploy backend as a service

in /etc/systemd/system/home_server.service
[Unit]

put in pub the frontend build.

## Nginx config

at /etc/nginx edit the nginx.conf for the purpose.
OR

```bash
sudo ln -s /etc/nginx/sites-available/home_server.conf /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl restart nginx
```

## Firewall

```bash
sudo ufw allow 'Nginx Full'
sudo ufw enable
```

## Run backend

```bash
make run
```

## Run Nginx

```bash
sudo systemctl restart nginx
```

## Test

```bash
curl -I http://localhost/
```
