============
Managing VMs
============

.. contents::


Virtual machines are conveniently managed through the ``vm`` subcommand of the
``ctl`` script.

In this document, we cover most of the functionality of the ``vm`` command, but
may leave out some details. Passing the ``-h`` option will show and explain
all available options and arguments.


With ``ctl``
============

Create
------

A new, empty VM can be created with the ``ctl`` script::

    $ ctl vm create MyBox 7680M
    [ OK ] initialise VM
    [ OK ] create 7680MiB image

The resulting VM should then show up in the list of managed VMs::

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


Delete
------

In order to delete a snapshot, we can::

    $ ctl vm delete MyBox:asdfölkj
    [ OK ] delete snapshot MyBox:asdfölkj

By omitting the snapshot name, we can lay the entire VM into ashes::

    $ ctl vm delete MyBox
    Delete VM MyBox? [y/N] y
    [ OK ] delete MyBox


Manually
========

Each of the abovementioned actions can also be done without ``ctl``, and are
rather simple. In this section, we will simply list the "manual" equivalent to
each of the commands mentioned above.


Create
------

::

    $ ctl vm create MyBox 7680M
    ___

    $ mkdir /path/to/s2e/vm/MyBox
    $ /path/to/s2e/build/i386-release/normal/qemu/qemu-img create -f raw /path/to/s2e/vm/MyBox/disk.s2e 7680M

Clone
-----

::

    $ ctl vm clone MyBox MyClonedBox
    ___

    $ cp -r /path/to/s2e/vm/MyBox /path/to/s2e/vm/MyClonedBox

Export/Import
-------------

::

    $ ctl vm export MyBox MyBox.tar.gz
    ___

    $ cd /path/to/s2e/vm/MyBox/
    $ tar czf MyBox.tar.gz disk.s2e*
    $ cd -
    $ mv /path/to/s2e/vm/MyBox/MyBox.tar.gz .

and ::

    $ ctl vm import MyBox.tar.gz AnotherBox
    ___

    $ mkdir /path/to/s2e/vm/AnotherBox
    $ cd /path/to/s2e/vm/AnotherBox/
    $ tar xzf /path/to/MyBox.tar.gz
    $ cd -

This also reveals how VM packages are structured.


Delete
------

::

    $ ctl vm delete MyBox:asdfölkj
    ___

    $ rm /path/to/s2e/vm/MyBox/disk.s2e.asdfölkj

and ::

    $ ctl vm delete MyBox
    ___

    $ rm -r /path/to/s2e/vm/MyBox


About the ``.s2e`` suffix
=========================

S²E operates on raw qemu disk images as hard drive files. In some runtime
configurations however (preparation and symbolic mode), S²E should treat the
hard drives as having the "S²E format" so that it won't write changes back to it
and cause corruption.

In order to do so, the option ``-drive format=s2e`` can be passed to S²E/qemu.
Alternatively, the hard drive will automatically be treated as having the S²E
format if its filename has the ``.s2e`` suffix.

To prevent data corruption upon forgetting to declare the hard drive format as
``s2e``, in practice, S²E will refuse to operate on a drive file that does not
have the ``.s2e`` suffix (even if explicitely specified with ``-drive
format=s2e``).
