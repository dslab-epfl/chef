#!/bin/sh
# Usage: ./setup.sh [--no-keep]
set -e

# check UID:
if [ "$(id -u)" != '0' ]; then
	echo 'Please Run this script as root.'
	exit 1
fi

# fix PWD:
cd "$(readlink -f "$(dirname "$0")")"

# update package tree:
apt-get update -y

# dependencies for S²E:
apt-get install -y build-essential subversion git gettext python-docutils python-pygments nasm unzip wget liblua5.1-dev libsdl1.2-dev libsigc++-2.0-dev binutils-dev libiberty-dev libc6-dev-i386
apt-get build-dep -y llvm-3.3 qemu

# compile and install Z3:
./ctl build -p z3
cd ../build/deps/z3/
make -C build install
cd -

# compile LLVM:
./ctl build -p llvm

# Clean up:
if [ "$1" = '--no-keep' ]; then
	cd ../build/deps/
	for file in *.log *.tar.gz clang compiler-rt llvm/*.log llvm/*.tar.gz llvm/*-native.*; do
		rm -rf "$file"
	done
	cd -
fi

# Add user to kvm group:
if ! getent group kvm; then
	groupadd -r -g 78 kvm
fi
if ! getent group kvm | grep $(id -un); then
    if [ $(id -u) -eq 0 ]; then
        usermod -a -G kvm root
    else
        usermod -a -G kvm $(id -u)
    fi
fi

# dependencies for Chef:
apt-get install -y gdb strace libdwarf-dev libelf-dev libboost-dev libsqlite3-dev libmemcached-dev libboost-serialization-dev libboost-system-dev

# compile and install protobuf:
./ctl build -p protobuf
cd ../build/deps/protobuf/
make install
ldconfig
cd -

# dependencies for ctl:
apt-get install -y coreutils python3 python3-netifaces python3-psutil python3-requests python3-yaml parallel
