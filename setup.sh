#!/usr/bin/env sh

# quit on error:
set -e

# update package tree:
apt-get update

# dependencies for ctl:
apt-get install -y coreutils python3 python3-netifaces python3-psutil python3-requests python3-yaml

# dependencies for S²E:
apt-get install -y build-essential subversion git gettext python-docutils python-pygments nasm unzip wget liblua5.1-dev libsdl1.2-dev libsigc++-2.0-dev binutils-dev libiberty-dev libc6-dev-i386
apt-get build-dep llvm-3.3 qemu

# dependencies for Chef:
apt-get install gdb strace libdwarf-dev libelf-dev libboost-dev libsqlite3-dev libmemcached-dev libbost-serialization-dev libbost-system-dev

# compile Z3, protobuf, LLVM:
./ctl build -p z3
./ctl install z3
./ctl build -p protobuf
./ctl install protobuf
./ctl build -p llvm

# Clean up:
if [ "$1" = '--no-keep' ]; then
	cd ../build/deps/
	for file in *.log *.tar.gz clang compiler-rt llvm/*.log llvm/*.tar.gz llvm/*-native.*; do
		rm -rf "$file"
	done
fi
