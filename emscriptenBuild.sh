#!/bin/sh

mkdir em_build
cp -r piCalc em_build/piCalc
cd em_build
cd piCalc
make static -j16 CC=emcc CXX=em++ AR=emar BUILD_CXX_FLAGS="-std=c++20 -O2"
cd ..
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
make -j16
