Getting Started
===============

This is a brief overview to get, build and run Chef.


Chef Command Line Interface
---------------------------

Chef is mostly controlled through the *Chef Command Line Interface* (*ccli*),
whose main script lies at the project's root (`ccli.py`). It automates many
Chef-related processes, including compiling and running Chef.

*ccli* itself is a collection of scripts, where each script interacts with a
different part of Chef; they are written in python and/or POSIX sh and are
available as a subcommand to *ccli*.

In this document, we invoke the main script as `ccli`, which is achieved by
adding a symbolic link to `ccli.py` from one of the locations in `PATH`. It is
not mandatory, but it allows you to type

	ccli ...

instead of

	path/to/s2e-chef/ccli.py ...

Run `ccli help` to get a list of all *ccli*-subcommands.


Preparation
-----------

### Docker

Chef is rather picky about its dependencies, and preparing a machine to make
Chef compile or run may be a cumbersome task. To avoid battling with the system,
we provide an easy way to wrap all "tricky" operations inside a
[docker](https://docker.io) container that is based on an Ubuntu 14.04 docker
image and satisfies all dependencies.

*ccli* can be told to use docker as a backend for launching processes like the
compilation, or running Chef; it is thus recommended to install docker:

	sudo aptitude install acl lxc-docker

After the installation, you may want to add yourself to the `docker` group to
avoid requiring root permissions for all docker related actions:

	sudo usermod -a -G docker $(id -un)

This will require you to restart your current user session (or temporarily
`newgrp docker`).

### Dependencies

Assuming we interact with S2E through *ccli* and docker, the dependencies can be
installed with

	aptitude install acl \
	                 coreutils \
	                 lxc-docker \
	                 python3 \
	                 python3-netifaces \
	                 python3-psutils \
	                 python3-requests \
	                 python3-yaml \
	                 qemu-kvm

Note that these are the package names for Ubuntu; for a list of required
packages for other distributions, please see the [Readme](README.md).

### Hardware virtualisation

In order to profit from better performance in qemu, it is recommended to enable
hardware virtualisation (Intel VT-X, AMD-V) on your machine.

### Source

	git clone https://github.com/stefanbucur/chef

### Docker image

Assuming we use the docker backend, the next step is to fetch the prepared
docker image:

	$ ccli docker fetch
	Pulling repository dslab/s2e-chef
	.
	.
	.
	Status: Image is up to date for dslab/s2e-chef:v0.6

You can check whether the image has been fetched correctly:

	$ docker images | grep dslab
	dslab/s2e-chef      v0.6                476f51ae6396        4 months ago        4.629 GB
	dslab/s2e-base      v0.3                ef985235e1fa        4 months ago        4.055 GB

### Compilation

Compilation of one Chef version takes a little moment (3+ minutes):

	ccli build -d

The `-d` flag tells *ccli* to compile insided a docker container.

### Environment

Chef stores its persistent data (virtual machines, experiment data) in
`/var/local/chef`. If you wish to change the location, set the environment
variable `CHEF_DATAROOT` to the desired path (see also the output of `ccli env`
for other Chef-related environment variables).

The machines will be accessible for all users in the `kvm` group; you will need
to add yourself to it:

	sudo usermod -a -G kvm $(id -un)

This will also allow you to *run* the VMs without root permissions.

Again, this will require you to restart your current user session (or
temporarily `newgrp kvm`).

### Fetching a virtual machine

We provide prepared VMs that can be fetched from our servers. To get an
overview, run

	$ ccli vm list --remote
	Ubuntu
	  Ubuntu 14.04 (Trusty Tahr)
	  Based on: ubuntu-14.04-desktop-i386.iso
	Debian
	  Debian 7.8 (Wheezy) with a custom kernel, prepared for being used with Chef
	  Based on: debian-7.8.0-i386-netinst.iso

In order to fetch an image, run

	$ ccli vm fetch Debian MyChefVM
	[ OK ] initialise VM
	[INFO] URL: http://localhost/~ayekat/Debian.tar.gz
	[ OK ] fetch image bundle: 625 MiB / 625 MiB (100%)
	[ OK ] extract bundle: debian-7.8.0-i386-netinst.iso => /var/local/chef/vm/debian-7.8.0-i386-netinst.iso
	[ OK ] extract bundle: disk.qcow2 => /var/local/chef/vm/Debian/disk.qcow2
	[ OK ] expand raw image
	[ OK ] symlink S2E image
	[ OK ] store metadata
	[ OK ] set permissions: /var/local/chef/vm/Debian
	[ OK ] set permissions: /var/local/chef/vm/Debian/disk.raw

If everything works out, there is now a new VM named *MyChefVM*; you can list
existing VMs with

	$ ccli vm list
	MyChefVM
	  Operating System: Debian


Workflow
--------

### KVM mode

	ccli run -d MyChefVM kvm

In this mode, Chef works exactly like a raw qemu: it uses KVM and mounts images
writable. This mode is intended for installing software on the VM and otherwise
preparing the VM for symbolic execution.

Again, `-d` tells Chef to run inside the docker container.

Note that the VM is always started in headless mode. As indicated by the output,
you can connect to the VNC server with the VNC client of your choice.

You may also access the qemu console through a TCP client of your choice - for
instance, to stop the machine:

	$ nc localhost 12345
	QEMU 1.0.50 monitor - type 'help' for more information
	(qemu) system_powerdown
	system_powerdown

### Preparation mode

Now Chef could theoretically be started in symbolic mode. However, booting the
system that way takes more time than necessary; the preferred way is thus to run
Chef in non-symbolic mode until the point right before launching the target
process, then to take a snapshot of the VM:

	ccli run -d MyChefVM prep

Once it is booted and ready, you can take a snapshot of the machine via the qemu
monitor:

	$ nc localhost 12345
	QEMU 1.0.50 monitor - type 'help' for more information
	(qemu) savevm mysnapshot
	savevm mysnapshot
	(qemu) quit
	quit

Running

	$ ccli vm list
	MyChefVM
	  Operating System: Debian
	  Snapshots:
	    /var/local/chef/vm/MyChefVM/disk.s2e.mysnapshot

will show that the machine *MyChefVM* has a snapshot *mysnapshot*.

### Symbolic mode

The snapshot can now be resumed in symbolic mode:

	ccli run -d MyChefVM sym mysnapshot

Note that most configuration aspects (RAM size, CPU type, network type, ...) of
the VM cannot be specified when resuming the VM in symbolic mode, as it must run
in the same hardware configuration as the preparation mode.
