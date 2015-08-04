Getting Started
===============

This is a brief introduction for how to get, build and run S²E-Chef.


Scratchpad
----------

* `./prepare.sh`
* `./ctl build`
* `./ctl vm fetch Debian Debian`
* Regular S²E/Chef Workflow:
  * `./ctl run Debian kvm`
  * `./ctl run Debian prep`
  * `./ctl run Debian sym SNAPSHOT`


Chef Command Line Tools
-----------------------

Chef is mostly controlled through the set of scripts that lay in /ctltools in
the project's root. They can either be executed directly, or more elegantly as
a subcommand to the main control script (`ctl`), which offers a convenient list
of available commands and what they achieve.

The various scripts automate many Chef-related processes, including installing
dependencies and compiling and running Chef.

In this document, we invoke the main script as `ctl`, which is achieved by
adding a symbolic link to the `ctl` script from one of the locations in `PATH`.
It is not mandatory, but it allows you to type

	ctl ...

instead of

	path/to/s2e-chef/ctl ...

Run `ctl help` to get a list of all *ctl*-subcommands.


Preparing the Environment
-------------------------

### Workspace structure

**TODO**

### Hardware virtualisation

In order to profit from better performance in qemu, it is recommended to enable
hardware virtualisation (Intel VT-X or AMD-V) on your machine, if available.

### prepare.sh

**TODO**

### Source

	git clone https://github.com/stefanbucur/chef src


Compiling Chef
--------------

Compilation of one Chef version takes a little moment (3+ minutes):

	$ ctl build
	.
	.   (lots of output)
	.
	>>> Build complete for in /path/to/chef/workspace/build/i386-release-normal.


Fetching a VM
-------------

We provide prepared VMs that can be fetched from our servers. To get an
overview, run

	$ ctl vm list --remote
	Ubuntu
	  Ubuntu 14.04 (Trusty Tahr)
	  Based on: ubuntu-14.04-desktop-i386.iso
	Debian
	  Debian 7.8 (Wheezy) with a custom kernel, prepared for being used with Chef
	  Based on: debian-7.8.0-i386-netinst.iso

In order to fetch an image, run

	$ ctl vm fetch Debian MyChefVM
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

	$ ctl vm list
	MyChefVM
	  Operating System: Debian
	  Based on: /var/local/chef/vm/debian-7.8.0-i386-netinst.iso
	$


Working with Chef
-----------------

### KVM mode

	ctl run MyChefVM kvm

In this mode, Chef works exactly like a raw qemu: it uses KVM and mounts images
writable. This mode is intended for installing software on the VM and otherwise
preparing the VM for symbolic execution.

Note that the VM is always started in headless mode. As indicated by the output,
you can connect to the VNC server with the VNC client of your choice.

You may also access the qemu console through a TCP client of your choice - for
instance, to stop the machine:

	$ nc localhost 12345
	QEMU 1.0.50 monitor - type 'help' for more information
	(qemu) system_powerdown
	system_powerdown
	$

### Preparation mode

Now Chef could theoretically be started in symbolic mode. However, booting the
system that way takes more time than necessary; the preferred way is thus to run
Chef in non-symbolic mode until the point right before launching the target
process, then to take a snapshot of the VM:

	ctl run MyChefVM prep

Once it is booted and ready, you can take a snapshot of the machine via the qemu
monitor:

	$ nc localhost 12345
	QEMU 1.0.50 monitor - type 'help' for more information
	(qemu) savevm mysnapshot
	savevm mysnapshot
	(qemu) quit
	quit
	$

Then, running

	$ ctl vm list
	MyChefVM
	  Operating System: Debian
	  Based on: /var/local/chef/vm/debian-7.8.0-i386-netinst.iso
	  Snapshots:
	    mysnapshot
	$

will show that the machine *MyChefVM* has a snapshot *mysnapshot*.

### Symbolic mode

The snapshot can now be resumed in symbolic mode:

	ctl run MyChefVM sym mysnapshot

Note that most configuration aspects (RAM size, CPU type, network type, ...) of
the VM cannot be specified when resuming the VM in symbolic mode, as it must run
in the same hardware configuration as the preparation mode.
