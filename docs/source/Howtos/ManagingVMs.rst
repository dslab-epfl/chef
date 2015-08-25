============
Managing VMs
============

.. contents::

**Note**: In this document, ``$S2EDIR`` denotes the location where all the S²E
related files lie in, as explained in :doc:`BuildingS2E`. Furthermore, this
document makes extensive use of the ``$S2EDIR/src/ctl`` script, and runs it
simply as the ``ctl`` command. You may achieve the same effect by ::

    $ alias ctl=$S2EDIR/src/ctl

or by adding a symbolic link from somewhere in your ``PATH`` environment.


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

.. _CloneVMs:

Clone
-----

Assume we've got the following machine::

    $ ctl vm list
    MyBox
      Size: 7680.0MiB
      Snapshots:
        ready
        base
        asdföklj

Now we may want to split the development on this machine, or just fiddle around
on it and have a "safe copy" in case something goes wrong, then we can *clone*
it::

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


The S²E VM Image Format
=======================

**Note**: This section is informational. In general, you should not face the
issues described here if you are using the ``ctl`` script to interact with S²E.
However, if you are interested, the output of ``ctl run`` always prints the full
QEMU command line to the terminal, e.g. ::

    $ ctl vm run --dry-run MyBox sym
    [DEBUG] Command line:
    $S2EDIR/build/i386-release-normal/qemu/i386-s2e-softmmu/qemu-system-i386 -drive file=$S2EDIR/vm/Debian/disk.s2e,cache=writeback,format=s2e -cpu pentium -net nic,model=pcnet -net user -s2e-config-file $S2EDIR/src/config/default-config.lua -s2e-verbose -s2e-output-dir $S2EDIR/expdata/auto_2015-08-19T15:06:25.383+0200

----

S²E operates on raw disk images, but it treats them as being in the *S²E disk
image format*. The image handler is basically a wrapper around QEMU's raw image
handler.

When in S²E mode, writes to the disk are local to each state and do not clobber
other states. Moreover, writes are NEVER propagated from the state to the image
(or the snapshot). This makes it possible to share one disk image and snapshots
among many instances of S²E.

S²E uses the raw format because existing disk image formats are not suitable for
multi-path execution; they usually mutate internal bookkeeping structures on
read operations, or worse, they may write these mutations back to the disk image
file, causing VM image corruptions. QCOW2 is one example of such formats. The
raw format does not suffer from any of these drawbacks, as there are no internal
disk handling mechanisms.

The ``.s2e`` file name suffix
-----------------------------

To make S²E handle a disk image with the S²E format, one can pass the
``format=s2e`` option when specifying the disk image with QEMU's ``-drive``
option. If this is missing, S²E will infer the format from the file name
extension; in particular, ``.s2e`` will denote the S²E image format.

In practice, however, if running in symbolic mode, S²E will refuse to operate on
disk image files that do not have the ``.s2e`` extension (even if the format is
explicitly specified through ``format=s2e``). This is to avoid data corruption
in case one forgets to specify the format.

Snapshots
---------

The S²E image format stores snapshots in a separate file, suffixed by the name
of the snapshot. For example, if the base image is called ``my_image.s2e``, the
snapshot ``ready`` (generated with ``savevm ready``) will be saved in the file
``my_image.s2e.ready`` in the same folder as ``my_image.s2e``.
