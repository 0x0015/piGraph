#!/bin/sh

mkdir em_build
cd em_build
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
make -j16
