The Selective Symbolic Execution (S²E) Platform
===============================================

.. contents::

Do not forget the `FAQ <FAQ.rst>`_ if you have questions.

S²E Documentation
=================

Getting Started
---------------

.. toctree::
   :maxdepth: 1
   :numbered:

   GettingStarted
   BuildingS2E
   ImageInstallation
   Workflow
   UsingS2EGet
   TestingMinimalProgram
   Howtos/init_env
   Howtos/Concolic
   EquivalenceTesting

Analyzing Windows Device Drivers
--------------------------------

.. toctree::
   :maxdepth: 1
   :numbered:

   Windows/DriverTutorial
   Windows/CheckedBuild

Analyzing the Linux Kernel
--------------------------

.. toctree::
   :maxdepth: 1
   :numbered:

   BuildingLinux
   SystemTap

Howtos
------

.. toctree::
   :maxdepth: 1
   :numbered:

   Howtos/ExecutionTracers
   Howtos/WritingPlugins
   Howtos/Parallel
   Howtos/Debugging

S2E Tools
---------

1. Available Tools

   .. toctree::
     :maxdepth: 1
     :numbered:

     Tools/ForkProfiler
     Tools/TbPrinter
     Tools/ExecutionProfiler
     Tools/CoverageGenerator

2. :doc:`Tools/DebugInfo`

FAQ
---

.. toctree::
   :numbered:

   FAQ

S²E Plugin Reference
====================


OS Event Monitors
-----------------

To implement selectivity, S2E relies on several OS-specific plugins to detect
module loads/unloads and execution of modules of interest.

* :doc:`Plugins/WindowsInterceptor/WindowsMonitor`
* :doc:`Plugins/RawMonitor`
* :doc:`Plugins/ModuleExecutionDetector`

Execution Tracers
-----------------

These plugins record various types of multi-path information during execution.
This information can be processed by offline analysis tools. Refer to
the `How to use execution tracers? <Howtos/ExecutionTracers.rst>`_ tutorial to understand
how to combine these tracers.

* :doc:`Plugins/Tracers/ExecutionTracer`
* :doc:`Plugins/Tracers/ModuleTracer`
* :doc:`Plugins/Tracers/TestCaseGenerator`
* :doc:`Plugins/Tracers/TranslationBlockTracer`
* :doc:`Plugins/Tracers/InstructionCounter`

Selection Plugins
-----------------

These plugins allow you to specify which paths to execute and where to inject symbolic values

* :doc:`Plugins/StateManager` helps exploring library entry points more efficiently.
* :doc:`Plugins/EdgeKiller` kills execution paths that execute some sequence of instructions (e.g., polling loops).
* :doc:`Plugins/BaseInstructions` implements various custom instructions to control symbolic execution from the guest.
* *SymbolicHardware* implements symbolic PCI and ISA devices as well as symbolic interrupts and DMA. Refer to the :doc:`Windows driver testing <Windows/DriverTutorial>` tutorial for usage instructions.
* *CodeSelector* disables forking outside of the modules of interest
* :doc:`Plugins/Annotation` plugin lets you intercept arbitrary instructions and function calls/returns and write Lua scripts to manipulate the execution state, kill paths, etc.

Analysis Plugins
----------------

* *CacheSim* implements a multi-path cache profiler.


Miscellaneous Plugins
---------------------

* :doc:`FunctionMonitor <Plugins/FunctionMonitor>` provides client plugins with events triggered when the guest code invokes specified functions.
* :doc:`HostFiles <UsingS2EGet>` allows to quickly upload files to the guest.

S²E Development
===============

* :doc:`Contributing to S2E <Contribute>`
* :doc:`Profiling S2E <ProfilingS2E>`


S²E Publications
================

* `S2E: A Platform for In Vivo Multi-Path Analysis of Software Systems
  <http://dslab.epfl.ch/proj/s2e>`_.
  Vitaly Chipounov, Volodymyr Kuznetsov, George Candea. 16th Intl. Conference on
  Architectural Support for Programming Languages and Operating Systems
  (`ASPLOS <http://asplos11.cs.ucr.edu/>`_), Newport Beach, CA, March 2011.

* `Testing Closed-Source Binary Device Drivers with DDT
  <http://dslab.epfl.ch/pubs/ddt>`_. Volodymyr Kuznetsov, Vitaly Chipounov,
  George Candea. USENIX Annual Technical Conference (`USENIX
  <http://www.usenix.org/event/atc10/>`_), Boston, MA, June 2010.

* `Reverse Engineering of Binary Device Drivers with RevNIC
  <http://dslab.epfl.ch/pubs/revnic>`_. Vitaly Chipounov and George Candea. 5th
  ACM SIGOPS/EuroSys European Conference on Computer Systems (`EuroSys
  <http://eurosys2010.sigops-france.fr/>`_), Paris, France, April 2010.

* `Selective Symbolic Execution <http://dslab.epfl.ch/pubs/selsymbex>`_. Vitaly
  Chipounov, Vlad Georgescu, Cristian Zamfir, George Candea. Proc. 5th Workshop
  on Hot Topics in System Dependability, Lisbon, Portugal, June 2009

