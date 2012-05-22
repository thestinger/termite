#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

static const char *url_regex = "(ftp|http)s?://[-a-zA-Z0-9.?$%&/=_~#.,:;+]*";

static const char *font = "Monospace 9";
static const long scrollback_lines = 1000;

static const char *foreground_color = "#dcdccc";
static const char *background_color = "#3f3f3f";

static const char *colors[16] = {
    "#3f3f3f",
    "#705050",
    "#60b48a",
    "#dfaf8f",
    "#506070",
    "#dc8cc3",
    "#8cd0d3",
    "#dcdccc",
    "#709080",
    "#dca3a3",
    "#c3bf9f",
    "#f0dfaf",
    "#94bff3",
    "#ec93d3",
    "#93e0e3",
    "#ffffff",
};

static const bool scroll_on_output = false;
static const bool scroll_on_keystroke = true;

static const bool audible_bell = false;
static const bool visible_bell = false;

static const bool mouse_autohide = false;

static const char *term = "xterm-256color";

#endif
