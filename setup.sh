#!/bin/sh
# Usage: ./setup.sh [--no-keep]
set -e

# check UID:
if [ "$(id -u)" = '0' ]; then
	echo 'Please do not run this script as root.'
	exit 1
fi

# fix PWD:
cd "$(readlink -f "$(dirname "$0")")"

# update package tree:
sudo apt-get update -y

# dependencies for S²E:
sudo apt-get install -y build-essential subversion git gettext python-docutils python-pygments nasm unzip wget liblua5.1-dev libsdl1.2-dev libsigc++-2.0-dev binutils-dev libiberty-dev libc6-dev-i386
sudo apt-get build-dep -y llvm-3.3 qemu

# compile and install Z3:
./ctl build -p z3
cd ../build/deps/z3/
sudo make -C build install
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
	sudo groupadd -r -g 78 kvm
fi
if ! getent group kvm | grep $(id -un); then
	sudo usermod -a -G kvm $(id -u)
fi

# dependencies for Chef:
sudo apt-get install -y gdb strace libdwarf-dev libelf-dev libboost-dev libsqlite3-dev libmemcached-dev libboost-serialization-dev libboost-system-dev

# compile and install protobuf:
./ctl build -p protobuf
cd ../build/deps/protobuf/
sudo make install
sudo ldconfig
cd -

# dependencies for ctl:
sudo apt-get install -y coreutils python3 python3-netifaces python3-psutil python3-requests python3-yaml parallel
