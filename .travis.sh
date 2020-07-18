#!/bin/sh

set -x

die(){
  echo "ERROR"
  exit -1
}

export DOWNLOAD_GCC_TOOLCHAIN="http://musl.cc/arm-linux-musleabihf-cross.tgz"
export PATH="$PWD/build/arm-linux-musleabihf-cross/bin:$PATH"

echo "downloading $DOWNLOAD_GCC_TOOLCHAIN"
mkdir -p build ||die
cd build
curl "$DOWNLOAD_GCC_TOOLCHAIN" --output cc_toolchain.tar.gz ||die
tar -xzf cc_toolchain.tar.gz ||die
cd ..


echo "building..."
cd build
arm-linux-musleabihf-gcc -O2 -o mbc ../mbc.c ||die
arm-linux-musleabihf-strip mbc ||die
cd ..

