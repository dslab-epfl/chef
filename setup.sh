#!/bin/sh

THIS_DIR="$(readlink -f "$(dirname "$0")")"

# quit on error:
set -e

# update package tree:
sudo apt-get update

# dependencies for ctl:
sudo apt-get install -y coreutils python3 python3-netifaces python3-psutil python3-requests python3-yaml

# dependencies for S²E:
sudo apt-get install -y build-essential subversion git gettext python-docutils python-pygments nasm unzip wget liblua5.1-dev libsdl1.2-dev libsigc++-2.0-dev binutils-dev libiberty-dev libc6-dev-i386
sudo apt-get build-dep llvm-3.3 qemu

# dependencies for Chef:
sudo apt-get install gdb strace libdwarf-dev libelf-dev libboost-dev libsqlite3-dev libmemcached-dev libboost-serialization-dev libboost-system-dev

# compile and install Z3:
"$THIS_DIR"/ctl build -p z3
cd "$CHEFROOT_BUILD_DEPS/z3"
sudo make -C build install

# compile and install protobuf:
"$THIS_DIR"/ctl build -p protobuf
cd "$CHEFROOT_BUILD_DEPS/protobuf"
sudo make install
sudo ldconfig

# compile LLVM:
"$THIS_DIR"/ctl build -p llvm

# Clean up:
if [ "$1" = '--no-keep' ]; then
	cd "$THIS_DIR"/../build/deps/
	for file in *.log *.tar.gz clang compiler-rt llvm/*.log llvm/*.tar.gz llvm/*-native.*; do
		rm -rf "$file"
	done
fi
