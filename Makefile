# Makefile
# vim:set noet
#
# For DEBUG
# OLD_SHELL := $(SHELL)
# SHELL = $(warning [$@ ($^) ($?)]) $(OLD_SHELL)
# ifdef DUMP
# 	SHELL = $(warning $@) $(OLD_SHELL) -x
# endif
# print-%: ; @echo '$*=$($*)'

.PHONY: all image run get_binary_from_image static_binary run_test_container ensure_container_running run_tests stop clean

# Detect OS
UNAME_S := $(shell uname -s)

# Detect docker command
ifeq ($(UNAME_S),Linux)
	OS := linux
	IS_DOCKER := $(shell command -v docker >/dev/null && echo $$?)
	IS_CONTAINERD := $(shell command -v nerdctl >/dev/null && echo $$?)
	ifeq ($(IS_DOCKER),0)
		DOCKER := docker
	else ifeq ($(IS_CONTAINERD),0)
		DOCKER := nerdctl --
	else
		$(error Containers not running on $(OS))
	endif
	MKDIR := mkdir -p
	SEP := /
else ifeq ($(UNAME_S),Darwin)
	OS := macos
	IS_DOCKER := $(shell command -v docker >/dev/null && echo 0 || echo 1)
	IS_COLIMA_CONTAINERD := $(shell command -v colima >/dev/null && echo 0 || echo 1)
	ifeq ($(IS_DOCKER),0)
		DOCKER := docker
	else ifeq ($(IS_COLIMA_CONTAINERD),0)
		DOCKER := colima nerdctl --
	else
		$(error Containers not running on $(OS))
	endif
	MKDIR := mkdir -p
	SEP := /
else
	$(error Unsupported OS: $(UNAME_S))
endif

ARCH := unknown
DEBUG ?= NO
ENVIRONMENT ?= dev
TEST_HOSTNAME ?= localhost
PORT ?= 8080
IMAGE_NAME ?= about
CONTAINER_NAME ?= about
PARALLELISM ?= 10
NUMBER_OF_LINES ?= 100
CYCLES ?= 1
FILES_DIR ?= files

# Detect environment
ifeq ($(ENVIRONMENT),dev)
	DEBUG := YES
else ifeq ($(ENVIRONMENT),staging)
	DEBUG := YES
else ifeq ($(ENVIRONMENT),prod)
	DEBUG := NO
else
	$(error Unsupported ENV: $(ENVIRONMENT); use dev, staging, or prod)
endif

ifeq ($(TEST_HOSTNAME),localhost)
	IS_RUNNING_LOCAL := $(shell $(DOCKER) ps | awk '{print $2}' | grep $(CONTAINER_NAME) >/dev/null 2>&1 && echo 1 || echo 0)
	IS_RUNNING_REMOTE := 10
