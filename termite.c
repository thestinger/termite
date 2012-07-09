#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <string.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#ifndef __GNUC__
# define __attribute__(x)
#endif

#define CSI "\x1b["

static const char * const url_regex = "(ftp|http)s?://[-a-zA-Z0-9.?$%&/=_~#.,:;+()]*";

typedef enum overlay_mode {
    OVERLAY_HIDDEN = 0,
    OVERLAY_SEARCH,
    OVERLAY_RSEARCH,
    OVERLAY_COMPLETION
} overlay_mode;

typedef enum select_mode {
    SELECT_OFF = 0,
    SELECT_ON,
    SELECT_VISUAL,
    SELECT_VISUAL_LINE,
    SELECT_VISUAL_BLOCK
} select_mode;

typedef struct select_info {
    select_mode mode;
    long begin_col;
    long begin_row;
    long cursor_col;
    long cursor_row;
} select_info;

typedef struct search_panel_info {
    GtkWidget *vte;
    GtkWidget *entry;
    GtkWidget *panel;
    enum overlay_mode mode;
    select_info select;
} search_panel_info;

static char *browser_cmd[3] = {NULL};

static void launch_browser(char *url);

static void window_title_cb(VteTerminal *vte, GtkWindow *window);
static gboolean key_press_cb(VteTerminal *vte, GdkEventKey *event, search_panel_info *info);
static gboolean entry_key_press_cb(GtkEntry *entry, GdkEventKey *event, search_panel_info *info);
static gboolean position_overlay_cb(GtkBin *overlay, GtkWidget *widget, GdkRectangle *alloc);
static gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event);
static void beep_cb(GtkWindow *window);
static gboolean focus_cb(GtkWindow *window);

static gboolean add_to_list_store(char *key, void *value, GtkListStore *store);
static GtkTreeModel *create_completion_model(VteTerminal *vte);
static void search(VteTerminal *vte, const char *pattern, bool reverse);
static void overlay_show(search_panel_info *info, overlay_mode mode, bool complete);
static void get_vte_padding(VteTerminal *vte, int *w, int *h);
static char *check_match(VteTerminal *vte, int event_x, int event_y);
static void load_config(GtkWindow *window, VteTerminal *vte,
                        gboolean *dynamic_title, gboolean *urgent_on_bell,
                        gboolean *clickable_url, const char **term);

