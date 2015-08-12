#!/bin/bash
#
# Copyright 2015 EPFL. All rights reserved.

THIS_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
PATCH_DIR=${THIS_DIR}/patches
LSMOD_FILE=${THIS_DIR}/qemu-lsmod.txt

set -e
set -x

sudo apt-get update
sudo apt-get install build-essential fakeroot
sudo apt-get build-dep linux

# This is a no-op if the source code is already there
apt-get source linux

SRC_DIR=./linux-*/

pushd $SRC_DIR

for PATCH in ${PATCH_DIR}/*.patch; do
    patch -N -p1 <${PATCH}
done

sed -i 's/^\(abiname: .*\)-s2e/\1/' debian/config/defines
sed -i 's/^\(abiname: .*\)/\1-s2e/' debian/config/defines

fakeroot debian/rules debian/control-real || echo "Assuming success, moving on..."

ARCH=i386_none_486
REAL_ARCH=i386_none_real

make -f debian/rules.gen setup_${ARCH}
make -C debian/build/build_${ARCH} LSMOD="${LSMOD_FILE}" localmodconfig

fakeroot make -f debian/rules.gen binary-arch_${ARCH} -j$(nproc)
fakeroot make -f debian/rules.gen binary-arch_${REAL_ARCH} -j$(nproc)

popd # $SRC_DIR
