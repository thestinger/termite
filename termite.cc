/*
 * Copyright (C) 2013 Daniel Micay
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>
#include <set>
#include <string>

#include <gtk/gtk.h>
#include <vte/vte.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "url_regex.hh"
#include "util/clamp.hh"
#include "util/maybe.hh"
#include "util/memory.hh"

using namespace std::placeholders;

/* Allow scales a bit smaller and a bit larger than the usual pango ranges */
#define TERMINAL_SCALE_XXX_SMALL   (PANGO_SCALE_XX_SMALL/1.2)
#define TERMINAL_SCALE_XXXX_SMALL  (TERMINAL_SCALE_XXX_SMALL/1.2)
#define TERMINAL_SCALE_XXXXX_SMALL (TERMINAL_SCALE_XXXX_SMALL/1.2)
#define TERMINAL_SCALE_XXX_LARGE   (PANGO_SCALE_XX_LARGE*1.2)
#define TERMINAL_SCALE_XXXX_LARGE  (TERMINAL_SCALE_XXX_LARGE*1.2)
#define TERMINAL_SCALE_XXXXX_LARGE (TERMINAL_SCALE_XXXX_LARGE*1.2)
#define TERMINAL_SCALE_MINIMUM     (TERMINAL_SCALE_XXXXX_SMALL/1.2)
#define TERMINAL_SCALE_MAXIMUM     (TERMINAL_SCALE_XXXXX_LARGE*1.2)

static const std::vector<double> zoom_factors = {
    TERMINAL_SCALE_MINIMUM,
    TERMINAL_SCALE_XXXXX_SMALL,
    TERMINAL_SCALE_XXXX_SMALL,
    TERMINAL_SCALE_XXX_SMALL,
    PANGO_SCALE_XX_SMALL,
    PANGO_SCALE_X_SMALL,
    PANGO_SCALE_SMALL,
    PANGO_SCALE_MEDIUM,
    PANGO_SCALE_LARGE,
    PANGO_SCALE_X_LARGE,
    PANGO_SCALE_XX_LARGE,
    TERMINAL_SCALE_XXX_LARGE,
    TERMINAL_SCALE_XXXX_LARGE,
    TERMINAL_SCALE_XXXXX_LARGE,
    TERMINAL_SCALE_MAXIMUM
};

enum class overlay_mode {
    hidden,
    search,
    rsearch,
    completion,
    urlselect
};

enum class vi_mode {
    insert,
    command,
    visual,
    visual_line,
    visual_block
};

struct select_info {
    vi_mode mode;
    long begin_col;
    long begin_row;
    long origin_col;
    long origin_row;
};

struct url_data {
    url_data(char *u, long c, long r) : url(u, g_free), col(c), row(r) {}
    std::unique_ptr<char, decltype(&g_free)> url;
    long col, row;
};

struct search_panel_info {
    GtkWidget *entry;
    GtkWidget *da;
    overlay_mode mode;
    std::vector<url_data> url_list;
    char *fulltext;
};

struct hint_info {
    PangoFontDescription *font;
    cairo_pattern_t *fg, *bg, *af, *ab, *border;
    double padding, border_width, roundness;
};

struct config_info {
    hint_info hints;
    char *browser;
    gboolean dynamic_title, urgent_on_bell, clickable_url, size_hints;
    gboolean filter_unmatched_urls, modify_other_keys;
    gboolean fullscreen;
    int tag;
    char *config_file;
    gdouble font_scale;
};

struct keybind_info {
    GtkWindow *window;
    VteTerminal *vte;
    search_panel_info panel;
    select_info select;
    config_info config;
    std::function<void (GtkWindow *)> fullscreen_toggle;
};

struct draw_cb_info {
    VteTerminal *vte;
    search_panel_info *panel;
    hint_info *hints;
    gboolean filter_unmatched_urls;
};

static void launch_browser(char *browser, char *url);
static void window_title_cb(VteTerminal *vte, gboolean *dynamic_title);
static gboolean window_state_cb(GtkWindow *window, GdkEventWindowState *event, keybind_info *info);
static gboolean key_press_cb(VteTerminal *vte, GdkEventKey *event, keybind_info *info);
static gboolean entry_key_press_cb(GtkEntry *entry, GdkEventKey *event, keybind_info *info);
static gboolean position_overlay_cb(GtkBin *overlay, GtkWidget *widget, GdkRectangle *alloc);
static gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event, const config_info *info);
static void bell_cb(GtkWidget *vte, gboolean *urgent_on_bell);
static gboolean focus_cb(GtkWindow *window);

static GtkTreeModel *create_completion_model(VteTerminal *vte);
static void search(VteTerminal *vte, const char *pattern, bool reverse);
static void overlay_show(search_panel_info *info, overlay_mode mode, VteTerminal *vte);
static void get_vte_padding(VteTerminal *vte, int *left, int *top, int *right, int *bottom);
static char *check_match(VteTerminal *vte, GdkEventButton *event);
static void load_config(GtkWindow *window, VteTerminal *vte, GtkWidget *scrollbar, GtkWidget *hbox,
                        config_info *info, char **icon, bool *show_scrollbar);
static void set_config(GtkWindow *window, VteTerminal *vte, GtkWidget *scrollbar, GtkWidget *hbox,
                       config_info *info, char **icon, bool *show_scrollbar,
                       GKeyFile *config);
static long first_row(VteTerminal *vte);

static std::function<void ()> reload_config;

