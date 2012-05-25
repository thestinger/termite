A simple VTE-based terminal.

Features that are available with ``tmux`` or ``screen`` such as tabs,
scrollback search/completion and keyboard url selection are left out.

Configuration is done at compile-time via ``config.h``.

DEPENDENCIES
============

A vte version >= 0.28. You can use vte3 by building with ``make GTK3=1``.

KEYBINDINGS
===========

* ``ctrl-shift-c``: copy to CLIPBOARD
* ``ctrl-shift-v``: paste from CLIPBOARD
* ``ctrl-shift-u``: unicode input (standard GTK binding)
