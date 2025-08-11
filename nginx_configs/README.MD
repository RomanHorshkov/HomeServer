# NGINX Configuration for Patatella Shared Home Cloud

This document provides a detailed breakdown of the NGINX setup used to power the Patatella Shared Home Cloud service. It covers every section of the configuration, explains each directive’s purpose, and offers guidance on deployment, testing, and troubleshooting.

---

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Configuration Breakdown](#configuration-breakdown)

   * [User & Worker Processes](#user--worker-processes)
   * [Events Block](#events-block)
   * [HTTP Core Settings](#http-core-settings)
   * [Logging](#logging)
   * [Gzip Compression](#gzip-compression)
   * [Rate Limiting](#rate-limiting)
   * [SSL/TLS Settings](#ssltls-settings)
   * [Cache Control for Static Assets](#cache-control-for-static-assets)
   * [Upstream Backend](#upstream-backend)
   * [HTTP → HTTPS Redirect](#http--https-redirect)
   * [Main HTTPS Server Block](#main-https-server-block)

     * [TLS Certificates](#tls-certificates)
     * [Security Headers](#security-headers)
     * [Static Frontend Serving](#static-frontend-serving)
     * [API Reverse Proxy](#api-reverse-proxy)
     * [Static Resources Caching](#static-resources-caching)
     * [Custom Error Pages](#custom-error-pages)
   * [Certbot-Managed Redirects](#certbot-managed-redirects)
4. [Deployment Instructions](#deployment-instructions)
5. [Testing & Validation](#testing--validation)
6. [Troubleshooting](#troubleshooting)
7. [License](#license)

---

## Overview

This setup is designed to serve a React single-page app from `/var/www/html` while proxying API calls to a C-based backend listening on port 3490. It enforces HTTPS via Certbot certificates, optimizes performance with gzip, caching, and rate limiting, and strengthens security with modern headers.

## Prerequisites

* NGINX (>= 1.14)
* Certbot for automatic SSL certificate renewal
* A valid DNS record for `patatellasharedhomecloud.cloud` (and `www.`)
* Backend service running on `localhost:3490`

## Configuration Breakdown

### User & Worker Processes

```nginx
user www-data;
worker_processes auto;
pid /run/nginx.pid;
```

* **user**: Runs worker processes under `www-data` for minimal privileges.
* **worker\_processes auto**: Detects available CPU cores.
* **pid**: Specifies the file for storing the master process ID.

### Events Block

```nginx
events {
    worker_connections 10240;
    multi_accept on;
    use epoll;
}
```

* **worker\_connections**: Max connections per worker (10K).
* **multi\_accept**: Allows a worker to accept multiple new connections at once.
* **use epoll**: High-performance I/O method on Linux.

### HTTP Core Settings

```nginx
http {
    sendfile on;
    tcp_nopush on;
    tcp_nodelay on;
    keepalive_timeout 65;
    types_hash_max_size 2048;
    include /etc/nginx/mime.types;
    default_type application/octet-stream;
    ...
}
```

* **sendfile**: Enables zero-copy file transfers.
* **tcp\_nopush / tcp\_nodelay**: Optimize packet handling for performance.
* **keepalive\_timeout**: Duration (in seconds) to keep connections open.
* **types\_hash\_max\_size**: Size of the MIME types hash table.
* **include mime.types**: Loads common MIME mappings.
* **default\_type**: Fallback content type.

### Logging

```nginx
log_format main ...;
access_log /var/log/nginx/access.log main;
error_log  /var/log/nginx/error.log warn;
```

* **log\_format**: Defines a custom format including client IP, user agent, referrer, etc.
* **access\_log**: Path for request logs.
* **error\_log**: Captures warnings and errors.

### Gzip Compression

```nginx
gzip on;
gzip_disable "msie6";
gzip_types text/plain text/css ...;
```

Reduces response sizes for supported MIME types, improving load times on modern browsers.

### Rate Limiting

```nginx
limit_req_zone $binary_remote_addr zone=api_zone:10m rate=10r/s;
```

Sets up a shared memory zone (`api_zone`) limiting each IP to 10 requests/sec. This prevents abusive or accidental flooding.

### SSL/TLS Settings

```nginx
ssl_protocols TLSv1.2 TLSv1.3;
ssl_prefer_server_ciphers on;
ssl_session_cache shared:SSL:10m;
ssl_session_timeout 10m;
```

Enforces strong protocols (1.2+), prioritizes server cipher suites, and caches sessions for faster handshakes.

### Cache Control for Static Assets

```nginx
map $sent_http_content_type $expires {
    default off;
    text/html epoch;
    text/css max;
    application/javascript max;
    ~image/ max;
}
```

Defines expiration policies based on content type:

* HTML: no caching (`epoch`).
* CSS/JS/Images: maximum allowed by client.

### Upstream Backend

```nginx
upstream backend_c_server {
    server 127.0.0.1:3490;
    keepalive 64;
}
```

Defines a pooled connection to the backend service on port 3490.

### HTTP → HTTPS Redirect

```nginx
server {
    listen 80 default_server;
    server_name patatellasharedhomecloud.cloud www.patatellasharedhomecloud.cloud;
    return 301 https://$host$request_uri;
}
```

All HTTP traffic is permanently redirected to HTTPS, preserving host and URI.

### Main HTTPS Server Block

```nginx
server {
    listen 443 ssl http2;
    server_name patatellasharedhomecloud.cloud www.patatellasharedhomecloud.cloud;
    ...
}
```

#### TLS Certificates

```nginx
ssl_certificate     /etc/letsencrypt/live/patatellasharedhomecloud.cloud/fullchain.pem;
ssl_certificate_key /etc/letsencrypt/live/patatellasharedhomecloud.cloud/privkey.pem;
include              /etc/letsencrypt/options-ssl-nginx.conf;
ssl_dhparam          /etc/letsencrypt/ssl-dhparams.pem;
```

Managed by Certbot for automatic renewal and best-practice DH parameters.

#### Security Headers

```nginx
add_header X-Frame-Options "SAMEORIGIN";
add_header X-Content-Type-Options "nosniff";
add_header X-XSS-Protection "1; mode=block";
add_header Referrer-Policy "strict-origin-when-cross-origin";
add_header Content-Security-Policy "default-src 'self'; script-src 'self'; object-src 'none';";
```

Hardens against clickjacking, MIME sniffing, XSS, and controls navigation policies.

#### Static Frontend Serving

```nginx
root /var/www/html;
index index.html;
# try_files $uri /index.html;
```

Serves the React build output. Uncomment the `try_files` line to enable client-side routing fallback.

#### API Reverse Proxy

```nginx
location /api/ {
    limit_req zone=api_zone burst=20 nodelay;
    proxy_pass http://backend_c_server;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
}
```

Forwards `/api/` requests to the upstream pool, with real IP headers and rate limiting.

#### Static Resources Caching

```nginx
location ~* \.(jpg|jpeg|png|gif|ico|css|js|woff|woff2|ttf|svg)$ {
    expires 30d;
    access_log off;
    add_header Cache-Control "public";
}
```

Caches images, fonts, and assets for 30 days, reducing load on the origin.

#### Custom Error Pages

```nginx
error_page 404 /404.html;
location = /404.html { internal; }
error_page 500 502 503 504 /50x.html;
location = /50x.html { internal; }
```

Delivers friendly error documents from the static root.

### Certbot-Managed Redirects

Additional `if` blocks ensure that requests on port 80 for both hostnames get redirected to HTTPS. These are auto-generated by Certbot and can be safely left in place.

---

## Deployment Instructions

1. **Install NGINX & Certbot**:

   ```bash
   sudo apt update
   sudo apt install nginx certbot python3-certbot-nginx
   ```

2. **Place the config** in `/etc/nginx/sites-available/patatella` and symlink to `sites-enabled/`.

3. **Test the configuration**:

   ```bash
   sudo nginx -t
   ```

4. **Reload the service**:

   ```bash
   sudo systemctl reload nginx
   ```

5. **Obtain/Renew certificates**:

   ```bash
   sudo certbot --nginx -d patatellasharedhomecloud.cloud -d www.patatellasharedhomecloud.cloud
   ```

## Testing & Validation

* **HTTPS**: Visit `https://patatellasharedhomecloud.cloud` and check the padlock icon.
* **API**: Curl a test endpoint:

  ```bash
  curl -i https://.../api/health
  ```

* **Caching**: Inspect response headers for `Cache-Control` and `Expires`.

## Troubleshooting

* **502 Bad Gateway**: Ensure backend is running on `127.0.0.1:3490`.
* **Certificate Errors**: Check Certbot logs under `/var/log/letsencrypt`.
* **Too Many Connections**: Tune `worker_connections` or increase `burst` in rate limiting.

## License

This configuration is provided under the MIT License. Feel free to adapt it for your needs.