static void override_background_color(GtkWidget *widget, GdkRGBA *rgba) {
    GtkCssProvider *provider = gtk_css_provider_new();

    gchar *colorstr = gdk_rgba_to_string(rgba);
    char *css = g_strdup_printf("* { background-color: %s; }", colorstr);
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    g_free(colorstr);
    g_free(css);

    gtk_style_context_add_provider(gtk_widget_get_style_context(widget),
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static const std::map<int, const char *> modify_table = {
    { GDK_KEY_Tab,        "\033[27;5;9~"  },
    { GDK_KEY_Return,     "\033[27;5;13~" },
    { GDK_KEY_apostrophe, "\033[27;5;39~" },
    { GDK_KEY_comma,      "\033[27;5;44~" },
    { GDK_KEY_minus,      "\033[27;5;45~" },
    { GDK_KEY_period,     "\033[27;5;46~" },
    { GDK_KEY_0,          "\033[27;5;48~" },
    { GDK_KEY_1,          "\033[27;5;49~" },
    { GDK_KEY_9,          "\033[27;5;57~" },
    { GDK_KEY_semicolon,  "\033[27;5;59~" },
    { GDK_KEY_equal,      "\033[27;5;61~" },
    { GDK_KEY_exclam,     "\033[27;6;33~" },
    { GDK_KEY_quotedbl,   "\033[27;6;34~" },
    { GDK_KEY_numbersign, "\033[27;6;35~" },
    { GDK_KEY_dollar,     "\033[27;6;36~" },
    { GDK_KEY_percent,    "\033[27;6;37~" },
    { GDK_KEY_ampersand,  "\033[27;6;38~" },
    { GDK_KEY_parenleft,  "\033[27;6;40~" },
    { GDK_KEY_parenright, "\033[27;6;41~" },
    { GDK_KEY_asterisk,   "\033[27;6;42~" },
    { GDK_KEY_plus,       "\033[27;6;43~" },
    { GDK_KEY_colon,      "\033[27;6;58~" },
    { GDK_KEY_less,       "\033[27;6;60~" },
    { GDK_KEY_greater,    "\033[27;6;62~" },
    { GDK_KEY_question,   "\033[27;6;63~" },
};

static const std::map<int, const char *> modify_meta_table = {
    { GDK_KEY_Tab,        "\033[27;13;9~"  },
    { GDK_KEY_Return,     "\033[27;13;13~" },
    { GDK_KEY_apostrophe, "\033[27;13;39~" },
    { GDK_KEY_comma,      "\033[27;13;44~" },
    { GDK_KEY_minus,      "\033[27;13;45~" },
    { GDK_KEY_period,     "\033[27;13;46~" },
    { GDK_KEY_0,          "\033[27;13;48~" },
    { GDK_KEY_1,          "\033[27;13;49~" },
    { GDK_KEY_9,          "\033[27;13;57~" },
    { GDK_KEY_semicolon,  "\033[27;13;59~" },
    { GDK_KEY_equal,      "\033[27;13;61~" },
    { GDK_KEY_exclam,     "\033[27;14;33~" },
    { GDK_KEY_quotedbl,   "\033[27;14;34~" },
    { GDK_KEY_numbersign, "\033[27;14;35~" },
    { GDK_KEY_dollar,     "\033[27;14;36~" },
    { GDK_KEY_percent,    "\033[27;14;37~" },
    { GDK_KEY_ampersand,  "\033[27;14;38~" },
    { GDK_KEY_parenleft,  "\033[27;14;40~" },
    { GDK_KEY_parenright, "\033[27;14;41~" },
    { GDK_KEY_asterisk,   "\033[27;14;42~" },
    { GDK_KEY_plus,       "\033[27;14;43~" },
    { GDK_KEY_colon,      "\033[27;14;58~" },
    { GDK_KEY_less,       "\033[27;14;60~" },
    { GDK_KEY_greater,    "\033[27;14;62~" },
    { GDK_KEY_question,   "\033[27;14;63~" },
};

static gboolean modify_key_feed(GdkEventKey *event, keybind_info *info,
                                const std::map<int, const char *>& table) {
    if (info->config.modify_other_keys) {
        unsigned int keyval = gdk_keyval_to_lower(event->keyval);
        auto entry = table.find((int)keyval);

        if (entry != table.end()) {
            vte_terminal_feed_child(info->vte, entry->second, -1);
            return TRUE;
        }
    }
    return FALSE;
}

void launch_browser(char *browser, char *url) {
    char *browser_cmd[3] = {browser, url, nullptr};
    GError *error = nullptr;

    if (!browser) {
        g_printerr("browser not set, can't open url\n");
        return;
    }

    GPid child_pid;
    if (!g_spawn_async(nullptr, browser_cmd, nullptr, G_SPAWN_SEARCH_PATH,
                       nullptr, nullptr, &child_pid, &error)) {
        g_printerr("error launching '%s': %s\n", browser, error->message);
        g_error_free(error);
    }
    g_spawn_close_pid(child_pid);
}

static void set_size_hints(GtkWindow *window, VteTerminal *vte) {
    static const GdkWindowHints wh = (GdkWindowHints)(GDK_HINT_RESIZE_INC | GDK_HINT_MIN_SIZE |
                                                      GDK_HINT_BASE_SIZE);
    const int char_width = (int)vte_terminal_get_char_width(vte);
    const int char_height = (int)vte_terminal_get_char_height(vte);
    int padding_left, padding_top, padding_right, padding_bottom;
    get_vte_padding(vte, &padding_left, &padding_top, &padding_right, &padding_bottom);

    GdkGeometry hints;
    hints.base_width = char_width + padding_left + padding_right;
    hints.base_height = char_height + padding_top + padding_bottom;
    hints.min_width = hints.base_width;
    hints.min_height = hints.base_height;
    hints.width_inc  = char_width;
    hints.height_inc = char_height;

    gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &hints, wh);
}

static void launch_in_directory(VteTerminal *vte) {
    const char *uri = vte_terminal_get_current_directory_uri(vte);
    if (!uri) {
        g_printerr("no directory uri set\n");
        return;
    }
    auto dir = make_unique(g_filename_from_uri(uri, nullptr, nullptr), g_free);
    char term[] = "termite"; // maybe this should be argv[0]
    char *cmd[] = {term, nullptr};
    g_spawn_async(dir.get(), cmd, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr);
}

static void find_urls(VteTerminal *vte, search_panel_info *panel_info) {
    GRegex *regex = g_regex_new(url_regex, G_REGEX_CASELESS, G_REGEX_MATCH_NOTEMPTY, nullptr);
    GArray *attributes = g_array_new(FALSE, FALSE, sizeof(VteCharAttributes));
    auto content = make_unique(vte_terminal_get_text(vte, nullptr, nullptr, attributes), g_free);

    for (char *s_ptr = content.get(), *saveptr; ; s_ptr = nullptr) {
        const char *token = strtok_r(s_ptr, "\n", &saveptr);
        if (!token) {
            break;
        }

        GError *error = nullptr;
        GMatchInfo *info;

        g_regex_match_full(regex, token, -1, 0, (GRegexMatchFlags)0, &info, &error);
        while (g_match_info_matches(info)) {
            int pos;
            g_match_info_fetch_pos(info, 0, &pos, nullptr);

            const long first_row = g_array_index(attributes, VteCharAttributes, 0).row;
            const auto attr = g_array_index(attributes, VteCharAttributes, token + pos - content.get());

            panel_info->url_list.emplace_back(g_match_info_fetch(info, 0),
                                              attr.column,
                                              attr.row - first_row);
            g_match_info_next(info, &error);
        }

        g_match_info_free(info);

        if (error) {
            g_printerr("error while matching: %s\n", error->message);
            g_error_free(error);
        }
    }
    g_regex_unref(regex);
    g_array_free(attributes, TRUE);
}

static void launch_url(char *browser, const char *text, search_panel_info *info) {
    char *end;
    errno = 0;
    unsigned long id = strtoul(text, &end, 10);
    if (!errno && id && id <= info->url_list.size() && !*end) {
        launch_browser(browser, info->url_list[id - 1].url.get());
    } else {
        g_printerr("url hint invalid: %s\n", text);
    }
}

static void draw_rectangle(cairo_t *cr, double x, double y, double height,
                           double width, double radius) {
    double a = x, b = x + height, c = y, d = y + width;
    cairo_arc(cr, a + radius, c + radius, radius, 2*(M_PI/2), 3*(M_PI/2));
    cairo_arc(cr, b - radius, c + radius, radius, 3*(M_PI/2), 4*(M_PI/2));
    cairo_arc(cr, b - radius, d - radius, radius, 0*(M_PI/2), 1*(M_PI/2));
    cairo_arc(cr, a + radius, d - radius, radius, 1*(M_PI/2), 2*(M_PI/2));
    cairo_close_path(cr);
}

static void draw_marker(cairo_t *cr, const PangoFontDescription *desc,
                        const hint_info *hints, long x, long y, const char *msg,
                        bool active) {
    cairo_text_extents_t ext;
    int width, height;

    cairo_text_extents(cr, msg, &ext);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, msg, -1);
    pango_layout_get_size(layout, &width, &height);

    draw_rectangle(cr, static_cast<double>(x), static_cast<double>(y),
                   static_cast<double>(width / PANGO_SCALE) + hints->padding * 2,
                   static_cast<double>(height / PANGO_SCALE) + hints->padding * 2,
                   hints->roundness);
    cairo_set_source(cr, hints->border);
    cairo_set_line_width(cr, hints->border_width);
    cairo_stroke_preserve(cr);
    cairo_set_source(cr, active ? hints->ab : hints->bg);
    cairo_fill(cr);

    cairo_new_path(cr);
    cairo_move_to(cr, static_cast<double>(x) + hints->padding,
                  static_cast<double>(y) + hints->padding);

    cairo_set_source(cr, active ? hints->af : hints->fg);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_layout_path(cr, layout);
    cairo_fill(cr);

    g_object_unref(layout);
}

static gboolean draw_cb(const draw_cb_info *info, cairo_t *cr) {
    if (!info->panel->url_list.empty()) {
        char buffer[std::numeric_limits<unsigned>::digits10 + 1];

        int padding_left, padding_top, padding_right, padding_bottom;
        const long cw = vte_terminal_get_char_width(info->vte);
        const long ch = vte_terminal_get_char_height(info->vte);
        const PangoFontDescription *desc = info->hints->font ?
            info->hints->font : vte_terminal_get_font(info->vte);
        size_t len = info->panel->fulltext == nullptr ?
            0 : strlen(info->panel->fulltext);

        cairo_set_line_width(cr, 1);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_stroke(cr);

        get_vte_padding(info->vte, &padding_left, &padding_top, &padding_right, &padding_bottom);

        for (unsigned i = 0; i < info->panel->url_list.size(); i++) {
            const url_data &data = info->panel->url_list[i];
            const long x = data.col * cw + padding_left;
            const long y = data.row * ch + padding_top;
            bool active = false;

            snprintf(buffer, sizeof(buffer), "%u", i + 1);
            if (len)
                active = strncmp(buffer, info->panel->fulltext, len) == 0;

            if (!info->filter_unmatched_urls || active || len == 0)
                draw_marker(cr, desc, info->hints, x, y, buffer, active);
        }
    }

    return FALSE;
}

