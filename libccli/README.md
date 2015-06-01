# Chef Command Line Interface

Roadmap


## SYNOPSIS

	ccli [OPTIONS ...] COMMAND [ARGS ...]


## DESCRIPTION

The chef command line interface is a common interface to a collection of scripts
that offer a simplified way for using chef, including building chef, creating
and managing disk images, or interacting with running chef instances.

The following commands are available:

* `init`: Initialise chef environment in /var/lib/chef
* `build`: Build chef inside a docker container
* `vm`: Manage VMs
* `run`: Run chef inside a docker container
* `help`: Display a help message and exit

Each command has its own command line interface, whose help can usually be
displayed by passing the `-h` flag.


## DEPENDENCIES

### Ubuntu

*coming soon*

### Debian GNU/Linux

*coming soon*

### Arch Linux

* `coreutils`
* `acl`
* `docker`
* `python`
* `python-psutil`
* `python-pyyaml`
* `qemu`
