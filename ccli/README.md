# Chef Command Line Interface

Roadmap


## SYNOPSIS

	ccli [OPTIONS ...] COMMAND [ARGS ...]


## DESCRIPTION

The chef command line interface is a common interface to a collection of scripts
that offer a simplified way for using chef, including

* Building chef
* Creating and managing disk images
* Interacting with running chef instances, running in multiple configurations

The following commands are available:

* `build`: Build chef inside a docker container
* `vm`: Manage VMs
* `run`: Run chef inside a docker container

Run `ccli COMMAND -h` for more information about a specific command.


## DEPENDENCIES

* acl
* docker
* docker-py
* python3
* coreutils
* qemu