void launch_browser(char *url) {
    browser_cmd[1] = url;
    g_spawn_async(NULL, browser_cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

/* {{{ CALLBACKS */
void window_title_cb(VteTerminal *vte, GtkWindow *window) {
    const char * const t = vte_terminal_get_window_title(vte);
    gtk_window_set_title(window, t ? t : "termite");
}

static void update_selection(VteTerminal *vte, const select_info *select) {
    if (select->mode == SELECT_ON) {
        return; // not in visual mode
    }

    vte_terminal_select_none(vte);
    vte_terminal_set_selection_block_mode(vte, select->mode == SELECT_VISUAL_BLOCK);

    const long n_columns = vte_terminal_get_column_count(vte);

    if (select->mode == SELECT_VISUAL) {
        const long begin = select->begin_row * n_columns + select->begin_col;
        const long end = select->cursor_row * n_columns + select->cursor_col;
        if (begin < end) {
            vte_terminal_select_text(vte, select->begin_col, select->begin_row,
                                     select->cursor_col, select->cursor_row, 0, 0);
        } else {
            vte_terminal_select_text(vte, select->cursor_col, select->cursor_row,
                                     select->begin_col, select->begin_row, 0, 0);
        }
    } else if (select->mode == SELECT_VISUAL_LINE) {
        vte_terminal_select_text(vte, 0,
                                 MIN(select->begin_row, select->cursor_row),
                                 n_columns - 1,
                                 MAX(select->begin_row, select->cursor_row),
                                 0, 0);
    } else if (select->mode == SELECT_VISUAL_BLOCK) {
        vte_terminal_select_text(vte,
                                 MIN(select->begin_col, select->cursor_col),
                                 MIN(select->begin_row, select->cursor_row),
                                 MAX(select->begin_col, select->cursor_col),
                                 MAX(select->begin_row, select->cursor_row),
                                 0, 0);
    }

    vte_terminal_copy_primary(vte);
}

static void feed_str(VteTerminal *vte, const char *s) {
    vte_terminal_feed(vte, s, (long)strlen(s));
}

static void start_selection(VteTerminal *vte, select_info *select) {
    feed_str(vte, CSI "?25l"); // hide cursor
    select->mode = SELECT_ON;
    vte_terminal_get_cursor_position(vte, &select->cursor_col, &select->cursor_row);
}

static void end_selection(VteTerminal *vte, select_info *select) {
    feed_str(vte, CSI "?25h"); // show cursor
    vte_terminal_select_none(vte);
    select->mode = SELECT_OFF;
}

static void toggle_visual(VteTerminal *vte, select_info *select, select_mode mode) {
    if (select->mode == mode) {
        select->mode = SELECT_ON;
        vte_terminal_select_none(vte);
    } else {
        if (select->mode == SELECT_ON) {
            select->begin_col = select->cursor_col;
            select->begin_row = select->cursor_row;
        }
        select->mode = mode;
        update_selection(vte, select);
    }
}

static long first_row(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    double scroll_lower = gtk_adjustment_get_lower(adjust);
    return (long)scroll_lower;
}

static long last_row(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    double scroll_upper = gtk_adjustment_get_upper(adjust);
    return (long)scroll_upper - 1;
}

static void move(VteTerminal *vte, select_info *select, long col, long row) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    double scroll_row = gtk_adjustment_get_value(adjust);
    long n_rows = vte_terminal_get_row_count(vte);

    const long end_col = vte_terminal_get_column_count(vte) - 1;

    select->cursor_col = CLAMP(select->cursor_col + col, 0, end_col);
    select->cursor_row = CLAMP(select->cursor_row + row, first_row(vte), last_row(vte));

    if (select->cursor_row < scroll_row) {
        gtk_adjustment_set_value(adjust, (double)select->cursor_row);
    } else if (select->cursor_row - n_rows >= (long)scroll_row) {
        gtk_adjustment_set_value(adjust, (double)(select->cursor_row - n_rows + 1));
    }
    update_selection(vte, select);
}

gboolean key_press_cb(VteTerminal *vte, GdkEventKey *event, search_panel_info *info) {
    const guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    gboolean dynamic_title = FALSE, urgent_on_bell = FALSE, clickable_url = FALSE;
    if (info->select.mode) {
        if (modifiers == GDK_CONTROL_MASK) {
            if (gdk_keyval_to_lower(event->keyval) == GDK_KEY_v) {
                toggle_visual(vte, &info->select, SELECT_VISUAL_BLOCK);
            }
            return TRUE;
        }
        switch (event->keyval) {
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
            case GDK_KEY_asciicircum:
                info->select.cursor_col = 0;
                update_selection(vte, &info->select);
                break;
            case GDK_KEY_dollar:
                info->select.cursor_col = vte_terminal_get_column_count(vte) - 1;
                update_selection(vte, &info->select);
                break;
            case GDK_KEY_v:
                toggle_visual(vte, &info->select, SELECT_VISUAL);
                break;
            case GDK_KEY_V:
                toggle_visual(vte, &info->select, SELECT_VISUAL_LINE);
                break;
            case GDK_KEY_Escape:
                end_selection(vte, &info->select);
                break;
        }
        return TRUE;
    }
    if (modifiers == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
        switch (gdk_keyval_to_lower(event->keyval)) {
            case GDK_KEY_space:
                start_selection(vte, &info->select);
                return TRUE;
            case GDK_KEY_c:
                vte_terminal_copy_clipboard(vte);
                return TRUE;
            case GDK_KEY_v:
                vte_terminal_paste_clipboard(vte);
                return TRUE;
            case GDK_KEY_p:
                vte_terminal_search_find_previous(vte);
                vte_terminal_copy_primary(vte);
                return TRUE;
            case GDK_KEY_n:
                vte_terminal_search_find_next(vte);
                vte_terminal_copy_primary(vte);
                return TRUE;
            case GDK_KEY_f:
                overlay_show(info, OVERLAY_SEARCH, true);
                return TRUE;
            case GDK_KEY_r:
                overlay_show(info, OVERLAY_RSEARCH, true);
                return TRUE;
            case GDK_KEY_j:
                search(vte, url_regex, false);
                return TRUE;
            case GDK_KEY_k:
                search(vte, url_regex, true);
                return TRUE;
            case GDK_KEY_Escape:
                load_config(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(vte))),
                            vte, &dynamic_title, &urgent_on_bell,
                            &clickable_url, NULL);
                return TRUE;
        }
    } else if (modifiers == GDK_CONTROL_MASK && event->keyval == GDK_KEY_Tab) {
        overlay_show(info, OVERLAY_COMPLETION, true);
        return TRUE;
    }
    return FALSE;
}

