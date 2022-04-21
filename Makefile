dev:
	docker build -f Dockerfile.build -o dist .

run: dev
	docker run --rm \
			-v $(PWD)/dist:/myplugin \
			fluent/fluent-bit:1.9.2 \
			/fluent-bit/bin/fluent-bit \
			-f 1 \
			-e /myplugin/flb-in_diskfree.so \
			-i diskfree $(EXTRA_ARGS)\
			-o stdout -m '*' \
			-o exit -m '*'

testv: EXTRA_ARGS=-vv
testv: run

testvsingle: EXTRA_ARGS=-vv -p mount_point=/
testvsingle: run

testvfs: EXTRA_ARGS=-vv -p fs_type=overlay
testvfs: run

testvall: EXTRA_ARGS=-vv -p show_all=on
testvall: run

test: EXTRA_ARGS=-q
test: run
	docker run --rm \
	           alpine:3.12 \
	           sh -c \
			   "stat -f /; df -B4096"

alltests: testv testvsingle testvfs testvall