static void update_selection(VteTerminal *vte, const select_info *select) {
    vte_terminal_unselect_all(vte);

    if (select->mode == vi_mode::command) {
        return;
    }

    const long n_columns = vte_terminal_get_column_count(vte);
    long cursor_col, cursor_row, selection_x_end;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    vte_terminal_set_selection_block_mode(vte, select->mode == vi_mode::visual_block);

    if (select->mode == vi_mode::visual) {
        const long begin = select->begin_row * n_columns + select->begin_col;
        const long end = cursor_row * n_columns + cursor_col;
        if (begin < end) {
            selection_x_end = cursor_col;
#if VTE_CHECK_VERSION(0, 55, 0)
            selection_x_end += 1;
#endif
            vte_terminal_select_text(vte, select->begin_col, select->begin_row,
                                     selection_x_end, cursor_row);
        } else {
            selection_x_end = select->begin_col;
#if VTE_CHECK_VERSION(0, 55, 0)
            selection_x_end += 1;
#endif
            vte_terminal_select_text(vte, cursor_col, cursor_row,
                                     selection_x_end, select->begin_row);
        }
    } else if (select->mode == vi_mode::visual_line) {
        selection_x_end = n_columns - 1;
#if VTE_CHECK_VERSION(0, 55, 0)
        selection_x_end += 1;
#endif
        vte_terminal_select_text(vte, 0,
                                 std::min(select->begin_row, cursor_row),
                                 selection_x_end,
                                 std::max(select->begin_row, cursor_row));
    } else if (select->mode == vi_mode::visual_block) {
        selection_x_end = std::max(select->begin_col, cursor_col);
#if VTE_CHECK_VERSION(0, 55, 0)
        selection_x_end += 1;
#endif
        vte_terminal_select_text(vte,
                                 std::min(select->begin_col, cursor_col),
                                 std::min(select->begin_row, cursor_row),
                                 selection_x_end,
                                 std::max(select->begin_row, cursor_row));
    }

    vte_terminal_copy_primary(vte);
}

static void enter_command_mode(VteTerminal *vte, select_info *select) {
    vte_terminal_disconnect_pty_read(vte);
    select->mode = vi_mode::command;
    vte_terminal_get_cursor_position(vte, &select->origin_col, &select->origin_row);
    update_selection(vte, select);
}

static void exit_command_mode(VteTerminal *vte, select_info *select) {
    vte_terminal_set_cursor_position(vte, select->origin_col, select->origin_row);
    vte_terminal_connect_pty_read(vte);
    vte_terminal_unselect_all(vte);
    select->mode = vi_mode::insert;
}

static void toggle_visual(VteTerminal *vte, select_info *select, vi_mode mode) {
    if (select->mode == mode) {
        select->mode = vi_mode::command;
    } else {
        if (select->mode == vi_mode::command) {
            vte_terminal_get_cursor_position(vte, &select->begin_col, &select->begin_row);
        }
        select->mode = mode;
    }
    update_selection(vte, select);
}

static long first_row(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    return (long)gtk_adjustment_get_lower(adjust);
}

static long last_row(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    return (long)gtk_adjustment_get_upper(adjust) - 1;
}

static long top_row(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    return (long)gtk_adjustment_get_value(adjust);
}

static long middle_row(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    return (long)gtk_adjustment_get_value(adjust) +
                (long)vte_terminal_get_row_count(vte) / 2;
}

static long bottom_row(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    return (long)gtk_adjustment_get_value(adjust) +
                (long)vte_terminal_get_row_count(vte) - 1;
}

static void update_scroll(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    const double scroll_row = gtk_adjustment_get_value(adjust);
    const long n_rows = vte_terminal_get_row_count(vte);
    long cursor_row;
    vte_terminal_get_cursor_position(vte, nullptr, &cursor_row);

    if ( (double)cursor_row < scroll_row) {
        gtk_adjustment_set_value(adjust, (double)cursor_row);
    } else if (cursor_row - n_rows >= (long)scroll_row) {
        gtk_adjustment_set_value(adjust, (double)(cursor_row - n_rows + 1));
    }
}

static void move(VteTerminal *vte, select_info *select, long col, long row) {
    const long end_col = vte_terminal_get_column_count(vte) - 1;

    long cursor_col, cursor_row;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    VteCursorBlinkMode mode = vte_terminal_get_cursor_blink_mode(vte);
    vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_OFF);

    vte_terminal_set_cursor_position(vte,
                                     clamp(cursor_col + col, 0l, end_col),
                                     clamp(cursor_row + row, first_row(vte), last_row(vte)));

    update_scroll(vte);
    update_selection(vte, select);
    vte_terminal_set_cursor_blink_mode(vte, mode);
}

static void move_to_row_start(VteTerminal *vte, select_info *select, long row) {
    vte_terminal_set_cursor_position(vte, 0, row);
    update_scroll(vte);
    update_selection(vte, select);
}

static void open_selection(char *browser, VteTerminal *vte) {
    if (!vte_terminal_get_has_selection(vte)) {
        g_printerr("no selection to open\n");
        return;
    }

    if (browser) {
        auto selection = make_unique(vte_terminal_get_selection(vte), g_free);
        if (selection && *selection) {
            launch_browser(browser, selection.get());
        }
    } else {
        g_printerr("no browser to open url\n");
    }
}

static std::unique_ptr<char, decltype(&g_free)>
get_text_range(VteTerminal *vte, long start_row, long start_col, long end_row, long end_col) {
    return {vte_terminal_get_text_range(vte, start_row, start_col, end_row, end_col,
                                        nullptr, nullptr, nullptr), g_free};
}

static bool is_word_char(gunichar c) {
    static const char *word_char_ascii_punct = "-,./?%&#_=+@~";
    return g_unichar_isgraph(c) &&
           (g_unichar_isalnum(c) || (g_unichar_ispunct(c) &&
                                     (c >= 0x80 || strchr(word_char_ascii_punct, (int)c) != NULL)));
}

template<typename F>
static void move_backward(VteTerminal *vte, select_info *select, F is_word) {
    long cursor_col, cursor_row;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    auto content = get_text_range(vte, cursor_row, 0, cursor_row, cursor_col);

    if (!content) {
        return;
    }

    long length;
    gunichar *codepoints = g_utf8_to_ucs4(content.get(), -1, nullptr, &length, nullptr);

    if (!codepoints) {
        return;
    }

    bool in_word = false;

    for (long i = length - 2; i > 0; i--) {
        cursor_col--;
        if (!is_word(codepoints[i - 1])) {
            if (in_word) {
                break;
            }
        } else {
            in_word = true;
        }
    }
    vte_terminal_set_cursor_position(vte, cursor_col, cursor_row);
    update_selection(vte, select);

    g_free(codepoints);
}

static void move_backward_word(VteTerminal *vte, select_info *select) {
    move_backward(vte, select, is_word_char);
}

static void move_backward_blank_word(VteTerminal *vte, select_info *select) {
    move_backward(vte, select, std::not1(std::ref(g_unichar_isspace)));
}

template<typename F>
void move_first(VteTerminal *vte, select_info *select, F is_match) {
    long cursor_col, cursor_row;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    const long end_col = vte_terminal_get_column_count(vte) - 1;

    auto content = get_text_range(vte, cursor_row, cursor_col, cursor_row, end_col);

    if (!content) {
        return;
    }

    long length;
    gunichar *codepoints = g_utf8_to_ucs4(content.get(), -1, nullptr, &length, nullptr);

    if (!codepoints) {
        return;
    }

    auto iter = std::find_if(codepoints, codepoints + length, is_match);
    if (iter != codepoints + length) {
        vte_terminal_set_cursor_position(vte, iter - codepoints, cursor_row);
        update_selection(vte, select);
    }

    g_free(codepoints);
}

static void set_cursor_column(VteTerminal *vte, const select_info *select, long column) {
    long cursor_row;
    vte_terminal_get_cursor_position(vte, nullptr, &cursor_row);
    vte_terminal_set_cursor_position(vte, column, cursor_row);
    update_selection(vte, select);
}

static void move_to_eol(VteTerminal *vte, select_info *select) {
    long cursor_row;
    vte_terminal_get_cursor_position(vte, nullptr, &cursor_row);

    const long end_col = vte_terminal_get_column_count(vte) - 1;

    auto content = get_text_range(vte, cursor_row, 0, cursor_row, end_col);

    if (!content) {
        return;
    }

    long length;
    gunichar *codepoints = g_utf8_to_ucs4(content.get(), -1, nullptr, &length, nullptr);

    if (!codepoints) {
        return;
    }

    auto iter = std::find(codepoints, codepoints + length, '\n');
    set_cursor_column(vte, select, std::max(iter - codepoints - 1l, 0l));

    g_free(codepoints);
}

