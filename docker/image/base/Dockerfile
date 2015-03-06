# S2E base container

FROM ubuntu:14.04
MAINTAINER Stefan Bucur <stefan.bucur@epfl.ch>

# Tool dependencies
RUN apt-get update && apt-get install -y build-essential subversion git gettext python-docutils python-pygments nasm unzip wget

# Library dependencies
RUN apt-get install -y liblua5.1-dev libsdl1.2-dev libsigc++-2.0-dev binutils-dev libiberty-dev libc6-dev-i386

# LLVM dependencies
RUN apt-get build-dep -y llvm-3.3

# Qemu dependencies
RUN apt-get build-dep -y qemu

# Build & install Z3 (doesn't need LLVM)
RUN mkdir -p /build && cd /build &&\
  wget -O z3.zip 'http://download-codeplex.sec.s-msft.com/Download/SourceControlFileDownload.ashx?ProjectName=z3&changeSetId=dd62ca5eb36c2a62ee44fc5a79fc27c883de21ae' &&\
  unzip -d z3 z3.zip && rm z3.zip &&\
  cd z3 &&\
  python scripts/mk_make.py && make -C build -j4 && make -C build install &&\
  rm -rf /build

# Download & patch the LLVM source code
ADD llvm-3.2-memorytracer.patch /opt/s2e/llvm/patches/
ADD clang-3.2-memorytracer.patch /opt/s2e/llvm/patches/
ADD compiler-rt-3.2-asan4s2e.patch /opt/s2e/llvm/patches/
ADD buildllvm.sh /opt/s2e/llvm/patches/

RUN /opt/s2e/llvm/patches/buildllvm.sh fetch

# Build the target LLVM
RUN /opt/s2e/llvm/patches/buildllvm.sh build release
RUN /opt/s2e/llvm/patches/buildllvm.sh build debug

# At this point we're left with three directories in /opt/s2e/llvm:
# - llvm-3.2-native
# - llvm-3.2.src
# - llvm-3.2.build
