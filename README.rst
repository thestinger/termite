A simple VTE-based terminal.

Configuration is done at compile-time via ``config.h``.

DEPENDENCIES
============

A vte version >= 0.30.

KEYBINDINGS
===========

* ``ctrl-shift-c``: copy to CLIPBOARD
* ``ctrl-shift-v``: paste from CLIPBOARD
* ``ctrl-shift-u``: unicode input (standard GTK binding)
* ``ctrl-shift-f``: start forward search
* ``ctrl-shift-b``: start reverse search
* ``ctrl-shift-j``: start forward url search
* ``ctrl-shift-k``: start reverse url search
* ``ctrl-shift-n``: jump to next search match
* ``ctrl-shift-p``: jump to previous search match
* ``ctrl-tab``: start scrollback completion

During scrollback search, The current selection is changed to the search match
and copied to the PRIMARY clipboard buffer.

With the scrollback completion/widget open, up/down cycle through completions,
escape closes the widget and enter accepts the input.

TODO
====

* tab and shift-tab bindings for completion
* better url matching regex
* hint mode overlay for urls (like elinks/vimperator/pentadactyl)
* scrollback search needs to be improved upstream [1]_
* expose more options in ``config.h``

.. [1] https://bugzilla.gnome.org/show_bug.cgi?id=627886
