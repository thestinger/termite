A simple VTE-based terminal.

DEPENDENCIES
============

Either vte (default) or vte3, including the vte dependencies.

You can use vte3 by building with ``make GTK3=1``.

KEYBINDINGS
===========

Scrollback search:

* ``ctrl-/``: start forward search
* ``ctrl-?``: start backward search
* ``ctrl-shift-n``: search forward
* ``ctrl-shift-p``: search backward
* ``ctrl-shift-u``: start forward url search
* ``ctrl-shift-i``: start backward url search

While typing search pattern:

* ``enter``: start search
* ``escape``: cancel search

TODO
====

* saner scrollback search widget
* better keyboard url selection/opening
* configurable keybindings
* keyword autocompletion for words in scrollback (ctrl-tab and ctrl-shift-tab)