template<typename F>
static void move_forward(VteTerminal *vte, select_info *select, F is_word, bool goto_word_end) {
    long cursor_col, cursor_row;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    const long end_col = vte_terminal_get_column_count(vte) - 1;

    auto content = get_text_range(vte, cursor_row, cursor_col, cursor_row, end_col);

    if (!content) {
        return;
    }

    long length;
    gunichar *codepoints = g_utf8_to_ucs4(content.get(), -1, nullptr, &length, nullptr);

    if (!codepoints) {
        return;
    }

    // prevent going past the end (get_text_range adds a \n)
    if (codepoints[length - 1] == '\n') {
        length--;
    }

    bool end_of_word = false;

    if (!goto_word_end) {
        for (long i = 1; i < length; i++) {
            if (is_word(codepoints[i - 1])) {
                if (end_of_word) {
                    break;
                }
            } else {
                end_of_word = true;
            }
            cursor_col++;
        }
    } else {
        for (long i = 2; i <= length; i++) {
            cursor_col++;
            if (is_word(codepoints[i - 1]) && !is_word(codepoints[i])) {
                break;
            }
        }
    }
    vte_terminal_set_cursor_position(vte, cursor_col, cursor_row);
    update_selection(vte, select);

    g_free(codepoints);
}

static void move_forward_end_word(VteTerminal *vte, select_info *select) {
    move_forward(vte, select, is_word_char, true);
}

static void move_forward_end_blank_word(VteTerminal *vte, select_info *select) {
    move_forward(vte, select, std::not1(std::ref(g_unichar_isspace)), true);
}

static void move_forward_word(VteTerminal *vte, select_info *select) {
    move_forward(vte, select, is_word_char, false);
}

static void move_forward_blank_word(VteTerminal *vte, select_info *select) {
    move_forward(vte, select, std::not1(std::ref(g_unichar_isspace)), false);
}

/* {{{ CALLBACKS */
void window_title_cb(VteTerminal *vte, gboolean *dynamic_title) {
    const char *const title = *dynamic_title ? vte_terminal_get_window_title(vte) : nullptr;
    gtk_window_set_title(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(vte))),
                         title ? title : "termite");
}

static void reset_font_scale(VteTerminal *vte, gdouble scale) {
    vte_terminal_set_font_scale(vte, scale);
}

static void increase_font_scale(VteTerminal *vte) {
    gdouble scale = vte_terminal_get_font_scale(vte);

    for (auto it = zoom_factors.begin(); it != zoom_factors.end(); ++it) {
        if ((*it - scale) > 1e-6) {
            vte_terminal_set_font_scale(vte, *it);
            return;
        }
    }
}

static void decrease_font_scale(VteTerminal *vte) {
    gdouble scale = vte_terminal_get_font_scale(vte);

    for (auto it = zoom_factors.rbegin(); it != zoom_factors.rend(); ++it) {
        if ((scale - *it) > 1e-6) {
            vte_terminal_set_font_scale(vte, *it);
            return;
        }
    }
}

gboolean window_state_cb(GtkWindow *, GdkEventWindowState *event, keybind_info *info) {
    if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
        info->fullscreen_toggle = gtk_window_unfullscreen;
    else
        info->fullscreen_toggle = gtk_window_fullscreen;
    return FALSE;
}

gboolean key_press_cb(VteTerminal *vte, GdkEventKey *event, keybind_info *info) {
    const guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();

    if (info->config.fullscreen && event->keyval == GDK_KEY_F11 && !modifiers) {
        info->fullscreen_toggle(info->window);
        return TRUE;
    }

    if (info->select.mode != vi_mode::insert) {
        if (modifiers == GDK_CONTROL_MASK) {
            switch (gdk_keyval_to_lower(event->keyval)) {
                case GDK_KEY_bracketleft:
                    exit_command_mode(vte, &info->select);
                    gtk_widget_hide(info->panel.da);
                    gtk_widget_hide(info->panel.entry);
                    info->panel.url_list.clear();
                    break;
                case GDK_KEY_v:
                    toggle_visual(vte, &info->select, vi_mode::visual_block);
                    break;
                case GDK_KEY_Left:
                    move_backward_blank_word(vte, &info->select);
                    break;
                case GDK_KEY_Right:
                    move_forward_blank_word(vte, &info->select);
                    break;
                case GDK_KEY_u:
                    move(vte, &info->select, 0, -(vte_terminal_get_row_count(vte) / 2));
                    break;
                case GDK_KEY_d:
                    move(vte, &info->select, 0, vte_terminal_get_row_count(vte) / 2);
                    break;
                case GDK_KEY_b:
                    move(vte, &info->select, 0, -(vte_terminal_get_row_count(vte) - 1));
                    break;
                case GDK_KEY_f:
                    move(vte, &info->select, 0, vte_terminal_get_row_count(vte) - 1);
                    break;
            }
            return TRUE;
        }
        if (modifiers == GDK_SHIFT_MASK) {
            switch (event->keyval) {
                case GDK_KEY_Left:
                    move_backward_word(vte, &info->select);
                    return TRUE;
                case GDK_KEY_Right:
                    move_forward_word(vte, &info->select);
                    return TRUE;
            }
        }
        switch (event->keyval) {
            case GDK_KEY_Escape:
            case GDK_KEY_q:
                exit_command_mode(vte, &info->select);
                gtk_widget_hide(info->panel.da);
                gtk_widget_hide(info->panel.entry);
                info->panel.url_list.clear();
                break;
            case GDK_KEY_Left:
            case GDK_KEY_h:
                move(vte, &info->select, -1, 0);
                break;
            case GDK_KEY_Down:
            case GDK_KEY_j:
                move(vte, &info->select, 0, 1);
                break;
            case GDK_KEY_Up:
            case GDK_KEY_k:
                move(vte, &info->select, 0, -1);
                break;
            case GDK_KEY_Right:
            case GDK_KEY_l:
                move(vte, &info->select, 1, 0);
                break;
            case GDK_KEY_b:
                move_backward_word(vte, &info->select);
                break;
            case GDK_KEY_B:
                move_backward_blank_word(vte, &info->select);
                break;
            case GDK_KEY_w:
                move_forward_word(vte, &info->select);
                break;
            case GDK_KEY_W:
                move_forward_blank_word(vte, &info->select);
                break;
            case GDK_KEY_e:
                move_forward_end_word(vte, &info->select);
                break;
            case GDK_KEY_E:
                move_forward_end_blank_word(vte, &info->select);
                break;
            case GDK_KEY_0:
            case GDK_KEY_Home:
                set_cursor_column(vte, &info->select, 0);
                break;
            case GDK_KEY_asciicircum:
                set_cursor_column(vte, &info->select, 0);
                move_first(vte, &info->select, std::not1(std::ref(g_unichar_isspace)));
                break;
            case GDK_KEY_dollar:
            case GDK_KEY_End:
                move_to_eol(vte, &info->select);
                break;
            case GDK_KEY_g:
                move_to_row_start(vte, &info->select, first_row(vte));
                break;
            case GDK_KEY_G:
                move_to_row_start(vte, &info->select, last_row(vte));
                break;
            case GDK_KEY_H:
                move_to_row_start(vte, &info->select, top_row(vte));
                break;
            case GDK_KEY_M:
                move_to_row_start(vte, &info->select, middle_row(vte));
                break;
            case GDK_KEY_L:
                move_to_row_start(vte, &info->select, bottom_row(vte));
                break;
            case GDK_KEY_v:
                toggle_visual(vte, &info->select, vi_mode::visual);
                break;
            case GDK_KEY_V:
                toggle_visual(vte, &info->select, vi_mode::visual_line);
                break;
            case GDK_KEY_y:
#if VTE_CHECK_VERSION(0, 50, 0)
                vte_terminal_copy_clipboard_format(vte, VTE_FORMAT_TEXT);
#else
                vte_terminal_copy_clipboard(vte);
#endif
                break;
            case GDK_KEY_slash:
                overlay_show(&info->panel, overlay_mode::search, vte);
                break;
            case GDK_KEY_question:
                overlay_show(&info->panel, overlay_mode::rsearch, vte);
                break;
            case GDK_KEY_n:
                vte_terminal_search_find_next(vte);
                vte_terminal_copy_primary(vte);
                break;
            case GDK_KEY_N:
                vte_terminal_search_find_previous(vte);
                vte_terminal_copy_primary(vte);
                break;
            case GDK_KEY_u:
                search(vte, url_regex, false);
                break;
            case GDK_KEY_U:
                search(vte, url_regex, true);
                break;
            case GDK_KEY_o:
                open_selection(info->config.browser, vte);
                break;
            case GDK_KEY_Return:
                open_selection(info->config.browser, vte);
                exit_command_mode(vte, &info->select);
                break;
            case GDK_KEY_x:
                if (!info->config.browser)
                    break;
                find_urls(vte, &info->panel);
                gtk_widget_show(info->panel.da);
                overlay_show(&info->panel, overlay_mode::urlselect, nullptr);
                break;
        }
        return TRUE;
    }
    if (modifiers == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
        switch (gdk_keyval_to_lower(event->keyval)) {
            case GDK_KEY_plus:
                increase_font_scale(vte);
                return TRUE;
            case GDK_KEY_equal:
                reset_font_scale(vte, info->config.font_scale);
                return TRUE;
            case GDK_KEY_t:
                launch_in_directory(vte);
                return TRUE;
            case GDK_KEY_space:
            case GDK_KEY_nobreakspace: // shift-space on some keyboard layouts
                enter_command_mode(vte, &info->select);
                return TRUE;
            case GDK_KEY_x:
                enter_command_mode(vte, &info->select);
                find_urls(vte, &info->panel);
                gtk_widget_show(info->panel.da);
                overlay_show(&info->panel, overlay_mode::urlselect, nullptr);
                exit_command_mode(vte, &info->select);
                return TRUE;
            case GDK_KEY_c:
#if VTE_CHECK_VERSION(0, 50, 0)
                vte_terminal_copy_clipboard_format(vte, VTE_FORMAT_TEXT);
#else
                vte_terminal_copy_clipboard(vte);
#endif
                return TRUE;
            case GDK_KEY_v:
                vte_terminal_paste_clipboard(vte);
                return TRUE;
            case GDK_KEY_r:
                reload_config();
                return TRUE;
            case GDK_KEY_l:
                vte_terminal_reset(vte, TRUE, TRUE);
                return TRUE;
            default:
                if (modify_key_feed(event, info, modify_table))
                    return TRUE;
        }
    } else if ((modifiers == (GDK_CONTROL_MASK|GDK_MOD1_MASK)) ||
               (modifiers == (GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK))) {
        if (modify_key_feed(event, info, modify_meta_table))
            return TRUE;
    } else if (modifiers == GDK_CONTROL_MASK) {
        switch (gdk_keyval_to_lower(event->keyval)) {
            case GDK_KEY_Tab:
                overlay_show(&info->panel, overlay_mode::completion, vte);
                return TRUE;
            case GDK_KEY_plus:
            case GDK_KEY_KP_Add:
                increase_font_scale(vte);
                return TRUE;
            case GDK_KEY_minus:
            case GDK_KEY_KP_Subtract:
                decrease_font_scale(vte);
                return TRUE;
            case GDK_KEY_equal:
                reset_font_scale(vte, info->config.font_scale);
                return TRUE;
            default:
                if (modify_key_feed(event, info, modify_table))
                    return TRUE;
        }
    }
    return FALSE;
}

