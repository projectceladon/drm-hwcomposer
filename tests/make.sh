#!/bin/bash
./prebuild.sh
export IGT_DIR=./igt-gpu-tools
make build
make
