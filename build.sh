#!/bin/sh

set -x

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

echo "building..."
cd build
arm-linux-musleabihf-gcc -std=c99 -D_XOPEN_SOURCE=700 -static -O2 -o mbc ../mbc.c ||die
arm-linux-musleabihf-strip mbc ||die
mkdir -p hook/action ||die
echo "#!/bin/bash" > "hook/action/__unnamed__" ||die
echo "./mbc \$@"  >> "hook/action/__unnamed__" ||die
tar -czf mbc.tar.gz mbc ./hook/action/__unnamed__
cd ..

