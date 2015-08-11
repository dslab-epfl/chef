===============
Getting Started
===============

This is a short guide on how to get, build, and use Chef. It assumes that you
are running Ubuntu 14.04, *Trusty Tahr* (for different distributions/versions,
you may be interested in using Linux containers, e.g. by using LXC directly or
through `docker.io`_).

.. _`docker.io`: https://docker.com/


Getting the Source
==================

::

    $ mkdir chef
    $ git clone https://github.com/dslab-epfl/chef chef/src

This will place the project's git repository in the ``chef/src`` subfolder.
Since Chef will put generated files directly in ``chef``, this allows us to keep
Chef-specific files in one place.


Preparing the Environment
=========================

Setting up the dependencies for Chef is a rather tedious task. Therefore, Chef
provides a ``setup.sh`` script that does all that setup work automatically. If
we take a look it, we can see that it will essentially

* update the package repository,
* install runtime dependencies for `S²E`_ and Chef,
* install buildtime dependencies for S²E, and
* add our user to the ``kvm`` group.

.. _`S²E`: http://dslab.epfl.ch/s2e/

Some of the steps will require us to be in the ``sudo`` group in order to be
able to install packages to your system. We can check that with ::

    $ id                                                  ↓↓↓↓↓↓↓↓
    uid=1000(mrbean) gid=1000(mrbean) groups=1000(mrbean),27(sudo),...

If we're in the ``sudo`` group, we can then launch ::

    $ chef/src/setup.sh --no-keep

The script will also compile LLVM, which takes a considerable time. So after
launching the script, we can go make ourselves a cup of tea and read the latest
*What If* (or get other important things done).

The ``--no-keep`` option tells the script to remove the intermediate files at
the end of the setup, which makes a difference of about 2.5 GiB.


Building Chef
=============

If the setup above works fine, we should now have an LLVM build in
``chef/build/deps/llvm``, and we can build Chef in its default configuration::

    $ chef/src/ctl build

Since that ``ctl`` script is going to be used a lot, we may want to ::

    $ alias ctl=/path/to/chef/src/ctl

or add a symlink somewhere to the ``PATH`` environment. This allows us to simply
run the ``ctl`` command instead of having to type ``/path/to/chef/src/ctl``
everytime. The build command above would thus become ::

    $ ctl build


Managing VMs
============

The virtual machines for Chef can be managed through the ``vm`` subcommand. They
are distributed and shared as gzipped tarballs, and can be imported to and
exported from Chef.

For now, let's fetch a prepared virtual machine, then import it to Chef::

    $ wget https://dslab.epfl.ch/chef/Debian.tar.gz
    $ ctl vm import Debian.tar.gz MyExampleBox

Once it's done, it should appear in the list of managed VMs::

    $ ctl vm list
    MyExampleBox
      Size: 10240MiB

There is also no magic behind this (try ``tar tf Debian.tar.gz`` and
``find /path/to/chef/vm`` and find out!)


Workflow
========

In S²E (and consequently in Chef, too) the workflow is as follows:

1. Boot the machine in KVM mode, install packages, set up things, then shut it
   down.
2. Boot the machine in *non-symbolic* dynamic-translation mode, launch the
   necessary services for running the experiment. Then, save a snapshot.
3. Resume from the snapshot in *symbolic* dynamic-translation mode, and run the
   experiment.

*(technically we could skip step 2, however booting up a machine in symbolic
mode is remarkably slow, so it is recommended to use non-symbolic mode for
preparing an experiment)*

KVM mode
--------

First, let's launch our machine in KVM mode::

    $ ctl run MyExampleBox kvm

In steps 2 and 3, there are no write-backs to the VM image, so if we wish to
make persistent changes that remain between different experiments, we must do
them here (mostly steps like installing packages). Once we're done, we shut down
the VM and proceed to the next step.

Preparation mode
----------------

::

    $ ctl run MyExampleBox prep

Once the machine has booted up in non-symbolic, dynamic-translation mode, we can
prepare our experiment, e.g. launch a server that will wait for instructions.
Then, we **don't shut down the machine**, but instead save a snapshot of it.
This snapshot can later be used to resume the machine without having to boot it.

But how do we do that?

You may have noticed the following output when you launched the machine::

    [INFO] Qemu monitor: port 12345 (connect with `{nc,telnet} XXX.XX.XX.XXX 12345)

This is the port on which the `qemu monitor`_ runs, and it can be used to save
snapshots of the VM.

.. _`qemu monitor`: https://en.wikibooks.org/wiki/QEMU/Monitor

So let's launch a second shell, and connect to the monitor using some TCP
connection tool (like netcat or telnet), then save a snapshot using the
``savevm`` command::

    $ nc localhost 12345
    QEMU 1.0.50 monitor - type 'help' for more information
    (qemu) savevm blabla
    savevm blabla

This should create a snapshot of the machine in the current state. If we were to
run ``ctl vm list``, we would see this::

    MyExampleBox
      Size: 10240MiB
      Snapshots:
        blabla

Since we don't need to gracefully shut down the machine, we can just stop it
from the qemu monitor::

    (qemu) quit

*(of course we could also just ^C in the main process terminal)*

Symbolic mode
-------------

We can now resume from the snapshot in symbolic mode, and we may launch any
experiments we like::

    $ ctl run MyExampleBox:blabla sym

This will also create an experiment output directory in ``chef/expdata``, which
can be used to further evaluate experiment results. See `an inexisting guide`_
for more information on running experiments.

.. _`an inexisting guide`: http://example.com/
