A simple VTE-based terminal.

Termite looks for ``termite.cfg`` in ``$XDG_CONFIG_HOME`` (or ``~/.config`` if
unset) and then falls back to ``$XDG_CONFIG_DIRS``.

DEPENDENCIES
============

A vte version >= 0.30. A patch is currently required to expose the
functions needed for keyboard selection.

KEYBINDINGS
===========

* ``ctrl-shift-escape``: reload configuration file
* ``ctrl-shift-c``: copy to CLIPBOARD
* ``ctrl-shift-v``: paste from CLIPBOARD
* ``ctrl-shift-u``: unicode input (standard GTK binding)
* ``ctrl-tab``: start scrollback completion
* ``ctrl-shift-space``: start selection mode

During scrollback search, the current selection is changed to the search match
and copied to the PRIMARY clipboard buffer.

With the scrollback search/completion widget open, up/down cycle through
completions, escape closes the widget and enter accepts the input.

TEXT SELECTION MODE
-------------------

* ``escape``: deactivate selection mode
* ``v``: visual mode
* ``V``: visual line mode
* ``ctrl-v``: visual block mode
* ``h``/``j``/``k``/``l`` or arrow keys: move cursor left/down/up/right
* ``$``: end-of-line
* ``^``: beginning-of-line
* ``g``: jump to start of first row
* ``G``: jump to start of last row
* ``y``: copy to CLIPBOARD
* ``/``: forward search
* ``?``: reverse search
* ``u``: forward url search
* ``U``: reverse url search
* ``n``: next search match
* ``N``: previous search match

TODO
====

* tab and shift-tab bindings for completion
* better url matching regex
* hint mode overlay for urls (like elinks/vimperator/pentadactyl)
* scrollback search needs to be improved upstream [1]_
* expose keybindings in ``termite.cfg``
* text selection needs to be extended with more bindings
* output should be paused while in selection mode

.. [1] https://bugzilla.gnome.org/show_bug.cgi?id=627886
