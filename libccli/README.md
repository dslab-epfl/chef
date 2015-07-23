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

* coreutils
* acl
* docker
* python3
* python3-psutil
* python3-yaml
* python3-requests
* qemu

## Debian GNU/Linux

*coming soon*

## Arch Linux

* coreutils
* acl
* docker
* python
* python-psutil
* python-pyyaml
* python-requests
* qemu


# ENVIRONMENT VARIABLES

The currently supported environment variables are:

	CCLI_ARCH         # Default architecture (default: i386)
	CCLI_TARGET       # Default target (default: release)
	CCLI_MODE         # Default mode (default: normal)
	CCLI_SILENT_BUILD # Whether to run builds silently by default (default: false)
	CCLI_DIRECT       # Whether to run in direct mode (without docker) by default (default: false)
	CHEF_DATAROOT     # Where Chef stores its virtual machines on the system (default: /var/local/chef)

Note that values for booleans are **0 for true, and non-0 for false**.

Because.
