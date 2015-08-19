=============================
Preparing VM Images for S²E
=============================

To run S²E, you need a QEMU-compatible virtual machine disk image. S²E can run
any x86 operating system inside the VM.

.. contents::

There are several ways of getting a VM that can be used with S²E. In this
document, we will quickly cover three of them. For more information about
managing S²E VMs and how things work underneath, see :doc:`ManagingVMs`.

**Note**: In this document, ``$S2EDIR`` denotes the location where all the S²E
related files lie in, as explained in :doc:`BuildingS2E`. Furthermore, this
document makes extensive use of the :file:`$S2EDIR/src/ctl` script, and runs it
simply as the :kbd:`ctl` command. You may achieve the same effect by ::

    $ alias ctl=$S2EDIR/src/ctl

or by adding a symbolic link from somewhere in your ``PATH`` environment.


Fetching a Prepared VM
======================

A prepared VM is available on Amazon S3, and can be fetched and imported as
follows::

    $ wget https://s3.amazonaws.com/chef.dslab.epfl.ch/vm/s2e-base.tar.gz
    $ ctl vm import s2e-base.tar.gz MyBox

Once it's done, the newly imported VM should appear in the list of managed VMs::

    $ ctl vm list
    MyBox
      Size: 10240.0MiB

Using an existing VM
====================

If you have already got an existing QEMU-compatible VM, it can be used by S²E if
if it has the ``raw`` format. For other formats, you will first need to run
:kbd:`qemu-img convert` on the disk image file, e.g. for files in the ``qcow2``
format::

    $ qemu-img convert -f qcow2 -O raw disk.qcow disk.raw

The raw disk image can then be imported with ::

    $ ctl vm import --raw disk.raw MyBox

Installing a new VM
===================

**Note**: Any QEMU-compatible VM can be made ready-for-use with S²E, so
installing it the classic way with a vanilla QEMU and importing it as seen above
is perfectly reasonable. This subsection shows an alternative way to perform the
installation with the ``ctl`` script.

First, a new virtual machine must be created. This is conveniently done by ::

    $ ctl vm create MyBox 5120M
    [ OK ] initialise VM
    [ OK ] create 5120MiB image

Next, get an installation CD with the distribution of your choice, e.g. ::

    $ wget http://cdimage.debian.org/debian-cd/7.7.0/i386/iso-cd/debian-7.7.0-i386-CD-1.iso

Now, the previously created VM is started using ``ctl``'s ``run`` command::

    $ ctl run -q=-drive -q file=debian-7.7.0-i386-CD-1.iso MyBox kvm
                 ↑         ↑
                 QEMU options

Once the installation is finished, it is recommended to get some development
tools (C/C++ compiler) in the guest machine; in the example of Debian, you'd
run ::

   guest$ su -c "apt-get install build-essential"


Recommendations
===============

* Disable fancy desktop themes. Most OSes have a GUI, which consumes resources.
  Disabling all visual effects will make program analysis faster.

* Disable the screen saver.

* Disable unnecessary services to save memory and speed up the guest. Services
  like file sharing, printing, wireless network configuration, or firewall are
  useless unless you want to test them in S²E.

* Avoid the QEMU ``virtio`` network interface for now. In the version of QEMU
  that is bundled into S²E, there can be random crashes.
