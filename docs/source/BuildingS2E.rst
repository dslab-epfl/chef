=========================
Building the S2E Platform
=========================

The following steps describe the installation process in detail. We assume the
installation is performed on an Ubuntu 14.04 host system.

.. contents::


Getting the Source
==================

The source is distributed as a git repository, so you will require the ``git``
package to be installed on your system::

    $ sudo apt-get install git

While running, S²E will create additional files on the same directory level as
its source directory, so the following procedure is recommended for fetching the
source::

    $ mkdir s2e
    $ git clone --recursive https://github.com/dslab-epfl/chef s2e/src

This will place the S²E source in the ``s2e/src`` subdirectory, together with
its sub-components.


Preparing the Environment
=========================

The repository should contain a script ``setup.sh``, which will install its
dependencies and run some additional preparation work. Since it will assume that
you are part of the ``sudo`` group, you may want to check that first::

    $ id                                                     ↓↓↓↓↓↓↓↓
    uid=1000(s2euser) gid=1000(s2euser) groups=1000(s2euser),27(sudo),...

Then, launch the script::

    $ s2e/src/setup.sh --no-keep

It will also compile LLVM, which takes a considerable time. So after launching
the script, you can go make yourself a cup of tea and read the latest *What If*
(or get other important things done).

The ``--no-keep`` option tells the script to remove the intermediate files at
the end of the setup, which makes a difference of about 1 GiB.


Building S²E
============

Once the setup script above finishes, it should have generated an LLVM build in
``s2e/build/deps/llvm``, which is required to build S²E::

    $ s2e/src/ctl build

This will build S²E using all available cores on the system, and in the default
configuration (i386, release mode, without address sanitizer). Pass the ``-h``
option to the build command to get an overview of possible build configurations.

Since that ``ctl`` script is going to be used a lot, it is recommended to either

::

    $ alias ctl=/path/to/s2e/src/ctl

or to add a symlink from somewhere in the ``PATH`` environment. This allows you
to run ``ctl`` as a command from anywhere without having to type
``/path/to/s2e/src/ctl`` each time, e.g. ::

    $ ctl build

The resulting build files are placed in ``s2e/build/i386-release-normal``.


Setting up a VM
===============

The ``ctl`` script provides a ``vm`` command for managing virtual machines for
S²E. A prepared VM can be fetched and imported to S²E::

    $ wget https://s3.amazonaws.com/chef.dslab.epfl.ch/vm/s2e-base.tar.gz
    $ ctl vm import s2e-base.tar.gz MyExampleBox

Once it's done, the newly imported VM should appear in the list of managed VMs::

    $ ctl vm list
    MyExampleBox
      Size: 10240.0MiB

See :doc:`ManagingVMs` for more details.


Reporting Bugs
==============

In order to report bugs, please use GitHub's
`issue tracker <https://github.com/dslab-epfl/s2e/issues>`_.

If you would like
to contribute to S2E, please create your own personal clone of S2E on GitHub, push your changes to it and then send us a
pull request.

You can find more information about using git on `http://gitref.org/ <http://gitref.org/>`_ or on
`http://progit.org/ <http://progit.org/>`_.


Updating S2E
============

You can rerun ``ctl build`` to recompile S²E either when changing it or when
pulling new versions through ``git``. Note that by default, the packages are not
automatically reconfigured; for deep changes you will need to pass the packages
to the ``-f`` option.


Rebuilding S2E Documentation
=============================

The S2E documentation is written in the `reStructuredText
<http://docutils.sourceforge.net/rst.html>`_ format and works with the `Sphinx
<http://sphinx-doc.org>`_ documentation tool. After changing the documentation
source, regenerate it by running ::

    $ make html

in the ``s2e/src/docs`` folder.