gboolean entry_key_press_cb(GtkEntry *entry, GdkEventKey *event, search_panel_info *info) {
    gboolean ret = FALSE;

    if (event->keyval == GDK_KEY_Escape) {
        ret = TRUE;
    } else if (event->keyval == GDK_KEY_Return) {
        const char *text = gtk_entry_get_text(entry);

        switch (info->mode) {
            case OVERLAY_SEARCH:
                search(VTE_TERMINAL(info->vte), text, false);
                break;
            case OVERLAY_RSEARCH:
                search(VTE_TERMINAL(info->vte), text, true);
                break;
            case OVERLAY_COMPLETION:
                vte_terminal_feed_child(VTE_TERMINAL(info->vte), text, -1);
                break;
            case OVERLAY_HIDDEN:
                break;
        }
        ret = TRUE;
    }

    if (ret) {
        info->mode = OVERLAY_HIDDEN;
        gtk_widget_hide(info->panel);
        gtk_widget_grab_focus(info->vte);
    }
    return ret;
}

gboolean position_overlay_cb(GtkBin *overlay, GtkWidget *widget, GdkRectangle *alloc) {
    GtkWidget *vte = gtk_bin_get_child(overlay);

    const int width  = gtk_widget_get_allocated_width(vte);
    const int height = gtk_widget_get_allocated_height(vte);

    GtkRequisition req;
    gtk_widget_get_preferred_size(widget, NULL, &req);

    alloc->x = width - req.width - 40;
    alloc->y = 0;
    alloc->width  = MIN(width, req.width);
    alloc->height = MIN(height, req.height);

    return TRUE;
}

gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event) {
    char *match = check_match(vte, (int)event->x, (int)event->y);
    if (event->button == 1 && event->type == GDK_BUTTON_PRESS && match) {
        launch_browser(match);
        g_free(match);
        return TRUE;
    }
    return FALSE;
}

void beep_cb(GtkWindow *window) {
    gtk_window_set_urgency_hint(window, TRUE);
}

gboolean focus_cb(GtkWindow *window) {
    gtk_window_set_urgency_hint(window, FALSE);
    return FALSE;
}
/* }}} */

gboolean add_to_list_store(char *key,
                           __attribute__((unused)) void *value,
                           GtkListStore *store) {
    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, key, -1);
    return FALSE;
}

GtkTreeModel *create_completion_model(VteTerminal *vte) {
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);

    long end_row, end_col;
    vte_terminal_get_cursor_position(vte, &end_col, &end_row);
    char *content = vte_terminal_get_text_range(vte, 0, 0, end_row, end_col,
                                                NULL, NULL, NULL);

    if (!content) {
        g_printerr("no content returned for completion\n");
        return GTK_TREE_MODEL(store);
    }

    char *s_ptr = content, *saveptr;

    GTree *tree = g_tree_new((GCompareFunc)strcmp);

    for (; ; s_ptr = NULL) {
        char *token = strtok_r(s_ptr, " \n\t", &saveptr);
        if (!token) {
            break;
        }
        g_tree_insert(tree, token, NULL);
    }

    g_tree_foreach(tree, (GTraverseFunc)add_to_list_store, store);
    g_tree_destroy(tree);
    g_free(content);
    return GTK_TREE_MODEL(store);
}

