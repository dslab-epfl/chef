#!/usr/bin/env sh

# ctl:
apt-get install coreutils python3 python3-netifaces python3-psutil python3-requests python3-yaml qemu

# S²E:
apt-get install build-essential subversion git gettext python-docutils python-pygments nasm unzip wget liblua5.1-dev libsdl1.2-dev libsigc++-2.0-dev binutils-dev libiberty-dev libc6-dev-i386
apt-get build-dep llvm-3.3 qemu

# Chef:
apt-get install gdb strace libdwarf-dev libelf-dev libboost-dev libsqlite3-dev libmemcached-dev libbost-serialization-dev libbost-system-dev

# Build Z3, protobuf and LLVM:
./ctl build -p z3
./ctl install z3
./ctl build -p protobuf
./ctl install protobuf
./ctl build -p llvm

cd ..
for directory in src build expdata vm; do
	setfacl -m user:431:rwx "$directory"
	setfacl -d -m user:431:rwx "$directory"
	setfacl -m user:$(id -u):rwx "$directory"
	setfacl -d -m user:$(id -u):rwx "$directory"
done
if [ "$1" = '--no-keep' ]; then
	cd build/deps/
	for file in *.log *.tar.gz clang compiler-rt llvm/*.log llvm/*.tar.gz llvm/*-native.*; do
		rm -rf "$file"
	done
fi
