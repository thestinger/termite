TERMITE IS OBSOLETED BY ALACRITTY
=================================

You should use `Alacritty <https://github.com/alacritty/alacritty>`_ instead
of Termite. It has a keyboard-based selection mode inspired by Termite and
Alacritty 0.8 adds a generic regex hints mode comparable to Termite's URL
hints mode. The user interface is very much in the same spirit as Termite
including a very minimal user interface delegating handling tabs and splits to
a window manager like i3. Alacritty is dramatically faster than VTE along with
being significantly more robust and secure. It's written in a modern, safe
programming language (Rust) and uses OpenGL for efficient rendering.

If you've packaged Termite in a repository, we would highly appreciate if you
could communicate our recommendation to end users as part of phasing out and
retiring the package. Alacritty is the only proper replacement for Termite and
it took until the 0.8 release currently available as a release candidate for us
to be able to wholeheartedly recommend it.

We strongly recommend against trying to continue the development of Termite
with a fork. You should contribute to Alacritty instead. VTE is a terrible base
for building a modern, fast and safe terminal emulator. It's slow, brittle and
difficult to improve. VTE is treated as simply being the GNOME Terminal widget
rather than a library truly intended to be useful to others. They've gone out
of the way to keep useful APIs private due to hostility towards implementing
any kind of user interface beyond what they provide. In 2012, we submitted a
`tiny patch exposing the APIs needed for the keyboard text selection, hints
mode and other features <https://bugzilla.gnome.org/show_bug.cgi?id=679658>`_.
Despite support from multiple other projects, the patch was rejected. It's now
almost a decade later and no progress has been made. There is no implementation
of these kinds of features in VTE and it's unlikely they'll be provided either
internally or as flexible APIs. This is the tip of the iceberg when it comes to
their hostility towards other projects using VTE as a library. GTK and most of
the GNOME project are much of the same. Avoid them and don't make the mistake
of thinking their libraries are meant for others to use.

INTRODUCTION
============

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
| ``ctrl-shift-l``     | reset and clear                             |
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
| ``e``                             | forward to end of word                                    |
+-----------------------------------+-----------------------------------------------------------+
| ``b`` or ``shift-left``           | backward word                                             |
+-----------------------------------+-----------------------------------------------------------+
| ``W`` or ``ctrl-right``           | forward WORD (non-whitespace)                             |
+-----------------------------------+-----------------------------------------------------------+
| ``E``                             | forward to end of WORD (non-whitespace)                   |
+-----------------------------------+-----------------------------------------------------------+
| ``B`` or ``ctrl-left``            | backward WORD (non-whitespace)                            |
+-----------------------------------+-----------------------------------------------------------+
| ``H``                             | jump to the top of the screen                             |
+-----------------------------------+-----------------------------------------------------------+
| ``M``                             | jump to the middle of the screen                          |
+-----------------------------------+-----------------------------------------------------------+
| ``L``                             | jump to the bottom of the screen                          |
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

Internal padding can be added by using CSS to style Termite. Adding
the following snippet to ``$XDG_CONFIG_HOME/gtk-3.0/gtk.css`` (or
``~/.config/gtk-3.0/gtk.css``) will add uniform 2px padding around the edges:

.. code:: css

    .termite {
        padding: 2px;
    }

This can also be used to add varying amounts of padding to each side via
standard usage of the CSS padding property.

TERMINFO
========

When working on a remote system with termite's terminfo missing, an error might
occur:

::

    Error opening terminal: xterm-termite

To solve this issue, install the termite terminfo on your remote system.

On Arch Linux:

::

        pacman -S termite-terminfo

On other systems:


::

    wget https://raw.githubusercontent.com/thestinger/termite/master/termite.terminfo
    tic -x termite.terminfo
