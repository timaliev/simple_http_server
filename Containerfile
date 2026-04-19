# Containerfile
# vim:set ft=dockerfile
# 
# FROM alpine:3.20 AS build-static
# 
# https://github.com/awesome-containers/alpine-build-essential
ARG BUILD_ESSENTIAL_VERSION=3.17
ARG BUILD_ESSENTIAL_IMAGE=ghcr.io/awesome-containers/alpine-build-essential

FROM $BUILD_ESSENTIAL_IMAGE:$BUILD_ESSENTIAL_VERSION AS build-static

# Install musl tools and gcc
RUN apk add --no-cache \
    gcc \
    musl-dev \
    make

WORKDIR /

# Copy source
COPY simple_http_server_mt.c .

# Build statically with musl-gcc
# Make it executable and owned by root
RUN set -xeu; \
  gcc -g -w -Os -static -o simple_http_server_mt simple_http_server_mt.c; \
  strip -s -R .comment --strip-unneeded simple_http_server_mt; \
  chmod -cR 755 simple_http_server_mt; \
  chown -cR 0:0 simple_http_server_mt

# Optional: verify static linking
RUN file simple_http_server_mt; \
  ! ldd simple_http_server_mt

FROM scratch

COPY --from=build-static simple_http_server_mt /
ADD index.html /

USER 0
EXPOSE 8080
CMD ["/simple_http_server_mt", "8080"]