static void synthesize_keypress(GtkWidget *widget, unsigned keyval) {
    GdkEvent new_event;

    new_event.key.type = GDK_KEY_PRESS;
    new_event.key.window = gtk_widget_get_parent_window(widget);
    new_event.key.send_event = TRUE;
    new_event.key.time = GDK_CURRENT_TIME;
    new_event.key.keyval = keyval;
    new_event.key.state = GDK_KEY_PRESS_MASK;
    new_event.key.length = 0;
    new_event.key.string = nullptr;
    new_event.key.hardware_keycode = 0;
    new_event.key.group = 0;

    gdk_event_put(&new_event);
}

gboolean entry_key_press_cb(GtkEntry *entry, GdkEventKey *event, keybind_info *info) {
    const guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    gboolean ret = FALSE;

    if (modifiers == GDK_CONTROL_MASK) {
        switch (event->keyval) {
            case GDK_KEY_bracketleft:
                ret = TRUE;
                break;
        }
    }
    switch (event->keyval) {
        case GDK_KEY_BackSpace:
            if (info->panel.mode == overlay_mode::urlselect && info->panel.fulltext) {
                size_t slen = strlen(info->panel.fulltext);
                if (info->panel.fulltext != nullptr && slen > 0)
                    info->panel.fulltext[slen-1] = '\0';
                gtk_widget_queue_draw(info->panel.da);
            }
            break;
        case GDK_KEY_0:
        case GDK_KEY_1:
        case GDK_KEY_2:
        case GDK_KEY_3:
        case GDK_KEY_4:
        case GDK_KEY_5:
        case GDK_KEY_6:
        case GDK_KEY_7:
        case GDK_KEY_8:
        case GDK_KEY_9:
            if (info->panel.mode == overlay_mode::urlselect) {
                const char *const text = gtk_entry_get_text(entry);
                size_t len = strlen(text);
                free(info->panel.fulltext);
                info->panel.fulltext = g_strndup(text, len + 1);
                info->panel.fulltext[len] = (char)event->keyval;
                size_t urld = static_cast<size_t>(info->panel.url_list.size());
                size_t textd = strtoul(info->panel.fulltext, nullptr, 10);
                size_t url_dig = static_cast<size_t>(
                    log10(static_cast<double>(info->panel.url_list.size())) + 1);
                size_t text_dig = static_cast<size_t>(
                    log10(static_cast<double>(textd)) + 1);

                if (url_dig == text_dig ||
                    textd > static_cast<size_t>(static_cast<double>(urld)/10)) {
                    launch_url(info->config.browser, info->panel.fulltext, &info->panel);
                    ret = TRUE;
                } else {
                    gtk_widget_queue_draw(info->panel.da);
                }
            }
            break;
        case GDK_KEY_Tab:
            synthesize_keypress(GTK_WIDGET(entry), GDK_KEY_Down);
            return TRUE;
        case GDK_KEY_ISO_Left_Tab:
            synthesize_keypress(GTK_WIDGET(entry), GDK_KEY_Up);
            return TRUE;
        case GDK_KEY_Down:
            // this stops the down key from leaving the GtkEntry...
            event->hardware_keycode = 0;
            break;
        case GDK_KEY_Escape:
            ret = TRUE;
            break;
        case GDK_KEY_Return: {
            const char *const text = gtk_entry_get_text(entry);

            switch (info->panel.mode) {
                case overlay_mode::search:
                    search(info->vte, text, false);
                    break;
                case overlay_mode::rsearch:
                    search(info->vte, text, true);
                    break;
                case overlay_mode::completion:
                    vte_terminal_feed_child(info->vte, text, -1);
                    break;
                case overlay_mode::urlselect:
                    launch_url(info->config.browser, text, &info->panel);
                    break;
                case overlay_mode::hidden:
                    break;
            }
            ret = TRUE;
         }
    }

    if (ret) {
        if (info->panel.mode == overlay_mode::urlselect) {
            gtk_widget_hide(info->panel.da);
            info->panel.url_list.clear();
            free(info->panel.fulltext);
            info->panel.fulltext = nullptr;
        }
        info->panel.mode = overlay_mode::hidden;
        gtk_widget_hide(info->panel.entry);
        gtk_widget_grab_focus(GTK_WIDGET(info->vte));
    }
    return ret;
}

gboolean position_overlay_cb(GtkBin *overlay, GtkWidget *widget, GdkRectangle *alloc) {
    GtkWidget *vte = gtk_bin_get_child(overlay);

    const int width  = gtk_widget_get_allocated_width(vte);
    const int height = gtk_widget_get_allocated_height(vte);

    GtkRequisition req;
    gtk_widget_get_preferred_size(widget, nullptr, &req);

    alloc->x = width - req.width - 40;
    alloc->y = 0;
    alloc->width  = std::min(width, req.width);
    alloc->height = std::min(height, req.height);

    return TRUE;
}

gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event, const config_info *info) {
    if (info->clickable_url && event->type == GDK_BUTTON_PRESS) {
#if VTE_CHECK_VERSION (0, 49, 1)
        auto match = make_unique(vte_terminal_hyperlink_check_event(vte, (GdkEvent*)event), g_free);
        if (!match) {
            match = make_unique(check_match(vte, event), g_free);
        }
#else
        auto match = make_unique(check_match(vte, event), g_free);
#endif
        if (!match)
            return FALSE;

        if (event->button == 1) {
            launch_browser(info->browser, match.get());
        } else if (event->button == 3) {
            GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            gtk_clipboard_set_text(clipboard, match.get(), -1);
        }

        return TRUE;
    }
    return FALSE;
}

