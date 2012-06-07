static const char *url_regex = "(ftp|http)s?://[-a-zA-Z0-9.?$%&/=_~#.,:;+()]*";

#define URL_COMMAND(URL_MATCH) {"/usr/bin/firefox", URL_MATCH, NULL}

// 0.0: opaque, 1.0: transparent
//#define TRANSPARENCY 0.2

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
