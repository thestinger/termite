A keyboard-centric VTE-based terminal, aimed at use within a window manager
with tiling and/or tabbing support.

Termite looks for the configuration file in the following order:
``$XDG_CONFIG_HOME/termite/config``, ``~/.config/termite/config``,
``$XDG_CONFIG_DIRS/termite/config``, ``/etc/xdg/termite/config``.

Termite's exit status is 1 on a failure, including a termination of the child
process from an uncaught signal. Otherwise the exit status is that of the child
process.

DEPENDENCIES
============

The `vte-ng <https://github.com/thestinger/vte-ng>`_ project is required until
VTE exposes the necessary functions for keyboard text selection and URL hints
(if ever). A simple patch `has been submitted upstream
<https://bugzilla.gnome.org/show_bug.cgi?id=679658#c10>`_ but they're unwilling
to expose functionality that's not required by GNOME Terminal even if there's
no extra maintenance (it already exists internally) and no additional backwards
compatibility hazards.

If no browser is configured and $BROWSER is unset, xdg-open from xdg-utils is
used as a fallback.

BUILDING VTE-NG
---------------

To build vte, you'll need

* gobject-introspection-1.0 (For Debian, this package is named libgirepository1.0-dev)
* gperf
* gtk-doc-tools
* valac

Once dependencies are met, you can build and install by running the following:

::

    git clone https://github.com/thestinger/vte-ng.git
    cd vte-ng
    ./autogen.sh
    ./configure
    make && make install

If when running termite you get an error about
``vte_terminal_get_cursor_position``, that indicates that vte-ng isn't
installed properly yet.

BUILDING
========
::

    git clone --recursive https://github.com/thestinger/termite.git
    cd termite && make

KEYBINDINGS
===========

INSERT MODE
-----------

+----------------------+---------------------------------------------+
| ``ctrl-shift-x``     | activate url hints mode                     |
+----------------------+---------------------------------------------+
| ``ctrl-shift-r``     | reload configuration file                   |
+----------------------+---------------------------------------------+
| ``ctrl-shift-c``     | copy to CLIPBOARD                           |
+----------------------+---------------------------------------------+
| ``ctrl-shift-v``     | paste from CLIPBOARD                        |
+----------------------+---------------------------------------------+
| ``ctrl-shift-u``     | unicode input (standard GTK binding)        |
+----------------------+---------------------------------------------+
| ``ctrl-tab``         | start scrollback completion                 |
+----------------------+---------------------------------------------+
| ``ctrl-shift-space`` | start selection mode                        |
+----------------------+---------------------------------------------+
| ``ctrl-shift-t``     | open terminal in the current directory [1]_ |
+----------------------+---------------------------------------------+
| ``ctrl-shift-up``    | scroll up a line                            |
+----------------------+---------------------------------------------+
| ``ctrl-shift-down``  | scroll down a line                          |
+----------------------+---------------------------------------------+
| ``shift-pageup``     | scroll up a page                            |
+----------------------+---------------------------------------------+
| ``shift-pagedown``   | scroll down a page                          |
+----------------------+---------------------------------------------+
| ``ctrl-+``           | increase font size                          |
+----------------------+---------------------------------------------+
| ``ctrl--``           | decrease font size                          |
+----------------------+---------------------------------------------+
| ``ctrl-=``           | reset font size to default                  |
+----------------------+---------------------------------------------+

.. [1] The directory can be set by a process running in the terminal. For
       example, with zsh:

       .. code:: sh

            if [[ $TERM == xterm-termite ]]; then
              . /etc/profile.d/vte.sh
              __vte_osc7
            fi
       ::

       For example, with bash:

       .. code:: sh

            if [[ $TERM == xterm-termite ]]; then
              . /etc/profile.d/vte.sh
              __vte_prompt_command
            fi

SELECTION MODE
--------------

