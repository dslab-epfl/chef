doctor
======

A simple script for easily building S²E-chef inside docker.


usage
-----

### prepare

First, you will need to create a base image that will be used for all
configurations of chef:

	./doctor prepare

This will create two docker images: one named `$username/s2e-base` that can be
used to build S²E inside, and one named `$username/s2e-chef` that is configured
further to build chef; you can check their existence with

	docker images

### build

Next, you need to build chef in a given configuration. Currently, `doctor`
distinguishes between Release and Debug mode, and creates appropriate containers
for each of them:

	./doctor build release
	./doctor build debug

They both take the abovementioned `$username/s2e-chef` image to build the
containers `zopf_release` and `zopf_debug`; you can check their existence with

	docker ps -a

### run

*(not implemented yet)*
