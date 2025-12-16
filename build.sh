#!/bin/sh

mkdir -p build

CJSON_DIR=$(brew --prefix cjson)

gcc -o build/jdk jdk.c -I"$CJSON_DIR/include" -L"$CJSON_DIR/lib" -lcjson

# copy to ~/.devtools/bin if it exists
TARGET_DIR="$HOME/.devtools/bin"

if [ -d "$TARGET_DIR" ]; then
    cp build/jdk "$TARGET_DIR/jdk"

    # copy config.json only if it does not already exist
    if [ ! -f "$TARGET_DIR/config.json" ]; then
        cp config.json "$TARGET_DIR/config.json"
    fi
fi
