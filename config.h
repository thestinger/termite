#include <stdbool.h>

#define URGENT_ON_BEEP
#define DYNAMIC_TITLE
#define CLICKABLE_URL
//#define ICON_NAME "terminal"

static const char *url_regex = "(ftp|http)s?://[-a-zA-Z0-9.?$%&/=_~#.,:;+]*";

#ifdef CLICKABLE_URL
#define URL_COMMAND(URL_MATCH) {"/usr/bin/firefox", URL_MATCH, NULL}
#endif

// 0.0: opaque, 1.0: transparent
//#define TRANSPARENCY 0.2

static const char *font = "Monospace 9";
static const long scrollback_lines = 1000;
static const bool cursor_blink = false;

static const char *foreground_color = "#dcdccc";
static const char *background_color = "#3f3f3f";

static const char *colors[16] = {
    "#3f3f3f", // black
    "#705050", // red
    "#60b48a", // green
    "#dfaf8f", // yellow
    "#506070", // blue
    "#dc8cc3", // magenta
    "#8cd0d3", // cyan
    "#dcdccc", // white
    "#709080", // bright black
    "#dca3a3", // bright red
    "#c3bf9f", // bright green
    "#f0dfaf", // bright yellow
    "#94bff3", // bright blue
    "#ec93d3", // bright magenta
    "#93e0e3", // bright cyan
    "#ffffff", // bright white
};

#define CURSOR_BLINK SYSTEM // SYSTEM, ON or OFF
#define CURSOR_SHAPE BLOCK  // BLOCK, UNDERLINE or IBEAM

static const bool resize_grip = false;

static const bool scroll_on_output = false;
static const bool scroll_on_keystroke = true;

static const bool audible_bell = false;
static const bool visible_bell = false;

static const bool mouse_autohide = false;

static const char *term = "vte-256color";

// keybindings
#define KEY_COPY    c
#define KEY_PASTE   v
#define KEY_PREV    p
#define KEY_NEXT    n
#define KEY_SEARCH  f
#define KEY_RSEARCH r
#define KEY_URL     j
#define KEY_RURL    k
