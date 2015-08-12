============
ModuleTracer
============

The ModuleTracer records load events for modules specified by the :doc:`ModuleExecutionDetector <../ModuleExecutionDetector>` plugin.
ModuleTracer is required by offline analysis tools to map program counters to specific modules, e.g., to display user-friendly debug information.

Options
-------

This plugin does not have any option.


Required Plugins
----------------

* :doc:`ExecutionTracer <ExecutionTracer>`
* :doc:`ModuleExecutionDetector <../ModuleExecutionDetector>`

Configuration Sample
--------------------

::

    pluginsConfig.ModuleTracer = {}

