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
  gcc -g -w -Os -static -o simple-http-server-mt simple_http_server_mt.c; \
  strip -s -R .comment --strip-unneeded simple-http-server-mt; \
  chmod -cR 755 simple-http-server-mt; \
  chown -cR 0:0 simple-http-server-mt

# Optional: verify static linking
RUN file simple-http-server-mt; \
  ! ldd simple-http-server-mt

FROM scratch

COPY --from=build-static simple-http-server-mt /
ADD index.html /

USER 65534
EXPOSE 8080
CMD ["/simple-http-server-mt", "8080"]
