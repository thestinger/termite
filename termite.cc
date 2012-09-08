#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <vte/vteaccess.h>

#include "url_regex.h"

enum class overlay_mode {
    hidden,
    search,
    rsearch,
    completion
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

struct search_panel_info {
    VteTerminal *vte;
    GtkWidget *entry;
    GtkWidget *panel;
    overlay_mode mode;
};

struct config_info {
    gboolean dynamic_title, urgent_on_bell, clickable_url;
    int tag;
};

struct keybind_info {
    search_panel_info panel;
    select_info select;
    config_info config;
};

static char *browser_cmd[3] = {NULL};

static void launch_browser(char *url);

static void window_title_cb(VteTerminal *vte, gboolean *dynamic_title);
static gboolean key_press_cb(VteTerminal *vte, GdkEventKey *event, keybind_info *info);
static gboolean entry_key_press_cb(GtkEntry *entry, GdkEventKey *event, search_panel_info *info);
static gboolean position_overlay_cb(GtkBin *overlay, GtkWidget *widget, GdkRectangle *alloc);
static gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event, gboolean *clickable_url);
static void beep_cb(GtkWidget *vte, gboolean *urgent_on_bell);
static gboolean focus_cb(GtkWindow *window);

static gboolean add_to_list_store(char *key, void *, GtkListStore *store);
static GtkTreeModel *create_completion_model(VteTerminal *vte);
static void search(VteTerminal *vte, const char *pattern, bool reverse);
static void overlay_show(search_panel_info *info, overlay_mode mode, bool complete);
static void get_vte_padding(VteTerminal *vte, int *w, int *h);
static char *check_match(VteTerminal *vte, int event_x, int event_y);
static void load_config(GtkWindow *window, VteTerminal *vte, config_info *info,
                        const char **term, char **geometry);

