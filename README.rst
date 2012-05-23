A simple VTE-based terminal.

The goal is a keyboard-centric terminal without GUI frills. Features will be
configurable at compile-time (enable/disable) in ``config.h``, along with
keybindings.

DEPENDENCIES
============

Either vte (default) or vte3, including the vte dependencies.

You can use vte3 by building with ``make GTK3=1``.

KEYBINDINGS
===========

* ``ctrl-shift-c``: copy to CLIPBOARD (useful for yanking search matches)
* ``ctrl-shift-v``: paste from CLIPBOARD

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
* ``ctrl-shift-n`` and ``ctrl-shift-p`` should be next/prev match in the
  direction of the current search, like ``n`` and ``N`` in vim.
