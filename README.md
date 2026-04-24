[//]: # (README.md)
[//]: # (vim:set ft=markdown)
[//]: # (SPDX-License-Identifier: MIT)

# Lightweight oversimplified multithreaded HTTP-only server

Server with tiny footprint which serves only HTTP/1.1 GET requests on port given as command line during startup (default 8080, if command line argument is absent). Typical use is running in docker container made from scratch (only 50 Kbytes small) with files to be served mounted to the `/` with `--volume` argument. It will serve `index.html` for request with path `/`, so it is a good idea to add at least `index.html` to the container.

**WARNING: it is WIP for now, some debug messages are adding. This code is no guaranteed working.**

## Why

Project was born from simple demand to serve custom error page while using with [Traefik](http://traefik.io) reverse-proxy server. I would prefer Rust, to be honest, but the goal was to get minimal memory footprint. May be in the future I will also try Rust and compare results. This will be fun, for sure )

## Installing

You can checkout repository and start things up using `make`. Supplied `Makefile` contains basic scenarios. Without arguments, `make` will build image and start demo container locally with `docker` or `containerd`.

```shell
$ git clone https://github.com/timaliev/about_http.git
$ cd simple_http_server
$ make
```

Compilation and static linking is done in `Alpine` container. It is possible to get static executable file from container image with `make` target `static_binary`:

```shell
$ cd simple_http_server
$ make static_binary
...
$ ./bin/simple-http-server-mt-arm64v8-linux-static # Example
...
```

## Running

You can run server locally (for testing purposes) or in `docker-compose`/`k8s` environments.

To run it without container (OK for local testing deployments only), you can get static binaries in `./bin` (see Installing above).

WARNING: only about 30 MIME types are implemented with very basic detection by file extension. So it is very possible that your data will be given default `application/octet-stream` MIME-type.

## Debugging

It is possible to run server in debug mode, where each thread will report something at `stderr`, which possibly could be pretty noisy. You can turn debug mode by creating environment variable `DEBUG=yes`. This is turned on for `dev` environment (which is default for `Makefile`), so to disable debug mode set `DEBUG=no` (or any other value then `yes` or `1`).

```shell
$ make run
Running on OS: macos, ENVIRONMENT: dev, CONTAINER_NAME: about
...
<very verbose>
...
$ DEBUG=no make run
Running on OS: macos, ENVIRONMENT: dev, CONTAINER_NAME: about
...<Silence>...
```

## Testing

There is supplied performance test in `./tests` directory. It can be started with `make` like this:

```shell
$ make run_tests
```

By default it will generate some random binary files, create URLs list, start local container with mounted generated files, and try to get those files through web server in container, along with some (sometimes erroneous) simple URLs, like `index.html` or `index.htm` (which is absent, so some 404 errors will be generated).

You can vary test volume and load on web-server by manipulating environment variables while running `make`:

```shell
# This will produce very small test traffic
$ PARALLELISM=3 NUMBER_OF_LINES=10 CYCLES=1 make run_tests
```

For now this is it, see `Containerfile` `Makefile`, `test_urls.sh` and code in `simple_http_server_mt.c` for details.

Thank you and enjoy!
