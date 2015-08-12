This directory contains the tools necessary to create a custom Linux
kernel instrumented with probes that report process
creation/termination to S2E.

Files:

* qemu-lsmod.txt - The output of `lsmod` when run inside an S2E qemu
  VM.  This is used by the kernel's `make localmodconfig` to compile
  only the modules used by the VM.

* patches/ - The set of patches that instrument the Linux kernel with
  S2E calls.

* build_s2e_kernel.sh - A push-button script that creates kernel .deb
  packages in the current directory.
