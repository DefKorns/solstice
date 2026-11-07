FROM alpine:3.19
RUN apk add --no-cache build-base cpio xz kmod tar gzip wget \
 && wget -qO- https://github.com/osm0sis/mkbootimg/archive/8f1843e.tar.gz | tar xz \
 && make -C mkbootimg-8f1843e* CFLAGS=-w \
 && cp mkbootimg-8f1843e*/mkbootimg mkbootimg-8f1843e*/unpackbootimg /usr/local/bin/ \
 && rm -rf mkbootimg-8f1843e*
