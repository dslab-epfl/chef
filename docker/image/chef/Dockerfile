# S²E-Chef container

FROM ubuntu:14.04
MAINTAINER Tinu Weber <martin.weber@epfl.ch>

# Place at the same level as the chef repository (named `src`), then launch with
# something like: `docker build --rm -t='dslab/s2e-chef:v0.7' .`

# Install base:
RUN apt-get install -y sudo

# Setup user (TODO replace login shell by custom script):
RUN groupadd -r -g 666 s2e
RUN useradd  -r -g s2e -u 666 -d /home/s2e -m -s /sbin/nologin -c "S2E image user" s2e
RUN echo 's2e ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/s2e

# Setup qemu, since it is not going to be installed:
RUN groupadd -g 78 kvm
RUN usermod  -a -G kvm s2e

# Run Chef setup:
ADD src/setup.sh                            /opt/s2e/chef/src/
ADD src/ctl                                 /opt/s2e/chef/src/
ADD src/ctltools/build.sh                   /opt/s2e/chef/src/ctltools/
ADD src/ctltools/utils.sh                   /opt/s2e/chef/src/ctltools/
ADD src/llvm/llvm-3.2-memorytracer.patch    /opt/s2e/chef/src/llvm/
ADD src/llvm/clang-3.2-memorytracer.patch   /opt/s2e/chef/src/llvm/
ADD src/llvm/compiler-rt-3.2-asan4s2e.patch /opt/s2e/chef/src/llvm/
RUN /opt/s2e/chef/src/setup.sh

# Switch to user
USER s2e
WORKDIR /home/s2e