static void bell_cb(GtkWidget *vte, gboolean *urgent_on_bell) {
    if (*urgent_on_bell) {
        gtk_window_set_urgency_hint(GTK_WINDOW(gtk_widget_get_toplevel(vte)), TRUE);
    }
}

gboolean focus_cb(GtkWindow *window) {
    gtk_window_set_urgency_hint(window, FALSE);
    return FALSE;
}
/* }}} */

GtkTreeModel *create_completion_model(VteTerminal *vte) {
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);

    long end_row, end_col;
    vte_terminal_get_cursor_position(vte, &end_col, &end_row);
    auto content = get_text_range(vte, 0, 0, end_row, end_col);

    if (!content) {
        g_printerr("no content returned for completion\n");
        return GTK_TREE_MODEL(store);
    }

    auto less = [](const char *a, const char *b) { return strcmp(a, b) < 0; };
    std::set<const char *, decltype(less)> tokens(less);

    for (char *s_ptr = content.get(), *saveptr; ; s_ptr = nullptr) {
        const char *token = strtok_r(s_ptr, " \n\t", &saveptr);
        if (!token) {
            break;
        }
        tokens.insert(token);
    }

    for (const char *token : tokens) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, token, -1);
    }

    return GTK_TREE_MODEL(store);
}

void search(VteTerminal *vte, const char *pattern, bool reverse) {
    auto terminal_search = reverse ? vte_terminal_search_find_previous : vte_terminal_search_find_next;

    VteRegex *regex = vte_terminal_search_get_regex(vte);
    if (regex) vte_regex_unref(regex);
    vte_terminal_search_set_regex(vte,
                    vte_regex_new_for_search(pattern,
                                    (gssize) strlen(pattern),
                                    PCRE2_MULTILINE | PCRE2_CASELESS,
                                    nullptr), 0);

    if (!terminal_search(vte)) {
        vte_terminal_unselect_all(vte);
        terminal_search(vte);
    }

    vte_terminal_copy_primary(vte);
}

void overlay_show(search_panel_info *info, overlay_mode mode, VteTerminal *vte) {
    if (vte) {
        GtkEntryCompletion *completion = gtk_entry_completion_new();
        gtk_entry_set_completion(GTK_ENTRY(info->entry), completion);
        g_object_unref(completion);

        GtkTreeModel *completion_model = create_completion_model(vte);
        gtk_entry_completion_set_model(completion, completion_model);
        g_object_unref(completion_model);

        gtk_entry_completion_set_inline_selection(completion, TRUE);
        gtk_entry_completion_set_text_column(completion, 0);
    }

    gtk_entry_set_text(GTK_ENTRY(info->entry), "");

    info->mode = mode;
    gtk_widget_show(info->entry);
    gtk_widget_grab_focus(info->entry);
}

void get_vte_padding(VteTerminal *vte, int *left, int *top, int *right, int *bottom) {
    GtkBorder border;
    gtk_style_context_get_padding(gtk_widget_get_style_context(GTK_WIDGET(vte)),
                                  gtk_widget_get_state_flags(GTK_WIDGET(vte)),
                                  &border);
    *left = border.left;
    *right = border.right;
    *top = border.top;
    *bottom = border.bottom;
}

char *check_match(VteTerminal *vte, GdkEventButton *event) {
    int tag;

    return vte_terminal_match_check_event(vte, (GdkEvent*) event, &tag);
}

/* {{{ CONFIG LOADING */
template<typename T>
maybe<T> get_config(T (*get)(GKeyFile *, const char *, const char *, GError **),
                    GKeyFile *config, const char *group, const char *key) {
    GError *error = nullptr;
    maybe<T> value = get(config, group, key, &error);
    if (error) {
        g_error_free(error);
        return {};
    }
    return value;
}

auto get_config_integer(std::bind(get_config<int>, g_key_file_get_integer,
                                  _1, _2, _3));
auto get_config_string(std::bind(get_config<char *>, g_key_file_get_string,
                                 _1, _2, _3));
auto get_config_double(std::bind(get_config<double>, g_key_file_get_double,
                                 _1, _2, _3));

static maybe<GdkRGBA> get_config_color(GKeyFile *config, const char *section, const char *key) {
    if (auto s = get_config_string(config, section, key)) {
        GdkRGBA color;
        if (gdk_rgba_parse(&color, *s)) {
            g_free(*s);
            return color;
        }
        g_printerr("invalid color string: %s\n", *s);
        g_free(*s);
    }
    return {};
}

static maybe<cairo_pattern_t *>
get_config_cairo_color(GKeyFile *config, const char *group, const char *key) {
    if (auto color = get_config_color(config, group, key)) {
        return cairo_pattern_create_rgba(color->red,
                                         color->green,
                                         color->blue,
                                         color->alpha);
    }
    return {};
}

static void load_theme(GtkWindow *window, VteTerminal *vte, GKeyFile *config, hint_info &hints) {
    std::array<GdkRGBA, 256> palette;
    char color_key[] = "color000";

    for (unsigned i = 0; i < palette.size(); i++) {
        snprintf(color_key, sizeof(color_key), "color%u", i);
        if (auto color = get_config_color(config, "colors", color_key)) {
            palette[i] = *color;
        } else if (i < 16) {
            palette[i].blue = (((i & 4) ? 0xc000 : 0) + (i > 7 ? 0x3fff: 0)) / 65535.0;
            palette[i].green = (((i & 2) ? 0xc000 : 0) + (i > 7 ? 0x3fff : 0)) / 65535.0;
            palette[i].red = (((i & 1) ? 0xc000 : 0) + (i > 7 ? 0x3fff : 0)) / 65535.0;
            palette[i].alpha = 0;
        } else if (i < 232) {
            const unsigned j = i - 16;
            const unsigned r = j / 36, g = (j / 6) % 6, b = j % 6;
            const unsigned red =   (r == 0) ? 0 : r * 40 + 55;
            const unsigned green = (g == 0) ? 0 : g * 40 + 55;
            const unsigned blue =  (b == 0) ? 0 : b * 40 + 55;
            palette[i].red   = (red | red << 8) / 65535.0;
            palette[i].green = (green | green << 8) / 65535.0;
            palette[i].blue  = (blue | blue << 8) / 65535.0;
            palette[i].alpha = 0;
        } else if (i < 256) {
            const unsigned shade = 8 + (i - 232) * 10;
            palette[i].red = palette[i].green = palette[i].blue = (shade | shade << 8) / 65535.0;
            palette[i].alpha = 0;
        }
    }

    vte_terminal_set_colors(vte, nullptr, nullptr, palette.data(), palette.size());
    if (auto color = get_config_color(config, "colors", "foreground")) {
        vte_terminal_set_color_foreground(vte, &*color);
        vte_terminal_set_color_bold(vte, &*color);
    }
    if (auto color = get_config_color(config, "colors", "foreground_bold")) {
        vte_terminal_set_color_bold(vte, &*color);
    }
    if (auto color = get_config_color(config, "colors", "background")) {
        vte_terminal_set_color_background(vte, &*color);
        override_background_color(GTK_WIDGET(window), &*color);
    }
    if (auto color = get_config_color(config, "colors", "cursor")) {
        vte_terminal_set_color_cursor(vte, &*color);
    }
    if (auto color = get_config_color(config, "colors", "cursor_foreground")) {
        vte_terminal_set_color_cursor_foreground(vte, &*color);
    }
    if (auto color = get_config_color(config, "colors", "highlight")) {
        vte_terminal_set_color_highlight(vte, &*color);
    }

    if (auto s = get_config_string(config, "hints", "font")) {
        hints.font = pango_font_description_from_string(*s);
        g_free(*s);
    }

    hints.fg = get_config_cairo_color(config, "hints", "foreground").get_value_or(cairo_pattern_create_rgb(1, 1, 1));
    hints.bg = get_config_cairo_color(config, "hints", "background").get_value_or(cairo_pattern_create_rgb(0, 0, 0));
    hints.af = get_config_cairo_color(config, "hints", "active_foreground").get_value_or(cairo_pattern_create_rgb(0.9, 0.5, 0.5));
    hints.ab = get_config_cairo_color(config, "hints", "active_background").get_value_or(cairo_pattern_create_rgb(0, 0, 0));
    hints.border = get_config_cairo_color(config, "hints", "border").get_value_or(hints.fg);
    hints.padding = get_config_double(config, "hints", "padding", 5).get_value_or(2.0);
    hints.border_width = get_config_double(config, "hints", "border_width").get_value_or(1.0);
    hints.roundness = get_config_double(config, "hints", "roundness").get_value_or(1.5);
}

