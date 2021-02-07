#!/bin/sh

set -e

cd $(dirname $0)

docker run --rm \
           -v $(pwd):/myplugin fopina/fluent-bit-plugin-dev:v1.6.10-1 \
           sh -c "cmake -DFLB_SOURCE=/usr/src/fluentbit/fluent-bit-1.6.10/ \
                 -DPLUGIN_NAME=out_influxdb_v2 ../ && make"

docker run --rm \
           -v $(pwd)/build:/myplugin fluent/fluent-bit:1.6.10 \
           /fluent-bit/bin/fluent-bit -v \
           -q -f 1 \
           -e /myplugin/flb-out_influxdb_v2.so \
           -i mem \
           -o stdout -m '*' \
           -o exit -m '*'
