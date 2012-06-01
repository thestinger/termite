#include <stdbool.h>

#define URGENT_ON_BEEP
#define DYNAMIC_TITLE
#define CLICKABLE_URL
//#define ICON_NAME "terminal"

#ifdef CLICKABLE_URL
static const char *url_regex = "(ftp|http)s?://[-a-zA-Z0-9.?$%&/=_~#.,:;+]*";
static const char *default_browser = "/usr/bin/firefox";
#endif

// 0.0: opaque, 1.0: transparent
#define TRANSPARENCY 0.25

static const char *font = "Envy Code R 9";
static const long scrollback_lines = 1000;

static const char *foreground_color = "#ddccbb";
static const char *background_color = "#151515";

static const char *colors[16] = {
    "#3b3d3e", // black
    "#f92672", // red
    "#82b414", // green
    "#fd971f", // yellow
    "#56c2d6", // blue
    "#8c54fe", // magenta
    "#6664a7", // cyan
    "#ccccc6", // white
    "#505354", // bright black
    "#ff5995", // bright red
    "#b6e354", // bright green
    "#feed6c", // bright yellow
    "#8cedff", // bright blue
    "#9e6ffe", // bright magenta
    "#888cb1", // bright cyan
    "#f8f8f2", // bright white
};

#define CURSOR_BLINK OFF    // SYSTEM, ON or OFF
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