else
	HOST_REACHABLE := $(shell ping -c 3 $(TEST_HOSTNAME) >/dev/null 2>&1 && echo 1 || echo 0)
	CONTAINER_RUNNING := $(shell [ $(HOST_REACHABLE) -eq 1 ] && curl -L -s -o /dev/null -w "%{http_code}" http://$(TEST_HOSTNAME):$(PORT))
	IS_RUNNING_REMOTE := $(shell [ "$(CONTAINER_RUNNING)" == "200" ] && echo 1 || echo 0)
	IS_RUNNING_LOCAL := 10
endif

all: image run

image:
	@echo "Running 'image' stage."
	@echo "docker command is '${DOCKER}'"
	${DOCKER} image build -f Containerfile -t $(IMAGE_NAME) .

clear_failure:
	@echo "Running 'clear_failure' stage."
	-@${DOCKER} stop about_static_binary >/dev/null

get_binary_from_image: image clear_failure
	@echo "Running 'get_binary_from_image' stage."
	@mkdir -p bin && \
		${DOCKER} run --name about_static_binary --rm -d about >/dev/null && \
		${DOCKER} cp about_static_binary:/simple-http-server-mt \
			./bin/simple-http-server-mt >/dev/null && \
		${DOCKER} images --format '{{if eq .Repository "$(IMAGE_NAME)"}}{{ .ID }}{{end}}' >.temp
	-@${DOCKER} stop about_static_binary >/dev/null

image_arch: get_binary_from_image
	@echo "Running 'image_arch' stage."
	$(eval IMAGEID := $(shell cat .temp))
	@${DOCKER} inspect -f '{{ .Architecture }}{{if .Variant}}{{ .Variant }}{{end}}-{{ .Os }}' $(IMAGEID) >./bin/.arch
	-@rm -f .temp

static_binary: image_arch
	@echo "Running 'static_binary' stage."
	@echo "static binary for Linux in ./bin"
	$(eval ARCH := $(shell head -1 ./bin/.arch))
	@mv ./bin/simple-http-server-mt ./bin/simple-http-server-mt-$(ARCH)-static && \
		cd ./bin && echo *

run:
	@echo "Running 'run' stage."
	@echo "Running on OS: $(OS), ENVIRONMENT: $(ENVIRONMENT), CONTAINER_NAME: $(CONTAINER_NAME)"
	${DOCKER} run --rm \
		--name $(CONTAINER_NAME) \
		--env DEBUG=$(DEBUG) \
		-p 8080:8080 \
		-v ./images/traefik-proxy.ico:/favicon.ico:ro \
		-v ./res/:/res/:ro \
		-v ./index-themes.html:/index.html:ro \
		-v ./404.html:/404.html:ro \
		$(IMAGE_NAME)

run_test_container:
	@echo "Running 'run_test_container' stage."
	@echo "Running on OS: $(OS), ENVIRONMENT: $(ENVIRONMENT), CONTAINER_NAME: $(CONTAINER_NAME)"
	mkdir -p ./tests/$(FILES_DIR)
	${DOCKER} run -d --rm \
		--name $(CONTAINER_NAME) \
		--env DEBUG=$(DEBUG) \
		-p 8080:8080 \
		-v ./images/traefik-proxy.ico:/favicon.ico:ro \
		-v ./tests/files/:/files/:ro \
		$(IMAGE_NAME)

ifeq ($(IS_RUNNING_LOCAL),1)
ensure_container_running:
	@echo "Running 'ensure_container_running' stage: local container running"
else ifeq ($(IS_RUNNING_LOCAL),0)
ensure_container_running: image run_test_container
	@echo "Running 'ensure_container_running' stage: local container running"
else ifeq ($(IS_RUNNING_REMOTE),1)
ensure_container_running:
	$(info Running remote tests, some server is running on target port)
else
ensure_container_running:
	$(error Running remote tests, remote server is not reachable on target port)
endif

run_tests: ensure_container_running
	@echo "Running 'run_tests' stage."
	@echo "Running tests on OS: $(OS), ENVIRONMENT: $(ENVIRONMENT), CONTAINER_NAME: $(CONTAINER_NAME)"
	cd ./tests && \
		DEBUG=$(DEBUG) \
		PARALLELISM=$(PARALLELISM) \
		NUMBER_OF_LINES=$(NUMBER_OF_LINES) \
		HOSTNAME=$(TEST_HOSTNAME) \
		PORT=$(PORT) \
		FILES_DIR=$(FILES_DIR) \
		./test_urls.sh $(CYCLES)

stop:
	@echo "Running 'stop' stage."
	@echo "Stopping container: $(CONTAINER_NAME)"
	-${DOCKER} stop $(CONTAINER_NAME)

clean: stop
	@echo "Running 'clean' stage."
	rm -rf ./bin/
	rm -rf ./tests/urls.txt
	rm -rf ./tests/log
	rm -rf ./tests/files
	rm -rf ./tests/*.tmp

superclean: clean
	@echo "Running 'superclean' stage."
	-${DOCKER} rmi -f $(CONTAINER_NAME)