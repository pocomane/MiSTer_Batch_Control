#!/bin/sh

set -x

if [ "$BUILD_MODE" = "test" ] ; then
  set -e
  chmod ugo+x ./build/release/mbc
  ./build/release/mbc
  exit 0
fi

die(){
  echo "ERROR"
  exit -1
}

export DOWNLOAD_GCC_TOOLCHAIN="http://musl.cc/arm-linux-musleabihf-cross.tgz"
export PATH="$PWD/build/arm-linux-musleabihf-cross/bin:$PATH"

if [ "$(ls build/arm-linux-musle*)" = "" ] ; then
  echo "downloading $DOWNLOAD_GCC_TOOLCHAIN"
  mkdir -p build ||die
  cd build
  curl "$DOWNLOAD_GCC_TOOLCHAIN" --output cc_toolchain.tar.gz ||die
  tar -xzf cc_toolchain.tar.gz ||die
  cd ..
fi

BFLAG=" -std=c99 -Wall -D_XOPEN_SOURCE=700 -static -O2 "
COMMIT=$(git rev-parse HEAD)
BFLAG=" $BFLAG -DMBC_BUILD_COMMIT=\"$COMMIT\" "
DATE=$(date --rfc-3339=seconds | tr ' ' '/')
BFLAG=" $BFLAG -DMBC_BUILD_DATE=\"$DATE\" "

echo "building..."
cd build
arm-linux-musleabihf-gcc $BFLAG -o mbc ../mbc.c ||die
arm-linux-musleabihf-strip mbc ||die
mkdir -p hook/expose ||die
cd hook/expose ||die
ln -s ../../mbc __unnamed__ ||die
cd -
tar -czf mbc.tar.gz mbc hook/expose/__unnamed__
mkdir release
cp mbc release/
cp mbc.tar.gz release/
cd ..

