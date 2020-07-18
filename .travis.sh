#!/bin/sh

set -x

die(){
  echo "ERROR"
  exit -1
}

# export DOWNLOAD_GCC_TOOLCHAIN="http://musl.cc/arm-linux-musleabihf-cross.tgz"
# export COMPILER="$PWD/build/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf-gcc"

# echo "downloading $DOWNLOAD_GCC_TOOLCHAIN"
# mkdir -p build ||die
# cd build
# curl "$DOWNLOAD_GCC_TOOLCHAIN" --output cc_toolchain.tar.gz ||die
# tar -xzf cc_toolchain.tar.gz ||die
# cd ..

# rm -fR build/bin
# mkdir -p build/bin
# ln -s "$PWD/build/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf-gcc" "$PWD/build/bin/arm-linux-gnueabihf-gcc"
# ln -s "$PWD/build/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf-g++" "$PWD/build/bin/arm-linux-gnueabihf-g++"
# export PATH="$PWD/build/bin:$PATH"

echo "building..."
# make SHELL="/bin/bash -x " CFLAGS=" -D__off64_t=off64_t " ||die
arm-linux-gnueabihf-gcc -O2 -o mbc mbc.c ||die
