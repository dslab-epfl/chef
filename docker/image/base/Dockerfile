# S2E base container

FROM ubuntu:14.04
MAINTAINER Stefan Bucur <stefan.bucur@epfl.ch>

# Tool dependencies
RUN apt-get update && apt-get install -y build-essential subversion git gettext python-docutils python-pygments nasm unzip wget qemu-kvm

# Library dependencies
RUN apt-get install -y liblua5.1-dev libsdl1.2-dev libsigc++-2.0-dev binutils-dev libiberty-dev libc6-dev-i386

# LLVM dependencies
RUN apt-get build-dep -y llvm-3.3

# Qemu dependencies
RUN apt-get build-dep -y qemu

# Build and install Z3 and LLVM:
ADD chef/libccli/build.sh                    /opt/s2e/chef/libccli/
ADD chef/libccli/utils.sh                    /opt/s2e/chef/libccli/
ADD chef/llvm/llvm-3.2-memorytracer.patch    /opt/s2e/chef/llvm/
ADD chef/llvm/clang-3.2-memorytracer.patch   /opt/s2e/chef/llvm/
ADD chef/llvm/compiler-rt-3.2-asan4s2e.patch /opt/s2e/chef/llvm/
RUN /opt/s2e/chef/ccli.py build -p llvm
RUN /opt/s2e/chef/ccli.py install z3

# At this point, we've got:
# /opt/s2e/chef-data/build/llvm/z3
# /opt/s2e/chef-data/build/llvm/clang
# /opt/s2e/chef-data/build/llvm/compiler-rt
# /opt/s2e/chef-data/build/llvm/llvm-3.2-native
# /opt/s2e/chef-data/build/llvm/llvm-3.2.build-release
# /opt/s2e/chef-data/build/llvm/llvm-3.2.build-debug
# /opt/s2e/chef-data/build/llvm/llvm-3.2.src
# + a bunch of temporary build files