static void load_config(GtkWindow *window, VteTerminal *vte, GtkWidget *scrollbar,
                        GtkWidget *hbox, config_info *info, char **icon,
                        bool *show_scrollbar) {
    const std::string default_path = "/termite/config";
    GKeyFile *config = g_key_file_new();
    GError *error = nullptr;

    gboolean loaded = FALSE;

    if (info->config_file) {
        loaded = g_key_file_load_from_file(config,
                                           info->config_file,
                                           G_KEY_FILE_NONE, &error);
        if (!loaded)
            g_printerr("%s parsing failed: %s\n", info->config_file,
                       error->message);
    }

    if (!loaded) {
        loaded = g_key_file_load_from_file(config,
                                           (g_get_user_config_dir() + default_path).c_str(),
                                           G_KEY_FILE_NONE, &error);
        if (!loaded)
            g_printerr("%s parsing failed: %s\n", (g_get_user_config_dir() + default_path).c_str(),
                       error->message);
    }

    for (const char *const *dir = g_get_system_config_dirs();
         !loaded && *dir; dir++) {
        loaded = g_key_file_load_from_file(config, (*dir + default_path).c_str(),
                                           G_KEY_FILE_NONE, &error);
        if (!loaded)
            g_printerr("%s parsing failed: %s\n", (*dir + default_path).c_str(),
                       error->message);
    }

    if (loaded) {
        set_config(window, vte, scrollbar, hbox, info, icon, show_scrollbar, config);
    }
    g_key_file_free(config);
}

static void set_config(GtkWindow *window, VteTerminal *vte, GtkWidget *scrollbar, GtkWidget *hbox,
                       config_info *info, char **icon, bool *show_scrollbar_ptr,
                       GKeyFile *config) {

    auto cfg_bool = [config](const char *key, gboolean value) {
        return get_config<gboolean>(g_key_file_get_boolean,
                                    config, "options", key).get_value_or(value);
    };

    vte_terminal_set_scroll_on_output(vte, cfg_bool("scroll_on_output", FALSE));
    vte_terminal_set_scroll_on_keystroke(vte, cfg_bool("scroll_on_keystroke", TRUE));
    vte_terminal_set_audible_bell(vte, cfg_bool("audible_bell", FALSE));
    vte_terminal_set_mouse_autohide(vte, cfg_bool("mouse_autohide", FALSE));
    vte_terminal_set_allow_bold(vte, cfg_bool("allow_bold", TRUE));
    vte_terminal_search_set_wrap_around(vte, cfg_bool("search_wrap", TRUE));
#if VTE_CHECK_VERSION (0, 49, 1)
    vte_terminal_set_allow_hyperlink(vte, cfg_bool("hyperlinks", FALSE));
#endif
#if VTE_CHECK_VERSION (0, 51, 2)
    vte_terminal_set_bold_is_bright(vte, cfg_bool("bold_is_bright", TRUE));
    vte_terminal_set_cell_height_scale(vte, get_config_double(config, "options", "cell_height_scale").get_value_or(1.0));
    vte_terminal_set_cell_width_scale(vte, get_config_double(config, "options", "cell_width_scale").get_value_or(1.0));
#endif
    info->dynamic_title = cfg_bool("dynamic_title", TRUE);
    info->urgent_on_bell = cfg_bool("urgent_on_bell", TRUE);
    info->clickable_url = cfg_bool("clickable_url", TRUE);
    info->size_hints = cfg_bool("size_hints", FALSE);
    info->filter_unmatched_urls = cfg_bool("filter_unmatched_urls", TRUE);
    info->modify_other_keys = cfg_bool("modify_other_keys", FALSE);
    info->fullscreen = cfg_bool("fullscreen", TRUE);
    info->font_scale = vte_terminal_get_font_scale(vte);

    g_free(info->browser);
    info->browser = nullptr;

    if (auto s = get_config_string(config, "options", "browser")) {
        info->browser = *s;
    } else {
        info->browser = g_strdup(g_getenv("BROWSER"));
    }

    if (!info->browser) {
        info->browser = g_strdup("xdg-open");
    }

    if (info->clickable_url) {
        info->tag = vte_terminal_match_add_regex(vte,
                vte_regex_new_for_match(url_regex,
                                        (gssize) strlen(url_regex),
                                        PCRE2_MULTILINE | PCRE2_NOTEMPTY,
                                        nullptr),
                0);
        vte_terminal_match_set_cursor_name(vte, info->tag, "hand");
    } else if (info->tag != -1) {
        vte_terminal_match_remove(vte, info->tag);
        info->tag = -1;
    }

    if (auto s = get_config_string(config, "options", "font")) {
        PangoFontDescription *font = pango_font_description_from_string(*s);
        vte_terminal_set_font(vte, font);
        pango_font_description_free(font);
        g_free(*s);
    }

    if (auto i = get_config_integer(config, "options", "scrollback_lines")) {
        vte_terminal_set_scrollback_lines(vte, *i);
    }

    if (auto s = get_config_string(config, "options", "cursor_blink")) {
        if (!g_ascii_strcasecmp(*s, "system")) {
            vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_SYSTEM);
        } else if (!g_ascii_strcasecmp(*s, "on")) {
            vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_ON);
        } else if (!g_ascii_strcasecmp(*s, "off")) {
            vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_OFF);
        }
        g_free(*s);
    }

    if (auto s = get_config_string(config, "options", "cursor_shape")) {
        if (!g_ascii_strcasecmp(*s, "block")) {
            vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_BLOCK);
        } else if (!g_ascii_strcasecmp(*s, "ibeam")) {
            vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_IBEAM);
        } else if (!g_ascii_strcasecmp(*s, "underline")) {
            vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_UNDERLINE);
        }
        g_free(*s);
    }

    if (icon) {
        if (auto s = get_config_string(config, "options", "icon_name")) {
            *icon = *s;
        }
    }

    if (info->size_hints) {
        set_size_hints(GTK_WINDOW(window), vte);
    }

    bool show_scrollbar = false;
    if (auto s = get_config_string(config, "options", "scrollbar")) {
        // "off" is implicitly handled by default
        if (!g_ascii_strcasecmp(*s, "left")) {
            show_scrollbar = true;
            gtk_box_reorder_child(GTK_BOX(hbox), scrollbar, 0);
        } else if (!g_ascii_strcasecmp(*s, "right")) {
            show_scrollbar = true;
            gtk_box_reorder_child(GTK_BOX(hbox), scrollbar, -1);
        }
        g_free(*s);
    }
    if (show_scrollbar) {
        gtk_widget_show(scrollbar);
    } else {
        gtk_widget_hide(scrollbar);
    }
    if (show_scrollbar_ptr != nullptr) {
        *show_scrollbar_ptr = show_scrollbar;
    }

    load_theme(window, vte, config, info->hints);
}/*}}}*/

static void exit_with_status(VteTerminal *, int status) {
    gtk_main_quit();
    exit(WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
}

static void exit_with_success(VteTerminal *) {
    gtk_main_quit();
    exit(EXIT_SUCCESS);
}

static char *get_user_shell_with_fallback() {
    if (const char *env = g_getenv("SHELL") ) {
        if (!((env != NULL) && (env[0] == '\0')))
            return g_strdup(env);
    }

    if (char *command = vte_get_user_shell()) {
        if (!((command != NULL) && (command[0] == '\0')))
           return command;
    }

    return g_strdup("/bin/sh");
}

static void on_alpha_screen_changed(GtkWindow *window, GdkScreen *, void *) {
    GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);

    if (!visual)
        visual = gdk_screen_get_system_visual(screen);

    gtk_widget_set_visual(GTK_WIDGET(window), visual);
}

