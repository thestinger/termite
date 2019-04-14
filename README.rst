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

+----------------------+---------------------------------------------+------------------------+
| **Default Binding**  | **Description**                             | **Keybinding Name**    |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-x``     | activate url hints mode                     | url-hint               |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-r``     | reload configuration file                   | `reload-config`        |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-c``     | copy to CLIPBOARD                           | `copy-clipboard`       |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-v``     | paste from CLIPBOARD                        | `paste-clipboard`      |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-u``     | unicode input                               | (standard GTK binding) |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-e``     | emoji                                       | (standard GTK binding) |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-tab``         | start scrollback completion                 | `complete`             |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-space`` | start selection mode                        | `command-mode`         |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-t``     | open terminal in the current directory [1]_ | `launch-in-directory`  |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-up``    | scroll up a line                            | (standard binding)     |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-down``  | scroll down a line                          | (standard binding)     |
+----------------------+---------------------------------------------+------------------------+
| ``shift-pageup``     | scroll up a page                            | (standard binding)     |
+----------------------+---------------------------------------------+------------------------+
| ``shift-pagedown``   | scroll down a page                          | (standard binding)     |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-shift-l``     | reset and clear                             | (standard binding)     |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-+``           | increase font size                          | `zoom-in`              |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl--``           | decrease font size                          | `zoom-out`             |
+----------------------+---------------------------------------------+------------------------+
| ``ctrl-=``           | reset font size to default                  | `zoom-reset`           |
+----------------------+---------------------------------------------+------------------------+

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

+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| **Default Binding**               | **Description**                                           | **Keybinding Name**           |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``q`` or ``escape`` or ``ctrl-[`` | enter insert mode                                         | `exit-mode`                   |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``x``                             | activate url hints mode                                   | `find-url`                    |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``v``                             | visual mode                                               | `toggle-visual`               |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``V``                             | visual line mode                                          | `toggle-visual-line`          |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``ctrl-v``                        | visual block mode                                         | `toggle-visual-block`         |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``hjkl`` or arrow keys            | move cursor left/down/up/right                            | `move-{left,down,up,right}`   |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``w`` or ``shift-right``          | forward word                                              | `move-word-forward`           |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``e``                             | forward to end of word                                    | `move-forward-end-word`       |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``b`` or ``shift-left``           | backward word                                             | `move-word-back`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``W`` or ``ctrl-right``           | forward WORD (non-whitespace)                             | `move-word-forward`           |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``E``                             | forward to end of WORD (non-whitespace)                   | `move-forward-end-blank-word` |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``B`` or ``ctrl-left``            | backward WORD (non-whitespace)                            | `move-backward-blank-word`    |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``H``                             | jump to the top of the screen                             | `move-top-row`                |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``M``                             | jump to the middle of the screen                          | `move-middle-row`             |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``L``                             | jump to the bottom of the screen                          | `move-bottom-row`             |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``0`` or ``home``                 | move cursor to the first column in the row                | `cursor-column0`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``^``                             | beginning-of-line (first non-blank character)             | `cursor-column-move-first`    |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``$`` or ``end``                  | end-of-line                                               | `move-eol`                    |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``g``                             | jump to start of first row                                | `move-first-row`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``G``                             | jump to start of last row                                 | `move-last-row`               |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``ctrl-u``                        | move cursor a half screen up                              | `move-half-up`                |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``ctrl-d``                        | move cursor a half screen down                            | `move-half-down`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``ctrl-b``                        | move cursor a full screen up (back)                       | `move-full-up`                |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``ctrl-f``                        | move cursor a full screen down (forward)                  | `move-full-down`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``y``                             | copy to CLIPBOARD                                         | `copy-clipboard`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``/``                             | forward search                                            | `search`                      |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``?``                             | reverse search                                            | `rsearch`                     |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``u``                             | forward url search                                        | `search-forward`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``U``                             | reverse url search                                        | `search-reverse`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``o``                             | open the current selection as a url                       | `open-selection`              |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``Return``                        | open the current selection as a url and enter insert mode | `open-selection-exit-command` |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``n``                             | next search match                                         | `find-next`                   |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
| ``N``                             | previous search match                                     | `find-previous`               |
+-----------------------------------+-----------------------------------------------------------+-------------------------------+
                                                                                                                                
During scrollback search, the current selection is changed to the search match
and copied to the PRIMARY clipboard buffer.

With the text input widget focused, up/down (or tab/shift-tab) cycle through
completions, escape closes the widget and enter accepts the input.

In hints mode, the input will be accepted as soon as termite considers it a
unique match.

CONFIGURING KEYBINDINGS
-----------------------

Keybindings can be changed in the configuration file in the ``[keybindings]``
section by specifying the command followed by key combinations and the modes in which they apply:

.. code:: ini

    [keybindings]
    copy-clipboard = y:!insert,<Control><Shift>j:all
    paste-clipboard = p:!insert,<Control><Shift>k:all



To unbind a default keybinding, leave the option for that keybinding blank. 
For example, to unbind F11 (bound to ``fullscreen`` by default):

.. code:: ini

    [keybindings]
    fullscreen=

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
