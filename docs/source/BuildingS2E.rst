=========================
Building the S²E Platform
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

    $ id
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
``s2e/build/deps/llvm``, which is required to build S²E. We do so by::

    $ s2e/src/ctl build

This will build S²E using all available cores on the system, and in the default
configuration (i386, release mode, without address sanitizer). Pass the ``-h``
option to the build command to get an overview of possible build configurations.

The resulting build files are placed in ``s2e/build/i386-release-normal``.


Reporting Bugs
==============

In order to report bugs, please use GitHub's
`issue tracker <https://github.com/dslab-epfl/s2e/issues>`_.

If you would like to contribute to S²E, please create your own personal clone of
S²E on GitHub, push your changes to it and then send us a pull request.

You can find more information about using git on http://gitref.org/ or on
http://progit.org/.


Updating S²E
============

You can rerun ``ctl build`` to recompile S²E either when changing it or when
pulling new versions through ``git``. Note that by default, the components are
not automatically reconfigured; for deep changes you will need to pass the
``-f`` option together with the components you wish to reconfigure, e.g. for
force-rebuilding STP and KLEE::

    $ s2e/src/ctl build -f stp,klee


Rebuilding S²E Documentation
=============================

The S²E documentation is written in the `reStructuredText
<http://docutils.sourceforge.net/rst.html>`_ format and works with the `Sphinx
<http://sphinx-doc.org>`_ documentation tool. After changing the documentation
source, regenerate it by running ::

    $ make html

in the ``s2e/src/docs`` folder.