int main(int argc, char **argv) {
    GError *error = nullptr;
    const char *const term = "xterm-termite";
    char *directory = nullptr;
    gboolean version = FALSE, hold = FALSE;

    GOptionContext *context = g_option_context_new(nullptr);
    char *role = nullptr, *execute = nullptr, *config_file = nullptr;
    char *title = nullptr, *icon = nullptr;
    bool show_scrollbar = false;
    const GOptionEntry entries[] = {
        {"version", 'v', 0, G_OPTION_ARG_NONE, &version, "Version info", nullptr},
        {"exec", 'e', 0, G_OPTION_ARG_STRING, &execute, "Command to execute", "COMMAND"},
        {"role", 'r', 0, G_OPTION_ARG_STRING, &role, "The role to use", "ROLE"},
        {"title", 't', 0, G_OPTION_ARG_STRING, &title, "Window title", "TITLE"},
        {"directory", 'd', 0, G_OPTION_ARG_STRING, &directory, "Change to directory", "DIRECTORY"},
        {"hold", 0, 0, G_OPTION_ARG_NONE, &hold, "Remain open after child process exits", nullptr},
        {"config", 'c', 0, G_OPTION_ARG_STRING, &config_file, "Path of config file", "CONFIG"},
        {"icon", 'i', 0, G_OPTION_ARG_STRING, &icon, "Icon", "ICON"},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}
    };
    g_option_context_add_main_entries(context, entries, nullptr);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("option parsing failed: %s\n", error->message);
        g_clear_error (&error);
        return EXIT_FAILURE;
    }

    g_option_context_free(context);

    if (version) {
        g_print("termite %s\n", TERMITE_VERSION);
        return EXIT_SUCCESS;
    }

    if (directory) {
        if (chdir(directory) == -1) {
            perror("chdir");
            return EXIT_FAILURE;
        }
        g_free(directory);
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    GtkWidget *panel_overlay = gtk_overlay_new();
    GtkWidget *hint_overlay = gtk_overlay_new();

    GtkWidget *vte_widget = vte_terminal_new();
    VteTerminal *vte = VTE_TERMINAL(vte_widget);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(hbox),"termite");
    GtkWidget *scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte_widget)));
    gtk_box_pack_start(GTK_BOX(hbox), hint_overlay, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, FALSE, 0);

    if (role) {
        gtk_window_set_role(GTK_WINDOW(window), role);
        g_free(role);
    }

    char **command_argv;
    char *default_argv[2] = {nullptr, nullptr};

    if (execute) {
        int argcp;
        char **argvp;
        g_shell_parse_argv(execute, &argcp, &argvp, &error);
        if (error) {
            g_printerr("failed to parse command: %s\n", error->message);
            return EXIT_FAILURE;
        }
        command_argv = argvp;
    } else {
        default_argv[0] = get_user_shell_with_fallback();
        command_argv = default_argv;
    }

    keybind_info info {
        GTK_WINDOW(window), vte,
        {gtk_entry_new(),
         gtk_drawing_area_new(),
         overlay_mode::hidden,
         std::vector<url_data>(),
         nullptr},
        {vi_mode::insert, 0, 0, 0, 0},
        {{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0},
         nullptr, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, -1, config_file, 0},
        gtk_window_fullscreen
    };

    load_config(GTK_WINDOW(window), vte, scrollbar, hbox, &info.config,
                icon ? nullptr : &icon, &show_scrollbar);

    reload_config = [&]{
        load_config(GTK_WINDOW(window), vte, scrollbar, hbox, &info.config,
                    nullptr, nullptr);
    };
    signal(SIGUSR1, [](int){ reload_config(); });

    GdkRGBA transparent {0, 0, 0, 0};

    override_background_color(hint_overlay, &transparent);
    override_background_color(info.panel.da, &transparent);

    gtk_widget_set_halign(info.panel.da, GTK_ALIGN_FILL);
    gtk_widget_set_valign(info.panel.da, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(hint_overlay), info.panel.da);

    gtk_widget_set_margin_start(info.panel.entry, 5);
    gtk_widget_set_margin_end(info.panel.entry, 5);
    gtk_widget_set_margin_top(info.panel.entry, 5);
    gtk_widget_set_margin_bottom(info.panel.entry, 5);
    gtk_overlay_add_overlay(GTK_OVERLAY(panel_overlay), info.panel.entry);

    gtk_widget_set_halign(info.panel.entry, GTK_ALIGN_START);
    gtk_widget_set_valign(info.panel.entry, GTK_ALIGN_END);

    gtk_container_add(GTK_CONTAINER(panel_overlay), hbox);
    gtk_container_add(GTK_CONTAINER(hint_overlay), vte_widget);
    gtk_container_add(GTK_CONTAINER(window), panel_overlay);

    if (!hold) {
        g_signal_connect(vte, "child-exited", G_CALLBACK(exit_with_status), nullptr);
    }
    g_signal_connect(window, "destroy", G_CALLBACK(exit_with_success), nullptr);
    g_signal_connect(vte, "key-press-event", G_CALLBACK(key_press_cb), &info);
    g_signal_connect(info.panel.entry, "key-press-event", G_CALLBACK(entry_key_press_cb), &info);
    g_signal_connect(panel_overlay, "get-child-position", G_CALLBACK(position_overlay_cb), nullptr);
    g_signal_connect(vte, "button-press-event", G_CALLBACK(button_press_cb), &info.config);
    g_signal_connect(vte, "bell", G_CALLBACK(bell_cb), &info.config.urgent_on_bell);
    draw_cb_info draw_cb_info{vte, &info.panel, &info.config.hints, info.config.filter_unmatched_urls};
    g_signal_connect_swapped(info.panel.da, "draw", G_CALLBACK(draw_cb), &draw_cb_info);

    g_signal_connect(window, "focus-in-event",  G_CALLBACK(focus_cb), nullptr);
    g_signal_connect(window, "focus-out-event", G_CALLBACK(focus_cb), nullptr);

    on_alpha_screen_changed(GTK_WINDOW(window), nullptr, nullptr);
    g_signal_connect(window, "screen-changed", G_CALLBACK(on_alpha_screen_changed), nullptr);

    if (info.config.fullscreen) {
        g_signal_connect(window, "window-state-event", G_CALLBACK(window_state_cb), &info);
    }

    if (title) {
        info.config.dynamic_title = FALSE;
        gtk_window_set_title(GTK_WINDOW(window), title);
        g_free(title);
    } else {
        g_signal_connect(vte, "window-title-changed", G_CALLBACK(window_title_cb),
                         &info.config.dynamic_title);
        if (execute) {
            gtk_window_set_title(GTK_WINDOW(window), execute);
        } else {
            window_title_cb(vte, &info.config.dynamic_title);
        }
    }

    if (icon) {
        gtk_window_set_icon_name(GTK_WINDOW(window), icon);
        g_free(icon);
    }

    gtk_widget_grab_focus(vte_widget);
    gtk_widget_show_all(window);
    gtk_widget_hide(info.panel.entry);
    gtk_widget_hide(info.panel.da);
    if (!show_scrollbar) {
        gtk_widget_hide(scrollbar);
    }

    char **env = g_get_environ();

#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_SCREEN(gtk_widget_get_screen(window))) {
        GdkWindow *gdk_window = gtk_widget_get_window(window);
        if (!gdk_window) {
            g_printerr("no window\n");
            return EXIT_FAILURE;
        }
        char xid_s[std::numeric_limits<long unsigned>::digits10 + 1];
        snprintf(xid_s, sizeof(xid_s), "%lu", GDK_WINDOW_XID(gdk_window));
        env = g_environ_setenv(env, "WINDOWID", xid_s, TRUE);
    }
#endif

    env = g_environ_setenv(env, "TERM", term, TRUE);

    GPid child_pid;
    if (vte_terminal_spawn_sync(vte, VTE_PTY_DEFAULT, nullptr, command_argv, env,
                                G_SPAWN_SEARCH_PATH, nullptr, nullptr, &child_pid, nullptr,
                                &error)) {
        vte_terminal_watch_child(vte, child_pid);
    } else {
        g_printerr("the command failed to run: %s\n", error->message);
        return EXIT_FAILURE;
    }

    int width, height, padding_left, padding_top, padding_right, padding_bottom;
    const long char_width = vte_terminal_get_char_width(vte);
    const long char_height = vte_terminal_get_char_height(vte);

    gtk_window_get_size(GTK_WINDOW(window), &width, &height);
    get_vte_padding(vte, &padding_left, &padding_top, &padding_right, &padding_bottom);
    vte_terminal_set_size(vte,
                          (width - padding_left - padding_right) / char_width,
                          (height - padding_top - padding_bottom) / char_height);

    g_strfreev(env);

    gtk_main();
    return EXIT_FAILURE; // child process did not cause termination
}

// vim: et:sts=4:sw=4:cino=(0:cc=100
