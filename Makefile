# Makefile
#
#
# DEBUG
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

all: image run

ifeq ($(TEST_HOSTNAME),localhost)
	# Detect if container image is already running
	CONTAINER_RUNNING := $(shell $(DOCKER) ps | awk '{print $2}' | grep $(CONTAINER_NAME))
	IS_RUNNING := $(shell [ -n "$(CONTAINER_RUNNING)" ] && echo 1 || echo 0)
 ifeq ($(IS_RUNNING),1)
ensure_container_running:
	@echo "Test container '$(CONTAINER_NAME)' is already started in background"
 else
ensure_container_running: image run_test_container
	@echo "Started test container '$(CONTAINER_NAME)' in background. Use 'make stop' to stop it."
 endif
else
	# Container is remote
	CONTAINER_RUNNING := $(shell curl -L -s -o /dev/null -w "%{http_code}" http://$(TEST_HOSTNAME):$(PORT))
	IS_RUNNING := $(shell [ "$(CONTAINER_RUNNING)" == "200" ] && echo 1 || echo 0)
 ifeq ($(IS_RUNNING),1)
ensure_container_running:
	@echo "Test container is running on remote host and available at http://$(TEST_HOSTNAME):$(PORT)"
 else
ensure_container_running:
	$(error "Looks like test container is not running at http://$(TEST_HOSTNAME):$(PORT)")
 endif
endif

image:
	@echo "docker command is ${DOCKER}"
	${DOCKER} image build -f Containerfile -t $(IMAGE_NAME) .

clear_failure:
	-@${DOCKER} stop about_static_binary >/dev/null

get_binary_from_image: image clear_failure
	@mkdir -p bin && \
	  ${DOCKER} run --name about_static_binary --rm -d about >/dev/null && \
		${DOCKER} cp about_static_binary:/simple-http-server-mt \
		  ./bin/simple-http-server-mt >/dev/null && \
		${DOCKER} images --format '{{if eq .Repository "$(IMAGE_NAME)"}}{{ .ID }}{{end}}' >.temp
	-@${DOCKER} stop about_static_binary >/dev/null

image_arch: get_binary_from_image
	$(eval IMAGEID := $(shell cat .temp))
	@${DOCKER} inspect -f '{{ .Architecture }}{{if .Variant}}{{ .Variant }}{{end}}-{{ .Os }}' $(IMAGEID) >./bin/.arch
	-@rm -f .temp

static_binary: image_arch
	@echo "static binary for Linux in ./bin"
	$(eval ARCH := $(shell head -1 ./bin/.arch))
	@mv ./bin/simple-http-server-mt ./bin/simple-http-server-mt-$(ARCH)-static && \
		cd ./bin && echo *

run:
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
	@echo "Running on OS: $(OS), ENVIRONMENT: $(ENVIRONMENT), CONTAINER_NAME: $(CONTAINER_NAME)"
	${DOCKER} run -d --rm \
		--name $(CONTAINER_NAME) \
    --env DEBUG=$(DEBUG) \
	  -p 8080:8080 \
		-v ./images/traefik-proxy.ico:/favicon.ico:ro \
		-v ./tests/files/:/files/:ro \
		$(IMAGE_NAME)

run_tests: ensure_container_running
	@echo "Running tests on OS: $(OS), ENVIRONMENT: $(ENVIRONMENT), CONTAINER_NAME: $(CONTAINER_NAME)"
	cd ./tests && \
	  DEBUG=$(DEBUG) \
 	  PARALLELISM=$(PARALLELISM) \
    NUMBER_OF_LINES=$(NUMBER_OF_LINES) \
    HOSTNAME=$(TEST_HOSTNAME) \
    PORT=$(PORT) \
    ./test_urls.sh $(CYCLES)

stop:
	@echo "Stopping container: $(CONTAINER_NAME)"
	-${DOCKER} stop $(CONTAINER_NAME)

clean: stop
	-${DOCKER} rmi -f $(CONTAINER_NAME)
	rm -rf ./bin/
	rm -rf ./tests/urls.txt
	rm -rf ./tests/log
	rm -rf ./tests/files