void search(VteTerminal *vte, const char *pattern, bool reverse) {
    GRegex *regex = vte_terminal_search_get_gregex(vte);
    if (regex) g_regex_unref(regex);
    regex = g_regex_new(pattern, (GRegexCompileFlags)0, (GRegexMatchFlags)0, NULL);
    vte_terminal_search_set_gregex(vte, regex);

    if (!reverse) {
        vte_terminal_search_find_next(vte);
    } else {
        vte_terminal_search_find_previous(vte);
    }
    vte_terminal_copy_primary(vte);
}

void overlay_show(search_panel_info *info, overlay_mode mode, bool complete) {
    if (complete) {
        GtkEntryCompletion *completion = gtk_entry_completion_new();
        gtk_entry_set_completion(GTK_ENTRY(info->entry), completion);
        g_object_unref(completion);

        GtkTreeModel *completion_model = create_completion_model(VTE_TERMINAL(info->vte));
        gtk_entry_completion_set_model(completion, completion_model);
        g_object_unref(completion_model);

        gtk_entry_completion_set_inline_selection(completion, TRUE);
        gtk_entry_completion_set_text_column(completion, 0);
    }

    gtk_entry_set_text(GTK_ENTRY(info->entry), "");

    info->mode = mode;
    gtk_widget_show(info->panel);
    gtk_widget_grab_focus(info->entry);
}

void get_vte_padding(VteTerminal *vte, int *w, int *h) {
    GtkBorder *border = NULL;
    gtk_widget_style_get(GTK_WIDGET(vte), "inner-border", &border, NULL);
    if (!border) {
        g_warning("VTE's inner-border property unavailable");
        *w = *h = 0;
    } else {
        *w = border->left + border->right;
        *h = border->top + border->bottom;
        gtk_border_free(border);
    }
}

char *check_match(VteTerminal *vte, int event_x, int event_y) {
    int xpad, ypad, tag;
    get_vte_padding(vte, &xpad, &ypad);
    return vte_terminal_match_check(vte,
                                    (event_x - ypad) / vte_terminal_get_char_width(vte),
                                    (event_y - ypad) / vte_terminal_get_char_height(vte),
                                    &tag);
}

/* {{{ CONFIG LOADING */
#define MAKE_GET_CONFIG_FUNCTION(NAME, TYPE) \
static bool get_config_ ## NAME (GKeyFile *config, const char *group, const char *key, TYPE *value) { \
    GError *error = NULL; \
    *value = g_key_file_get_ ## NAME (config, group, key, &error); \
    if (error) { \
        g_error_free(error); \
        return false; \
    } \
    return true; \
}

MAKE_GET_CONFIG_FUNCTION(boolean, gboolean)
MAKE_GET_CONFIG_FUNCTION(integer, int)
MAKE_GET_CONFIG_FUNCTION(string, char *)
MAKE_GET_CONFIG_FUNCTION(double, double)

static bool get_config_color(GKeyFile *config, const char *key, GdkColor *color) {
    char *cfgstr;
    bool success = false;
    if (get_config_string(config, "colors", key, &cfgstr)) {
        if (gdk_color_parse(cfgstr, color)) {
            success = true;
        } else {
            g_printerr("invalid color string: %s\n", cfgstr);
        }
        g_free(cfgstr);
    }
    return success;
}

