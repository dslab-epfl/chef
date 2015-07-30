# Chef Command Line Iterface


## Name

ccli - Chef Command Line Interface


## Synopsis

	ccli COMMAND [ARGS ...]


## Description

The Chef Command Line Interface is a common interface to a collection of scripts
that offer a simplified way for using Chef, including building Chef, creating
and managing disk images, or interacting with running Chef instances.


## Dependencies

### Ubuntu

* acl
* coreutils
* lxc-docker
* python3
* python3-netifaces
* python3-psutil
* python3-requests
* python3-yaml
* qemu-kvm

### Debian GNU/Linux

* acl
* coreutils
* docker.io
* python3-netifaces
* python3-psutil
* python3-requests
* python3-yaml
* qemu-kvm

### Arch Linux

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


## Environment Variables

The currently supported environment variables are:

	CHEF_ARCH             # [default: i386]     Default architecture
	CHEF_TARGET           # [default: release]  Default target
	CHEF_MODE             # [default: normal]   Default mode
	CHEF_RELEASE          # [default: $CHEF_ARCH:$CHEF_TARGET:$CHEF_MODE]  Default release tuple
	CHEF_VERBOSE          # [default: 0]        Whether to be verbose by default
	CHEF_DOCKERIZED       # [default: 0]        Whether to wrap execution in docker by default
	CHEF_DATAROOT         # [default:/var/local/chef]        Where Chef stores its data on the system
	CHEF_DATAROOT_VMROOT  # [default:$CHEF_DATAROOT/vm]      Where Chef manages VM images
	CHEF_DATAROOT_EXPDATA # [default:$CHEF_DATAROOT/expdata] Where Chef stores experiment output data
