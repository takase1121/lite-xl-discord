#!/usr/bin/env sh

gcc -g -O0 -static -nostdlib -nostdinc -fno-pie -no-pie -mno-red-zone \
  -fno-omit-frame-pointer -pg -mnop-mcount \
  -o rpc.com.dbg rpc.c -fuse-ld=bfd -Wl,-T,ape.lds \
  -include cosmopolitan.h crt.o ape.o cosmopolitan.a
objcopy -S -O binary rpc.com.dbg rpc.com