static void load_config(GtkWindow *window, VteTerminal *vte,
                        gboolean *dynamic_title, gboolean *urgent_on_bell,
                        gboolean *clickable_url, const char **term) {

    static const char * const filename = "termite.cfg";
    const char *dir = g_get_user_config_dir();
    char *path = g_build_filename(dir, filename, NULL);
    GKeyFile *config = g_key_file_new();

    if ((g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, NULL) ||
         g_key_file_load_from_dirs(config, filename, (const char **)g_get_system_config_dirs(),
                                   NULL, G_KEY_FILE_NONE, NULL))) {
        gboolean cfgbool;
        double cfgdouble;
        int cfgint;
        char *cfgstr;

        if (term && get_config_string(config, "options", "term", &cfgstr)) {
            *term = cfgstr;
        }
        if (get_config_boolean(config, "options", "resize_grip", &cfgbool)) {
            gtk_window_set_has_resize_grip(window, cfgbool);
        }
        if (get_config_boolean(config, "options", "scroll_on_output", &cfgbool)) {
            vte_terminal_set_scroll_on_output(vte, cfgbool);
        }
        if (get_config_boolean(config, "options", "scroll_on_keystroke", &cfgbool)) {
            vte_terminal_set_scroll_on_keystroke(vte, cfgbool);
        }
        if (get_config_boolean(config, "options", "audible_bell", &cfgbool)) {
            vte_terminal_set_audible_bell(vte, cfgbool);
        }
        if (get_config_boolean(config, "options", "visible_bell", &cfgbool)) {
            vte_terminal_set_visible_bell(vte, cfgbool);
        }
        if (get_config_boolean(config, "options", "mouse_autohide", &cfgbool)) {
            vte_terminal_set_mouse_autohide(vte, cfgbool);
        }
        if (get_config_boolean(config, "options", "allow_bold", &cfgbool)) {
            vte_terminal_set_allow_bold(vte, cfgbool);
        }
        if (get_config_boolean(config, "options", "dynamic_title", &cfgbool)) {
            *dynamic_title = cfgbool;
        }
        if (get_config_boolean(config, "options", "urgent_on_bell", &cfgbool)) {
            *urgent_on_bell = cfgbool;
        }
        if (get_config_boolean(config, "options", "clickable_url", &cfgbool)) {
            *clickable_url = cfgbool;
        }
        if (get_config_boolean(config, "options", "search_wrap", &cfgbool)) {
            vte_terminal_search_set_wrap_around(vte, cfgbool);
        }

        g_free(browser_cmd[0]);
        if (get_config_string(config, "options", "browser", &cfgstr)) {
            browser_cmd[0] = cfgstr;
        } else {
            browser_cmd[0] = g_strdup(g_getenv("BROWSER"));
            if (!browser_cmd[0]) *clickable_url = false;
        }

        if (get_config_string(config, "options", "font", &cfgstr)) {
            vte_terminal_set_font_from_string(vte, cfgstr);
            g_free(cfgstr);
        }

        if (get_config_integer(config, "options", "scrollback_lines", &cfgint)) {
            vte_terminal_set_scrollback_lines(vte, cfgint);
        }

        if (get_config_string(config, "options", "cursor_blink", &cfgstr)) {
            if (!g_ascii_strcasecmp(cfgstr, "system")) {
                vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_SYSTEM);
            } else if (!g_ascii_strcasecmp(cfgstr, "on")) {
                vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_ON);
            } else if (!g_ascii_strcasecmp(cfgstr, "off")) {
                vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_OFF);
            }
            g_free(cfgstr);
        }

        if (get_config_string(config, "options", "cursor_shape", &cfgstr)) {
            if (!g_ascii_strcasecmp(cfgstr, "block")) {
                vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_BLOCK);
            } else if (!g_ascii_strcasecmp(cfgstr, "ibeam")) {
                vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_IBEAM);
            } else if (!g_ascii_strcasecmp(cfgstr, "underline")) {
                vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_UNDERLINE);
            }
            g_free(cfgstr);
        }

        if (get_config_string(config, "options", "icon_name", &cfgstr)) {
            gtk_window_set_icon_name(window, cfgstr);
            g_free(cfgstr);
        }

        if (get_config_double(config, "options", "transparency", &cfgdouble)) {
            vte_terminal_set_background_saturation(vte, cfgdouble);
            vte_terminal_set_opacity(vte, (guint16)(0xffff * (1 - cfgdouble)));
        }

        static const long palette_size = 255;
        GdkColor color, palette[palette_size];

        char color_key[] = "color000";

        bool success = true;
        for (unsigned i = 0; success && i < palette_size; i++) {
            snprintf(color_key, sizeof color_key, "color%u", i);
            if (get_config_string(config, "colors", color_key, &cfgstr)) {
                if (!gdk_color_parse(cfgstr, &palette[i])) {
                    g_printerr("invalid color string: %s\n", cfgstr);
                    success = false;
                }
            } else {
                if (i < 16) {
                    palette[i].blue = (i & 4) ? 0xc000 : 0;
                    palette[i].green = (i & 2) ? 0xc000 : 0;
                    palette[i].red = (i & 1) ? 0xc000 : 0;
                    if (i > 7) {
                        palette[i].blue = (guint16)(palette[i].blue + 0x3fff);
                        palette[i].green = (guint16)(palette[i].green + 0x3fff);
                        palette[i].red = (guint16)(palette[i].red + 0x3fff);
                    }
                } else if (i < 232) {
                    const unsigned j = i - 16;
                    const unsigned r = j / 36, g = (j / 6) % 6, b = j % 6;
                    const unsigned red =   (r == 0) ? 0 : r * 40 + 55;
                    const unsigned green = (g == 0) ? 0 : g * 40 + 55;
                    const unsigned blue =  (b == 0) ? 0 : b * 40 + 55;
                    palette[i].red   = (guint16)(red | red << 8);
                    palette[i].green = (guint16)(green | green << 8);
                    palette[i].blue  = (guint16)(blue | blue << 8);
                } else if (i < 256) {
                    const unsigned shade = 8 + (i - 232) * 10;
                    palette[i].red = palette[i].green = palette[i].blue = (guint16)(shade | shade << 8);
                }
            }
        }

        if (success) {
            vte_terminal_set_colors(vte, NULL, NULL, palette, palette_size);
        }

        if (get_config_color(config, "foreground", &color)) {
            vte_terminal_set_color_foreground(vte, &color);
        }

        if (get_config_color(config, "foreground_bold", &color)) {
            vte_terminal_set_color_bold(vte, &color);
        }

        if (get_config_color(config, "foreground_dim", &color)) {
            vte_terminal_set_color_dim(vte, &color);
        }

        if (get_config_color(config, "background", &color)) {
            vte_terminal_set_color_background(vte, &color);
        }

        if (get_config_color(config, "cursor", &color)) {
            vte_terminal_set_color_cursor(vte, &color);
        }
    }
    g_free(path);
    g_key_file_free(config);
}/*}}}*/

