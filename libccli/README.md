# NAME

ccli - Chef Command Line Interface


# SYNOPSIS

	ccli COMMAND [ARGS ...]


# DESCRIPTION

The Chef Command Line Interface is a common interface to a collection of scripts
that offer a simplified way for using Chef, including building Chef, creating
and managing disk images, or interacting with running Chef instances.


# DEPENDENCIES

## Ubuntu

* acl
* coreutils
* lxc-docker
* python3
* python3-netifaces
* python3-psutil
* python3-requests
* python3-yaml
* qemu-kvm

## Debian GNU/Linux

* acl
* coreutils
* docker.io
* python3-netifaces
* python3-psutil
* python3-requests
* python3-yaml
* qemu-kvm

## Arch Linux

* acl
* coreutils
* docker
* python
* python-netifaces
* python-psutil
* python-pyyaml
* python-requests
* qemu

Note that acl is only required if you wish to use docker, as it is there for
fixing some permission-related issues when interacting across
"container-boundaries".


# ENVIRONMENT VARIABLES

The currently supported environment variables are:

	CCLI_ARCH       # Default architecture - default: i386
	CCLI_TARGET     # Default target - default: release
	CCLI_MODE       # Default mode - default: normal
	CCLI_VERBOSE    # Whether to be verbose by default - default: false (except for `build`)
	CCLI_DIRECT     # Whether to run in direct mode (without docker) by default - default: true
	CHEF_DATAROOT   # Where Chef stores its data on the system - default: /var/local/chef

Note that values for booleans are **0 for true, and non-0 for false**.

Because.