+-----------------------------------+-----------------------------------------------------------+
| ``q`` or ``escape`` or ``ctrl-[`` | enter insert mode                                         |
+-----------------------------------+-----------------------------------------------------------+
| ``x``                             | activate url hints mode                                   |
+-----------------------------------+-----------------------------------------------------------+
| ``v``                             | visual mode                                               |
+-----------------------------------+-----------------------------------------------------------+
| ``V``                             | visual line mode                                          |
+-----------------------------------+-----------------------------------------------------------+
| ``ctrl-v``                        | visual block mode                                         |
+-----------------------------------+-----------------------------------------------------------+
| ``hjkl`` or arrow keys            | move cursor left/down/up/right                            |
+-----------------------------------+-----------------------------------------------------------+
| ``w`` or ``shift-right``          | forward word                                              |
+-----------------------------------+-----------------------------------------------------------+
| ``b`` or ``shift-left``           | backward word                                             |
+-----------------------------------+-----------------------------------------------------------+
| ``W`` or ``ctrl-right``           | forward WORD (non-whitespace)                             |
+-----------------------------------+-----------------------------------------------------------+
| ``B`` or ``ctrl-left``            | backward WORD (non-whitespace)                            |
+-----------------------------------+-----------------------------------------------------------+
| ``0`` or ``home``                 | move cursor to the first column in the row                |
+-----------------------------------+-----------------------------------------------------------+
| ``^``                             | beginning-of-line (first non-blank character)             |
+-----------------------------------+-----------------------------------------------------------+
| ``$`` or ``end``                  | end-of-line                                               |
+-----------------------------------+-----------------------------------------------------------+
| ``g``                             | jump to start of first row                                |
+-----------------------------------+-----------------------------------------------------------+
| ``G``                             | jump to start of last row                                 |
+-----------------------------------+-----------------------------------------------------------+
| ``ctrl-u``                        | move cursor a half screen up                              |
+-----------------------------------+-----------------------------------------------------------+
| ``ctrl-d``                        | move cursor a half screen down                            |
+-----------------------------------+-----------------------------------------------------------+
| ``ctrl-b``                        | move cursor a full screen up (back)                       |
+-----------------------------------+-----------------------------------------------------------+
| ``ctrl-f``                        | move cursor a full screen down (forward)                  |
+-----------------------------------+-----------------------------------------------------------+
| ``y``                             | copy to CLIPBOARD                                         |
+-----------------------------------+-----------------------------------------------------------+
| ``/``                             | forward search                                            |
+-----------------------------------+-----------------------------------------------------------+
| ``?``                             | reverse search                                            |
+-----------------------------------+-----------------------------------------------------------+
| ``u``                             | forward url search                                        |
+-----------------------------------+-----------------------------------------------------------+
| ``U``                             | reverse url search                                        |
+-----------------------------------+-----------------------------------------------------------+
| ``o``                             | open the current selection as a url                       |
+-----------------------------------+-----------------------------------------------------------+
| ``Return``                        | open the current selection as a url and enter insert mode |
+-----------------------------------+-----------------------------------------------------------+
| ``n``                             | next search match                                         |
+-----------------------------------+-----------------------------------------------------------+
| ``N``                             | previous search match                                     |
+-----------------------------------+-----------------------------------------------------------+

During scrollback search, the current selection is changed to the search match
and copied to the PRIMARY clipboard buffer.

With the text input widget focused, up/down (or tab/shift-tab) cycle through
completions, escape closes the widget and enter accepts the input.

In hints mode, the input will be accepted as soon as termite considers it a
unique match.

PADDING
=======

Internal padding can be added by using CSS to style the VTE widget. Adding the
following snippet to ``$XDG_CONFIG_HOME/gtk-3.0/gtk.css`` (or
``~/.config/gtk-3.0/gtk.css``) will add uniform 2px padding around the edges:

.. code:: css

    VteTerminal, vte-terminal {
        padding: 2px;
    }

This can also be used to add varying amounts of padding to each side via
standard usage of the CSS padding property.

TERMINFO
========

termite.terminfo is a terminfo entry which defines termite's capabilities to
libraries such as nvi, rogue, and ncurses, as well as apps that use these
libraries (like tmux.) If you installed termite via `make install`, this was
done for you on your local system. If you need to do this manually or do it
on any remote servers, the simplest way to add termite's terminfo is to use
``tic``:

::

    tic -x termite.terminfo

Without this, apps (like tmux) may not run properly.