int main(int argc, char **argv) {
    GError *error = NULL;
    const char *term = "termite";
    gboolean dynamic_title = FALSE, urgent_on_bell = FALSE, clickable_url = FALSE;
    gboolean version = FALSE;

    GOptionContext *context = g_option_context_new(NULL);
    char *role = NULL, *geometry = NULL, *execute = NULL;
    const GOptionEntry entries[] = {
        {"role", 'r', 0, G_OPTION_ARG_STRING, &role, "The role to use", "ROLE"},
        {"geometry", 0, 0, G_OPTION_ARG_STRING, &geometry, "Window geometry", "GEOMETRY"},
        {"exec", 'e', 0, G_OPTION_ARG_STRING, &execute, "Command to execute", "COMMAND"},
        {"version", 'v', 0, G_OPTION_ARG_NONE, &version, "Version info", NULL},
        {NULL}
    };
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("option parsing failed: %s\n", error->message);
        return 1;
    }

    if (version) {
        g_print("termite %s\n", TERMITE_VERSION);
        return 0;
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *vte = vte_terminal_new();

    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (!visual) {
        visual = gdk_screen_get_system_visual(screen);
    }
    gtk_widget_set_visual(window, visual);

    if (role) {
        gtk_window_set_role(GTK_WINDOW(window), role);
        g_free(role);
    }

    char **command_argv;
    char fallback[] = "/bin/sh";
    char *default_argv[2] = {fallback, NULL};

    if (execute) {
        int argcp;
        char **argvp;
        g_shell_parse_argv(execute, &argcp, &argvp, &error);
        if (error) {
            g_printerr("Failed to parse command: %s\n", error->message);
            return 1;
        }
        command_argv = argvp;
    } else {
        char *shell = vte_get_user_shell();
        if (shell) default_argv[0] = shell;
        command_argv = default_argv;
    }

    VtePty *pty = vte_terminal_pty_new(VTE_TERMINAL(vte), VTE_PTY_DEFAULT, &error);

    if (!pty) {
        g_printerr("Failed to create pty: %s\n", error->message);
        return 1;
    }

    load_config(GTK_WINDOW(window), VTE_TERMINAL(vte), &dynamic_title,
                &urgent_on_bell, &clickable_url, &term);

    vte_terminal_set_pty_object(VTE_TERMINAL(vte), pty);
    vte_pty_set_term(pty, term);

    GtkWidget *alignment = gtk_alignment_new(0, 0, 1, 1);
    gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 5, 5, 5, 5);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), alignment);

    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_halign(entry, GTK_ALIGN_START);
    gtk_widget_set_valign(entry, GTK_ALIGN_END);

    gtk_container_add(GTK_CONTAINER(alignment), entry);
    gtk_container_add(GTK_CONTAINER(overlay), vte);
    gtk_container_add(GTK_CONTAINER(window), overlay);

    select_info select = {SELECT_OFF, 0, 0, 0, 0};
    search_panel_info info = {vte, entry, alignment, OVERLAY_HIDDEN, select};

    g_signal_connect(window,  "destroy",            G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte,     "child-exited",       G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte,     "key-press-event",    G_CALLBACK(key_press_cb), &info);
    g_signal_connect(entry,   "key-press-event",    G_CALLBACK(entry_key_press_cb), &info);
    g_signal_connect(overlay, "get-child-position", G_CALLBACK(position_overlay_cb), NULL);

    if (clickable_url) {
        int tag = vte_terminal_match_add_gregex(VTE_TERMINAL(vte),
                                                g_regex_new(url_regex,
                                                            G_REGEX_CASELESS,
                                                            G_REGEX_MATCH_NOTEMPTY,
                                                            NULL),
                                                (GRegexMatchFlags)0);
        vte_terminal_match_set_cursor_type(VTE_TERMINAL(vte), tag, GDK_HAND2);
        g_signal_connect(vte, "button-press-event", G_CALLBACK(button_press_cb), NULL);
    }

    if (urgent_on_bell) {
        g_signal_connect_swapped(vte, "beep", G_CALLBACK(beep_cb), window);
        g_signal_connect(window, "focus-in-event",  G_CALLBACK(focus_cb), NULL);
        g_signal_connect(window, "focus-out-event", G_CALLBACK(focus_cb), NULL);
    }

    if (dynamic_title) {
        window_title_cb(VTE_TERMINAL(vte), GTK_WINDOW(window));
        g_signal_connect(vte, "window-title-changed", G_CALLBACK(window_title_cb), window);
    }

    if (geometry) {
        gtk_widget_show_all(overlay);
        gtk_widget_show_all(alignment);
        if (!gtk_window_parse_geometry(GTK_WINDOW(window), geometry)) {
            g_printerr("Invalid geometry string: %s\n", geometry);
        }
        g_free(geometry);
    }

    gtk_widget_grab_focus(vte);
    gtk_widget_show_all(window);
    gtk_widget_hide(alignment);

    GdkWindow *gdk_window = gtk_widget_get_window(window);
    if (!gdk_window) {
        g_printerr("no window");
        return 1;
    }
    char *xid_s = g_strdup_printf("%lu", GDK_WINDOW_XID(gdk_window));
    char **env = g_get_environ();
    env = g_environ_setenv(env, "WINDOWID", xid_s, TRUE);
    env = g_environ_setenv(env, "TERM", term, TRUE);
    g_free(xid_s);

    GPid ppid;
    if (g_spawn_async(NULL, command_argv, env,
                      (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH),
                      (GSpawnChildSetupFunc)vte_pty_child_setup, pty,
                      &ppid, &error)) {
        vte_terminal_watch_child(VTE_TERMINAL(vte), ppid);
    } else {
        g_printerr("The new terminal's command failed to run: %s\n", error->message);
        return 1;
    }

    g_strfreev(env);

    gtk_main();
    return vte_terminal_get_child_exit_status(VTE_TERMINAL(vte));
}

// vim: et:sts=4:sw=4:cino=(0
