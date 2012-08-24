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
* ``ctrl-shift-space``: start command mode

COMMAND MODE
-------------------

* ``escape``: deactivate command mode
* ``v``: visual mode
* ``V``: visual line mode
* ``ctrl-v``: visual block mode
* ``h``/``j``/``k``/``l`` or arrow keys: move cursor left/down/up/right
* ``w``/``b``: forward/backward word
* ``$``: end-of-line
* ``^``: beginning-of-line
* ``g``: jump to start of first row
* ``G``: jump to start of last row
* ``y``: copy to CLIPBOARD
* ``/``: forward search
* ``?``: reverse search
* ``u``: forward url search
* ``U``: reverse url search
* ``o``: open the current selection as a url
* ``Return``: open the current selection as a url and exit command mode
* ``n``: next search match
* ``N``: previous search match

During scrollback search, the current selection is changed to the search match
and copied to the PRIMARY clipboard buffer.

With the scrollback search/completion widget open, up/down cycle through
completions, escape closes the widget and enter accepts the input.
