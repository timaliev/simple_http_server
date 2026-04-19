all: dockerimage

dockerimage:
	docker image build -f Containerfile -t about .

run:
	docker run -d --rm -p 8080:8080 -v ./traefik-proxy.ico:/favicon.ico:ro about

clean:
	docker rmi -f about