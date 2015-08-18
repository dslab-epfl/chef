============
Managing VMs
============

.. contents::

In this document, we cover most of the functionality of the ``vm`` command,
which is used to manage virtual machines in a convenient way. However, we may
leave out some details. Passing the ``-h`` option will show and explain all
available options and arguments.

**Note**: ``$S2EDIR`` denotes the location into which the project's source code
and other related files have been placed into. Furthermore, this guide will make
extensive use of the ``$S2EDIR/src/ctl`` script, and runs it simply as the
``ctl`` command. You may achieve the same effect by ::

    $ alias ctl=$S2EDIR/src/ctl


About the VM Images
===================

S²E operates on raw qemu disk images as hard drive files. In order to reduce the
used space, they are stored as `sparse files
<https://en.wikipedia.org/wiki/Sparse_file>`_, so even a 10GiB image should only
take a few gigabytes (depending on what's installed on it, of course).


About the ``.s2e`` suffix
=========================

In some runtime configurations (preparation and symbolic mode), S²E should treat
the hard drives as having the "S²E format" so that it won't write changes back
to it and cause data corruption.

In order to do so, the option ``-drive format=s2e`` can be passed to S²E/qemu.
Alternatively, the hard drive will automatically be treated as having the S²E
format if its filename has the ``.s2e`` suffix.

To prevent data corruption upon forgetting to declare the hard drive format as
``s2e``, in practice, S²E will refuse to operate on a drive file that does not
have the ``.s2e`` suffix (even if explicitely specified with ``-drive
format=s2e``).

In general, you should not face these issues if you are using the ``ctl`` script
to interact with S²E. Furthermore, if you are intersted, running ``ctl run``
will print the full qemu command line to the terminal.


Reusing an existing VM
======================

See how to :ref:`import VMs <ExportImportVMs>`.


Managing VMs With ``ctl``
=========================

Create
------

::

    $ ctl vm create MyBox 7680M
    [ OK ] initialise VM
    [ OK ] create 7680MiB image

The resulting VM should show up in the list of managed VMs::

    $ ctl vm list
    MyBox
      Size: 7680.0MiB

Clone
-----

Assuming we want to split the development on a machine ::

    $ ctl vm list
    MyBox
      Size: 7680.0MiB
      Snapshots:
        ready
        base
        asdföklj

or just fiddle around on it and have a "safe copy" in case something goes
wrong::

    $ ctl vm clone MyBox MyClonedBox
    [ OK ] initialise VM
    [ OK ] copy disk image
    [ OK ] copy snapshot: ready
    [ OK ] copy snapshot: base
    [ OK ] copy snapshot: asdfölkj

After this, the result is::

    $ ctl vm list
    MyBox
      Size: 7680.0MiB
      Snapshots:
        ready
        base
        asdföklj
    MyClonedBox
      Size: 7680.0MiB
      Snapshots:
        ready
        base
        asdföklj

Note that cloning takes both time and disk space. Depending on your tasks,
creating snapshots may be a better solution.

.. _ExportImportVMs:

Export/Import
-------------

To facilitate collaboration, VMs can be packaged::

    $ ctl vm export MyBox
    [INFO] exporting to /path/to/working/directory/MyBox.tar.gz
    [ OK ] package disk image
    [ OK ] package snapshot: ready
    [ OK ] package snapshot: base
    [ OK ] package snapshot: asdfölkj
    [ OK ] compress package
    [ OK ] clean up

The resulting package can then of course be imported again::

    $ ctl vm import MyBox.tar.gz AnImportedBox
    [ OK ] initialise VM
    [ OK ] decompress package
    [ OK ] scan package
    [ OK ] extract disk image
    [ OK ] extract snapshot: ready
    [ OK ] extract snapshot: base
    [ OK ] extract snapshot: asdfölkj
    [ OK ] clean up

and should appear in the list of managed VMs::

    $ ctl vm list
    MyBox
      Size: 7680.0MiB
      Snapshots:
        ready
        base
        asdfölkj
    AnImportedBox
      Size: 7680.0MiB
      Snapshots:
        ready
        base
        asdfölkj

If you wish to import just a raw image, you can pass the ``--raw`` option::

    $ ctl vm import --raw /path/to/disk.raw AnImportedBox
    [ OK ] initialise VM
    [ OK ] copy disk image

Delete
------

In order to delete a snapshot, we can::

    $ ctl vm delete MyBox:asdfölkj
    [ OK ] delete snapshot MyBox:asdfölkj

By omitting the snapshot name, we can lay the entire VM into ashes::

    $ ctl vm delete MyBox
    Delete VM MyBox? [y/N] y
    [ OK ] delete MyBox


Managing VMs Manually
=====================

Each of the abovementioned actions can also be done without ``ctl``, and are
rather simple. In this section, we will simply list the "manual" equivalent to
each of the commands mentioned above.


Create
------

::

    $ ctl vm create MyBox 7680M
    ___

    $ mkdir $S2EDIR/vm/MyBox
    $ $S2EDIR/build/i386-release/normal/qemu/qemu-img create -f raw $S2EDIR/vm/MyBox/disk.s2e 7680M

Clone
-----

::

    $ ctl vm clone MyBox MyClonedBox
    ___

    $ cp -r $S2EDIR/vm/MyBox $S2EDIR/vm/MyClonedBox

Export/Import
-------------

::

    $ ctl vm export MyBox MyBox.tar.gz
    ___

    $ cd $S2EDIR/vm/MyBox/
    $ tar czf MyBox.tar.gz disk.s2e*
    $ cd -
    $ mv $S2EDIR/vm/MyBox/MyBox.tar.gz .

and ::

    $ ctl vm import MyBox.tar.gz AnotherBox
    ___

    $ mkdir $S2EDIR/vm/AnotherBox
    $ cd $S2EDIR/vm/AnotherBox/
    $ tar xzf /path/to/MyBox.tar.gz
    $ cd -

This also reveals how VM packages are structured.

Delete
------

::

    $ ctl vm delete MyBox:asdfölkj
    ___

    $ rm $S2EDIR/vm/MyBox/disk.s2e.asdfölkj

and ::

    $ ctl vm delete MyBox
    ___

    $ rm -r $S2EDIR/vm/MyBox
