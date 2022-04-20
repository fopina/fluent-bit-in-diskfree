dev:
	docker build -f Dockerfile.build -o dist .

test: dev
	docker run --rm \
			-v $(PWD)/dist:/myplugin \
			fluent/fluent-bit:1.9.2 \
			/fluent-bit/bin/fluent-bit -v \
			-f 1 \
			-e /myplugin/flb-in_diskfree.so \
			-i diskfree \
			-o stdout -m '*' \
			-o exit -m '*'
	docker run --rm \
	           alpine:3.12 \
	           sh -c \
			   "cat /proc/meminfo | grep -e ^MemAvailable -e ^MemFree -e ^MemTotal -e ^Buffers -e ^Cached"
