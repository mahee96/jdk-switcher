#!/bin/sh

mkdir -p build

CJSON_DIR=$(brew --prefix cjson)

gcc -o build/jdk jdk.c -I"$CJSON_DIR/include" -L"$CJSON_DIR/lib" -lcjson
