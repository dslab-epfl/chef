============
S²E Workflow
============

This document describes a recommended workflow when working with S²E. A more
detailed example of how to use S²E to test a program is given at
:doc:`TestingMinimalProgram`.

.. contents::

**Note**: In this document, ``$S2EDIR`` denotes the location where all the S²E
related files lie in, as explained in :doc:`BuildingS2E`. Furthermore, this
document makes extensive use of the :file:`$S2EDIR/src/ctl` script, and runs it
simply as the :kbd:`ctl` command. You may achieve the same effect by ::

    $ alias ctl=$S2EDIR/src/ctl

or by adding a symbolic link from somewhere in your ``PATH`` environment.


KVM mode: Preparing the VM
==========================

Once you have set up a VM as described in :doc:`ImageInstallation`, you will
likely need to install additional programs and apply changes that are specific
to the experiment you would like to run. It is recommended to do as much
preparation work as possible in vanilla QEMU with KVM, as the dynamic binary
translation mode (described below) tends to be rather slow.

Let's assume you have got a freshly installed Debian system that you would like
to use for the experiment::

    $ ctl vm list
    Debian
      Size: 10240.0MiB

If you wish to reuse this VM for other experiments later, it is a good idea to
first create a "backup" copy of your machine (otherwise you can just skip
this)::

    $ ctl vm clone Debian Debian_base
    [ OK ] initialise VM
    [ OK ] copy disk image

The image can now be run in KVM mode as follows::

    $ ctl run Debian kvm
    [DEBUG] Command line:
    $S2EDIR/build/i386-release-normal/qemu/i386-softmmu/qemu-system-i386 -drive file=$S2EDIR/vm/Debian/disk.s2e,if=virtio,format=raw -cpu pentium -m 2048M -net nic,model=pcnet -net user -enable-kvm -smp 4
    ...

Inside the running machine, install packages, adjust configurations, etc. so
that you can run the experiment afterwards. Finally, shut down the machine
normally, e.g. through the :kbd:`poweroff` command.


Preparation mode: Preparing the Experiment
==========================================

In theory we could now launch the VM in symbolic mode and start the experiment.
However, binary translation in S²E mode (also referred to as *symbolic mode*) is
extremely slow. Therefore, in this intermediary step, we use the ordinary binary
translation (non-S²E mode) to boot the system up::

    $ ctl run Debian prep
    [DEBUG] Command line:
    $S2EDIR/build/i386-release-normal/qemu/i386-softmmu/qemu-system-i386 -drive file=$S2EDIR/vm/Debian/disk.s2e,cache=writeback,format=s2e -cpu pentium -m 128M -net nic,model=pcnet -net user
    ...

Once the system has booted up, we put the machine in a state where we can launch
the experiment, e.g. launch services, :kbd:`cd` in some directory, etc. Then, we
save a snapshot by switching to the QEMU monitor (pressing :kbd:`Control+Alt+2`)
and running :kbd:`savevm`::

    QEMU 1.0.50 monitor - type 'help' for more information
    (qemu) savevm prepared
    savevm prepared

If everything is fine, this has now created a snapshot ``prepared`` for our
machine::

    $ ctl vm list
    Debian
      Size: 10240.0MiB
      Snapshots:
        prepared

We may now stop the running VM through the QEMU monitor::

    (qemu) quit
    quit

*(or just ^C the QEMU process)*


Symbolic mode: Running the Experiment
=====================================

Finally, we can run S²E in symbolic mode by resuming from the snapshot we
created above::

    $ ctl run Debian:prepared sym
    [INFO] Experiment name: auto_2015-08-19T17:04:45.819+0200
    [DEBUG] Command line:
    $S2EDIR/build/i386-release-normal/qemu/i386-s2e-softmmu/qemu-system-i386 -drive file=$S2EDIR/vm/Debian/disk.s2e,cache=writeback,format=s2e -cpu pentium -m 128M -net nic,model=pcnet -net user -s2e-config-file $S2EDIR/src/config/default-config.lua -s2e-verbose -s2e-output-dir $S2EDIR/expdata/auto_2015-08-19T17:04:45.819+0200
    ...

Each time you run a VM in symbolic mode, S²E assumes that you will symbolically
execute some process. It therefore creates logs and experiment data and stores
them in a folder in :file:`$S2EDIR/expdata`. You can specify the name of the
experiment as follows::

    $ ctl run Debian:prepared sym --expname MyExperiment

which will result in an experiment data folder
:file:`$S2EDIR/expdata/MyExperiment`. If you omit the experiment name, ``ctl
run`` will auto-generate a name containing a human-readable timestamp (so you
can still find it later). In the example above, it was
``auto_2015-08-19T17:04:45.819+0200``.


Experimental KVM Snapshot Support
=================================

It is possible to boot an image in KVM mode, take a snapshot, and resume it in
the dynamic binary translation (DBT) mode that QEMU normally uses.  This is
useful if your guest system is large and avoids cumbersome manipulations to
workaround the relative slowness of the DBT (e.g., starting in QEMU, setting up,
rebooting again in DBT mode, etc.).

::

    $ $S2EDIR/build/x86_64-release-normal/qemu/x86_64-softmmu/qemu-system-x86_64 -enable-kvm -cpu core2duo -net none
    ... (set up the guest, save a snapshot, quit the machine)

    $ $S2EDIR/build/x86_64-release-normal/qemu/x86_64-softmmu/qemu-system-x86_64 -cpu core2duo -net none -loadvm mysnapshot
    ... (finish setup, save another snapshot, quit the machine)

    $ $S2EDIR/build/x86_64-release-normal/qemu/x86_64-s2e-softmmu/qemu-system-x86_64 -cpu core2duo -net none -loadvm mysnapshot
    ... (run experiment)

Limitations:

* The host CPU in KVM mode must match the virtual CPU in DBT mode. For example,
  you cannot save a KVM snapshot on an Intel CPU and resume it with default
  settings in DBT mode (i.e., -cpu qemu64, which uses the AMD variations of some
  instructions).

* The CPUID flags should be matched between KVM and DBT mode. Mismatches do not
  seem to matter for simple experiments, but may lead to guest kernel crashes.
  You can dump :file:`/proc/cpuinfo` in KVM and DBT mode, compare both and add
  the corresponding tweaks to the ``-cpu`` parameter.

* KVM mode does not support S²E custom instructions. They cause an invalid
  opcode exception in the guest. Therefore, you might need to save a second
  snapshot in DBT mode when using tools such as ``s2eget``.

* It is possible that the guest hangs when resumed in DBT mode from a KVM
  snapshot. Try to save and resume again.

* Resuming DBT snapshots in KVM mode does not seem to work.
