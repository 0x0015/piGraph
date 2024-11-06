#!/bin/sh

mkdir build
cp -r piCalc build/piCalc
cd build
cd piCalc
make static -j16
cd ..
cmake ..
make -j16