void launch_browser(char *url) {
    browser_cmd[1] = url;
    g_spawn_async(NULL, browser_cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

static void update_selection(VteTerminal *vte, const select_info *select) {
    vte_terminal_select_none(vte);

    if (select->mode == vi_mode::command) {
        return;
    }

    const long n_columns = vte_terminal_get_column_count(vte);
    long cursor_col, cursor_row;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    vte_terminal_set_selection_block_mode(vte, select->mode == vi_mode::visual_block);

    if (select->mode == vi_mode::visual) {
        const long begin = select->begin_row * n_columns + select->begin_col;
        const long end = cursor_row * n_columns + cursor_col;
        if (begin < end) {
            vte_terminal_select_text(vte, select->begin_col, select->begin_row,
                                     cursor_col, cursor_row);
        } else {
            vte_terminal_select_text(vte, cursor_col, cursor_row,
                                     select->begin_col, select->begin_row);
        }
    } else if (select->mode == vi_mode::visual_line) {
        vte_terminal_select_text(vte, 0,
                                 std::min(select->begin_row, cursor_row),
                                 n_columns - 1,
                                 std::max(select->begin_row, cursor_row));
    } else if (select->mode == vi_mode::visual_block) {
        vte_terminal_select_text(vte,
                                 std::min(select->begin_col, cursor_col),
                                 std::min(select->begin_row, cursor_row),
                                 std::max(select->begin_col, cursor_col),
                                 std::max(select->begin_row, cursor_row));
    }

    vte_terminal_copy_primary(vte);
}

static void start_selection(VteTerminal *vte, select_info *select) {
    vte_terminal_disconnect_pty_read(vte);
    select->mode = vi_mode::command;
    vte_terminal_get_cursor_position(vte, &select->origin_col, &select->origin_row);
    update_selection(vte, select);
}

static void end_selection(VteTerminal *vte, select_info *select) {
    vte_terminal_set_cursor_position(vte, select->origin_col, select->origin_row);
    vte_terminal_connect_pty_read(vte);
    vte_terminal_select_none(vte);
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

static void update_scroll(VteTerminal *vte) {
    GtkAdjustment *adjust = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
    const double scroll_row = gtk_adjustment_get_value(adjust);
    const long n_rows = vte_terminal_get_row_count(vte);
    long cursor_row;
    vte_terminal_get_cursor_position(vte, nullptr, &cursor_row);

    if (cursor_row < scroll_row) {
        gtk_adjustment_set_value(adjust, (double)cursor_row);
    } else if (cursor_row - n_rows >= (long)scroll_row) {
        gtk_adjustment_set_value(adjust, (double)(cursor_row - n_rows + 1));
    }
}

static void move(VteTerminal *vte, select_info *select, long col, long row) {
    const long end_col = vte_terminal_get_column_count(vte) - 1;

    long cursor_col, cursor_row;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    vte_terminal_set_cursor_position(vte,
                                     CLAMP(cursor_col + col, 0, end_col),
                                     CLAMP(cursor_row + row, first_row(vte), last_row(vte)));

    update_scroll(vte);
    update_selection(vte, select);
}

static void move_to_row_start(VteTerminal *vte, select_info *select, long row) {
    vte_terminal_set_cursor_position(vte, 0, row);
    update_scroll(vte);
    update_selection(vte, select);
}

static void open_selection(VteTerminal *vte) {
    if (browser_cmd[0]) {
        AtkText *text = ATK_TEXT(vte_terminal_accessible_new(vte));
        char *selection = atk_text_get_selection(text, 0, NULL, NULL);
        if (selection && selection[0]) {
            launch_browser(selection);
        }
        g_free(selection);
    } else {
        g_printerr("no browser to open url");
    }
}

template<typename F>
static void move_backward(VteTerminal *vte, select_info *select, F predicate) {
    long cursor_col, cursor_row;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    char *content = vte_terminal_get_text_range(vte, cursor_row, 0,
                                                cursor_row, cursor_col,
                                                NULL, NULL, NULL);

    if (!content) {
        return;
    }

    glong length;
    gunichar *codepoints = g_utf8_to_ucs4(content, -1, NULL, &length, NULL);

    if (!codepoints) {
        return;
    }

    bool in_word = false;

    for (gunichar *c = codepoints + length - 2; c > codepoints; c--) {
        if (predicate(*(c - 1))) {
            if (in_word) {
                break;
            }
        } else {
            in_word = true;
        }
        cursor_col--;
    }
    vte_terminal_set_cursor_position(vte, cursor_col, cursor_row);
    update_selection(vte, select);

    g_free(codepoints);
    g_free(content);
}

static void move_backward_word(VteTerminal *vte, select_info *select) {
    move_backward(vte, select, [vte](gunichar c) {
        return !vte_terminal_is_word_char(vte, c);
    });
}

static void move_backward_blank_word(VteTerminal *vte, select_info *select) {
    move_backward(vte, select, [](gunichar c) {
        return g_unichar_isspace(c);
    });
}

template<typename F>
static void move_forward(VteTerminal *vte, select_info *select, F predicate) {
    long cursor_col, cursor_row;
    vte_terminal_get_cursor_position(vte, &cursor_col, &cursor_row);

    const long end_col = vte_terminal_get_column_count(vte) - 1;

    char *content = vte_terminal_get_text_range(vte, cursor_row, cursor_col,
                                                cursor_row, end_col,
                                                NULL, NULL, NULL);

    if (!content) {
        return;
    }

    gunichar *codepoints = g_utf8_to_ucs4(content, -1, NULL, NULL, NULL);

    if (!codepoints) {
        return;
    }

    bool end_of_word = false;

    for (gunichar *c = codepoints; *c != 0; c++) {
        if (predicate(*c)) {
            if (end_of_word) {
                break;
            }
        } else {
            end_of_word = true;
        }
        cursor_col++;
    }
    vte_terminal_set_cursor_position(vte, cursor_col, cursor_row);
    update_selection(vte, select);

    g_free(codepoints);
    g_free(content);
}

static void move_forward_word(VteTerminal *vte, select_info *select) {
    move_forward(vte, select, [vte](gunichar c) {
        return vte_terminal_is_word_char(vte, c);
    });
}

static void move_forward_blank_word(VteTerminal *vte, select_info *select) {
    move_forward(vte, select, [](gunichar c) {
        return !g_unichar_isspace(c);
    });
}

static void set_cursor_column(VteTerminal *vte, const select_info *select, long column) {
    long cursor_row;
    vte_terminal_get_cursor_position(vte, nullptr, &cursor_row);
    vte_terminal_set_cursor_position(vte, column, cursor_row);
    update_selection(vte, select);
}

/* {{{ CALLBACKS */
void window_title_cb(VteTerminal *vte, gboolean *dynamic_title) {
    const char * const title = *dynamic_title ? vte_terminal_get_window_title(vte) : NULL;
    gtk_window_set_title(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(vte))),
                         title ? title : "termite");
}

gboolean key_press_cb(VteTerminal *vte, GdkEventKey *event, keybind_info *info) {
    const guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    if (info->select.mode != vi_mode::insert) {
        if (modifiers == GDK_CONTROL_MASK) {
            if (gdk_keyval_to_lower(event->keyval) == GDK_KEY_v) {
                toggle_visual(vte, &info->select, vi_mode::visual_block);
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
            case GDK_KEY_asciicircum:
                set_cursor_column(vte, &info->select, 0);
                break;
            case GDK_KEY_dollar:
                set_cursor_column(vte, &info->select, vte_terminal_get_column_count(vte) - 1);
                break;
            case GDK_KEY_g:
                move_to_row_start(vte, &info->select, first_row(vte));
                break;
            case GDK_KEY_G:
                move_to_row_start(vte, &info->select, last_row(vte));
                break;
            case GDK_KEY_v:
                toggle_visual(vte, &info->select, vi_mode::visual);
                break;
            case GDK_KEY_V:
                toggle_visual(vte, &info->select, vi_mode::visual_line);
                break;
            case GDK_KEY_Escape:
                end_selection(vte, &info->select);
                break;
            case GDK_KEY_y:
                vte_terminal_copy_clipboard(vte);
                break;
            case GDK_KEY_slash:
                overlay_show(&info->panel, overlay_mode::search, true);
                break;
            case GDK_KEY_question:
                overlay_show(&info->panel, overlay_mode::rsearch, true);
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
                open_selection(vte);
                break;
            case GDK_KEY_Return:
                open_selection(vte);
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
            case GDK_KEY_Escape:
                load_config(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(vte))),
                            vte, &info->config, NULL, NULL);
                return TRUE;
        }
    } else if (modifiers == GDK_CONTROL_MASK && event->keyval == GDK_KEY_Tab) {
        overlay_show(&info->panel, overlay_mode::completion, true);
        return TRUE;
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

gboolean entry_key_press_cb(GtkEntry *entry, GdkEventKey *event, search_panel_info *info) {
    gboolean ret = FALSE;

    if (event->keyval == GDK_KEY_Escape) {
        ret = TRUE;
    } else if (event->keyval == GDK_KEY_Return) {
        const char *text = gtk_entry_get_text(entry);

        switch (info->mode) {
            case overlay_mode::search:
                search(info->vte, text, false);
                break;
            case overlay_mode::rsearch:
                search(info->vte, text, true);
                break;
            case overlay_mode::completion:
                vte_terminal_feed_child(info->vte, text, -1);
                break;
            case overlay_mode::hidden:
                break;
        }
        ret = TRUE;
    } else if (info->mode == overlay_mode::completion) {
        if (event->keyval == GDK_KEY_Tab) {
            synthesize_keypress(GTK_WIDGET(entry), GDK_KEY_Down);
            return TRUE;
        } else if (event->keyval == GDK_KEY_ISO_Left_Tab) {
            synthesize_keypress(GTK_WIDGET(entry), GDK_KEY_Up);
            return TRUE;
        } else if (event->keyval == GDK_KEY_Down) {
            // this stops the down key from leaving the GtkEntry...
            event->hardware_keycode = 0;
        }
    }

    if (ret) {
        info->mode = overlay_mode::hidden;
        gtk_widget_hide(info->panel);
        gtk_widget_grab_focus(GTK_WIDGET(info->vte));
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
    alloc->width  = std::min(width, req.width);
    alloc->height = std::min(height, req.height);

    return TRUE;
}

gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event, gboolean *clickable_url) {
    if (*clickable_url) {
        char *match = check_match(vte, (int)event->x, (int)event->y);
        if (event->button == 1 && event->type == GDK_BUTTON_PRESS && match) {
            launch_browser(match);
            g_free(match);
            return TRUE;
        }
    }
    return FALSE;
}

void beep_cb(GtkWidget *vte, gboolean *urgent_on_bell) {
    if (*urgent_on_bell) {
        gtk_window_set_urgency_hint(GTK_WINDOW(gtk_widget_get_toplevel(vte)), TRUE);
    }
}

gboolean focus_cb(GtkWindow *window) {
    gtk_window_set_urgency_hint(window, FALSE);
    return FALSE;
}
/* }}} */

gboolean add_to_list_store(char *key, void *, GtkListStore *store) {
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

        GtkTreeModel *completion_model = create_completion_model(info->vte);
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

static void load_config(GtkWindow *window, VteTerminal *vte, config_info *info,
                        const char **term, char **geometry) {

    const char * const filename = "termite.cfg";
    char *path = g_build_filename(g_get_user_config_dir(), filename, nullptr);
    GKeyFile *config = g_key_file_new();

    if ((g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, NULL) ||
         g_key_file_load_from_dirs(config, filename,
                                   const_cast<const char **>(g_get_system_config_dirs()),
                                   NULL, G_KEY_FILE_NONE, NULL))) {
        gboolean cfgbool;
        double cfgdouble;
        int cfgint;
        char *cfgstr;

        if (geometry && get_config_string(config, "options", "geometry", &cfgstr)) {
            *geometry = cfgstr;
        }
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
            info->dynamic_title = cfgbool;
        }
        if (get_config_boolean(config, "options", "urgent_on_bell", &cfgbool)) {
            info->urgent_on_bell = cfgbool;
        }
        if (get_config_boolean(config, "options", "search_wrap", &cfgbool)) {
            vte_terminal_search_set_wrap_around(vte, cfgbool);
        }
        if (get_config_boolean(config, "options", "clickable_url", &cfgbool)) {
            info->clickable_url = cfgbool;
        }
        if (info->clickable_url) {
            info->tag =
                vte_terminal_match_add_gregex(vte,
                                              g_regex_new(url_regex,
                                                          G_REGEX_CASELESS,
                                                          G_REGEX_MATCH_NOTEMPTY,
                                                          NULL),
                                              (GRegexMatchFlags)0);
            vte_terminal_match_set_cursor_type(vte, info->tag, GDK_HAND2);
        } else if (info->tag != -1) {
            vte_terminal_match_remove(vte, info->tag);
            info->tag = -1;
        }

        g_free(browser_cmd[0]);
        if (get_config_string(config, "options", "browser", &cfgstr)) {
            browser_cmd[0] = cfgstr;
        } else {
            browser_cmd[0] = g_strdup(g_getenv("BROWSER"));
            if (!browser_cmd[0]) info->clickable_url = false;
        }

        if (get_config_string(config, "options", "font", &cfgstr)) {
            vte_terminal_set_font_from_string(vte, cfgstr);
            g_free(cfgstr);
        }

        if (get_config_string(config, "options", "word_chars", &cfgstr)) {
            vte_terminal_set_word_chars(vte, cfgstr);
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

        const long palette_size = 255;
        GdkColor color, palette[palette_size];

        char color_key[] = "color000";

        for (unsigned i = 0; i < palette_size; i++) {
            snprintf(color_key, sizeof color_key, "color%u", i);
            if (!get_config_color(config, color_key, &palette[i])) {
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
        vte_terminal_set_colors(vte, nullptr, nullptr, palette, palette_size);
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
        if (get_config_color(config, "highlight", &color)) {
            vte_terminal_set_color_highlight(vte, &color);
        }
    }
    g_free(path);
    g_key_file_free(config);
}/*}}}*/

static void exit_with_status(VteTerminal *vte) {
    gtk_main_quit();
    exit(vte_terminal_get_child_exit_status(vte));
}

int main(int argc, char **argv) {
    GError *error = NULL;
    const char *term = "xterm-termite";
    gboolean version = FALSE;

    GOptionContext *context = g_option_context_new(NULL);
    char *role = NULL, *geometry = NULL, *execute = NULL;
    const GOptionEntry entries[] = {
        {"role", 'r', 0, G_OPTION_ARG_STRING, &role, "The role to use", "ROLE"},
        {"geometry", 0, 0, G_OPTION_ARG_STRING, &geometry, "Window geometry", "GEOMETRY"},
        {"exec", 'e', 0, G_OPTION_ARG_STRING, &execute, "Command to execute", "COMMAND"},
        {"version", 'v', 0, G_OPTION_ARG_NONE, &version, "Version info", NULL},
        {}
    };
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("option parsing failed: %s\n", error->message);
        return EXIT_FAILURE;
    }

    if (version) {
        g_print("termite %s\n", TERMITE_VERSION);
        return EXIT_SUCCESS;
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *vte_widget = vte_terminal_new();
    VteTerminal *vte = VTE_TERMINAL(vte_widget);

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
    char *default_argv[2] = {NULL, NULL};

    if (execute) {
        int argcp;
        char **argvp;
        g_shell_parse_argv(execute, &argcp, &argvp, &error);
        if (error) {
            g_printerr("Failed to parse command: %s\n", error->message);
            return EXIT_FAILURE;
        }
        command_argv = argvp;
    } else {
        default_argv[0] = vte_terminal_get_user_shell_with_fallback();
        command_argv = default_argv;
    }

    VtePty *pty = vte_terminal_pty_new(vte, VTE_PTY_DEFAULT, &error);

    if (!pty) {
        g_printerr("Failed to create pty: %s\n", error->message);
        return EXIT_FAILURE;
    }

    search_panel_info panel = {vte, gtk_entry_new(),
                               gtk_alignment_new(0, 0, 1, 1),
                               overlay_mode::hidden};
    keybind_info info = {panel, {vi_mode::insert, 0, 0, 0, 0}, {FALSE, FALSE, FALSE, -1}};

    load_config(GTK_WINDOW(window), vte, &info.config, &term, &geometry);

    vte_terminal_set_pty_object(vte, pty);
    vte_pty_set_term(pty, term);

    gtk_alignment_set_padding(GTK_ALIGNMENT(panel.panel), 5, 5, 5, 5);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), panel.panel);

    gtk_widget_set_halign(panel.entry, GTK_ALIGN_START);
    gtk_widget_set_valign(panel.entry, GTK_ALIGN_END);

    gtk_container_add(GTK_CONTAINER(panel.panel), panel.entry);
    gtk_container_add(GTK_CONTAINER(overlay), vte_widget);
    gtk_container_add(GTK_CONTAINER(window), overlay);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte, "child-exited", G_CALLBACK(exit_with_status), NULL);
    g_signal_connect(vte, "key-press-event", G_CALLBACK(key_press_cb), &info);
    g_signal_connect(panel.entry, "key-press-event", G_CALLBACK(entry_key_press_cb), &info.panel);
    g_signal_connect(overlay, "get-child-position", G_CALLBACK(position_overlay_cb), NULL);
    g_signal_connect(vte, "button-press-event", G_CALLBACK(button_press_cb), &info.config.clickable_url);
    g_signal_connect(vte, "beep", G_CALLBACK(beep_cb), &info.config.urgent_on_bell);
    g_signal_connect(window, "focus-in-event",  G_CALLBACK(focus_cb), NULL);
    g_signal_connect(window, "focus-out-event", G_CALLBACK(focus_cb), NULL);
    g_signal_connect(vte, "window-title-changed", G_CALLBACK(window_title_cb),
                     &info.config.dynamic_title);
    window_title_cb(vte, &info.config.dynamic_title);

    if (geometry) {
        gtk_widget_show_all(overlay);
        gtk_widget_show_all(panel.panel);
        if (!gtk_window_parse_geometry(GTK_WINDOW(window), geometry)) {
            g_printerr("Invalid geometry string: %s\n", geometry);
        }
        g_free(geometry);
    }

    gtk_widget_grab_focus(vte_widget);
    gtk_widget_show_all(window);
    gtk_widget_hide(panel.panel);

    GdkWindow *gdk_window = gtk_widget_get_window(window);
    if (!gdk_window) {
        g_printerr("no window");
        return EXIT_FAILURE;
    }
    char xid_s[std::numeric_limits<long unsigned>::digits10 + 1];
    snprintf(xid_s, sizeof xid_s, "%lu", GDK_WINDOW_XID(gdk_window));
    char **env = g_get_environ();
    env = g_environ_setenv(env, "WINDOWID", xid_s, TRUE);
    env = g_environ_setenv(env, "TERM", term, TRUE);

    GPid ppid;
    if (g_spawn_async(NULL, command_argv, env,
                      (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH),
                      (GSpawnChildSetupFunc)vte_pty_child_setup, pty,
                      &ppid, &error)) {
        vte_terminal_watch_child(vte, ppid);
    } else {
        g_printerr("The new terminal's command failed to run: %s\n", error->message);
        return EXIT_FAILURE;
    }

    int width, height, padding_w, padding_h;
    long char_width = vte_terminal_get_char_width(vte);
    long char_height = vte_terminal_get_char_height(vte);

    gtk_window_get_size(GTK_WINDOW(window), &width, &height);
    get_vte_padding(vte, &padding_w, &padding_h);
    vte_terminal_set_size(vte,
                          (width - padding_w) / char_width,
                          (height - padding_h) / char_height);

    g_strfreev(env);

    gtk_main();
    return EXIT_FAILURE; // child process did not exit
}

// vim: et:sts=4:sw=4:cino=(0
