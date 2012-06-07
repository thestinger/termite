#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <string.h>

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "config.h"

#define CONCAT(X, Y) X ## Y
#define CONCAT2(X, Y) CONCAT(X, Y)
#define KEY(X) CONCAT(GDK_KEY_, X)

#ifndef __GNUC__
# define __attribute__(x)
#endif

enum overlay_mode {
    OVERLAY_HIDDEN = 0,
    OVERLAY_SEARCH,
    OVERLAY_RSEARCH,
    OVERLAY_COMPLETION
};

typedef struct search_panel_info {
    GtkWidget *vte;
    GtkWidget *entry;
    GtkBin *panel;
    enum overlay_mode mode;
} search_panel_info;

static gboolean add_to_list_store(char *key,
                                  __attribute__((unused)) void *value,
                                  GtkListStore *store) {
    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, key, -1);
    return FALSE;
}

static GtkTreeModel *create_completion_model(VteTerminal *vte) {
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);

    glong end_row, end_col;
    vte_terminal_get_cursor_position(vte, &end_col, &end_row);
    gchar *content = vte_terminal_get_text_range(vte, 0, 0, end_row, end_col,
                                                 NULL, NULL, NULL);

    if (!content) {
        g_printerr("no content returned for completion");
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

static void search(VteTerminal *vte, const char *pattern, bool reverse) {
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

static gboolean entry_key_press_cb(GtkEntry *entry, GdkEventKey *event, search_panel_info *info) {
    gboolean ret = FALSE;

    if (event->keyval == GDK_KEY_Escape) {
        ret = TRUE;
    } else if (event->keyval == GDK_KEY_Return) {
        const gchar *text = gtk_entry_get_text(entry);

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
        gtk_widget_hide(GTK_WIDGET(info->panel));
        gtk_widget_grab_focus(info->vte);
    }
    return ret;
}

static void overlay_show(search_panel_info *info, enum overlay_mode mode, bool complete) {
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
    gtk_widget_show(GTK_WIDGET(info->panel));
    gtk_widget_grab_focus(info->entry);
}

static gboolean key_press_cb(VteTerminal *vte, GdkEventKey *event, search_panel_info *info) {
    const guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    if (modifiers == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
        switch (gdk_keyval_to_lower(event->keyval)) {
            case KEY(KEY_COPY):
                vte_terminal_copy_clipboard(vte);
                return TRUE;
            case KEY(KEY_PASTE):
                vte_terminal_paste_clipboard(vte);
                return TRUE;
            case KEY(KEY_PREV):
                vte_terminal_search_find_previous(vte);
                vte_terminal_copy_primary(vte);
                return TRUE;
            case KEY(KEY_NEXT):
                vte_terminal_search_find_next(vte);
                vte_terminal_copy_primary(vte);
                return TRUE;
            case KEY(KEY_SEARCH):
                overlay_show(info, OVERLAY_SEARCH, true);
                return TRUE;
            case KEY(KEY_RSEARCH):
                overlay_show(info, OVERLAY_RSEARCH, true);
                return TRUE;
            case KEY(KEY_URL):
                search(vte, url_regex, false);
                return TRUE;
            case KEY(KEY_RURL):
                search(vte, url_regex, true);
                return TRUE;
        }
    } else if (modifiers == GDK_CONTROL_MASK && event->keyval == GDK_KEY_Tab) {
        overlay_show(info, OVERLAY_COMPLETION, true);
        return TRUE;
    }
    return FALSE;
}

#ifdef CLICKABLE_URL
static void get_vte_padding(VteTerminal *vte, int *w, int *h) {
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

static char *check_match(VteTerminal *vte, int event_x, int event_y) {
    int xpad, ypad, tag;
    get_vte_padding(vte, &xpad, &ypad);
    return vte_terminal_match_check(vte,
                                    (event_x - ypad) / vte_terminal_get_char_width(vte),
                                    (event_y - ypad) / vte_terminal_get_char_height(vte),
                                    &tag);
}

static gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event) {
    char *match = check_match(vte, (int)event->x, (int)event->y);
    if (event->button == 1 && event->type == GDK_BUTTON_PRESS && match != NULL) {
        char *argv[] = URL_COMMAND(match);
        g_spawn_async(NULL, argv, NULL, (GSpawnFlags)0, NULL, NULL, NULL, NULL);
        g_free(match);
        return TRUE;
    }
    return FALSE;
}
#endif

static void beep_handler(GtkWindow *window) {
    gtk_window_set_urgency_hint(window, TRUE);
}

static gboolean focus_in_handler(GtkWindow *window) {
    gtk_window_set_urgency_hint(window, FALSE);
    return FALSE;
}

static void window_title_cb(VteTerminal *vte, GtkWindow *window) {
    const char *t = vte_terminal_get_window_title(vte);
    gtk_window_set_title(window, t ? t : "termite");
}

static gboolean position_overlay_cb(GtkBin *overlay, GtkWidget *widget, GdkRectangle *alloc) {
    GtkWidget *vte = gtk_bin_get_child(overlay);

    int width  = gtk_widget_get_allocated_width(vte);
    int height = gtk_widget_get_allocated_height(vte);

    GtkRequisition req;
    gtk_widget_get_preferred_size(widget, NULL, &req);

    alloc->x = width - req.width - 40;
    alloc->y = 0;
    alloc->width  = MIN(width, req.width);
    alloc->height = MIN(height, req.height);

    return TRUE;
}

#define IGNORE_ON_ERROR(ERROR) if (ERROR) { g_clear_error(&ERROR); } else

static void load_config(GtkWindow *window, VteTerminal *vte, gboolean *dynamic_title, gboolean *urgent_on_bell) {
    GError *error = NULL;
    GKeyFile *config = g_key_file_new();
    if (!g_key_file_load_from_file(config, "termite.cfg", G_KEY_FILE_NONE, &error)) {
        g_printerr("could not open config file: %s\n", error->message);
        g_error_free(error);
    } else {
        gboolean resize_grip = g_key_file_get_boolean(config, "options", "resize_grip", &error);
        IGNORE_ON_ERROR(error) {
            gtk_window_set_has_resize_grip(window, resize_grip);
        }

        gboolean scroll_on_output = g_key_file_get_boolean(config, "options", "scroll_on_output", &error);
        IGNORE_ON_ERROR(error) {
            vte_terminal_set_scroll_on_output(vte, scroll_on_output);
        }

        gboolean scroll_on_keystroke = g_key_file_get_boolean(config, "options", "scroll_on_keystroke", &error);
        IGNORE_ON_ERROR(error) {
            vte_terminal_set_scroll_on_keystroke(vte, scroll_on_keystroke);
        }

        gboolean audible_bell = g_key_file_get_boolean(config, "options", "audible_bell", &error);
        IGNORE_ON_ERROR(error) {
            vte_terminal_set_audible_bell(vte, audible_bell);
        }

        gboolean visible_bell = g_key_file_get_boolean(config, "options", "visible_bell", &error);
        IGNORE_ON_ERROR(error) {
            vte_terminal_set_visible_bell(vte, visible_bell);
        }

        gboolean mouse_autohide = g_key_file_get_boolean(config, "options", "mouse_autohide", &error);
        IGNORE_ON_ERROR(error) {
            vte_terminal_set_mouse_autohide(vte, mouse_autohide);
        }

        *dynamic_title = g_key_file_get_boolean(config, "options", "dynamic_title", &error);
        g_clear_error(&error);

        *urgent_on_bell = g_key_file_get_boolean(config, "options", "urgent_on_bell", &error);
        g_clear_error(&error);

        gchar *font = g_key_file_get_string(config, "options", "font", &error);
        IGNORE_ON_ERROR(error) {
            vte_terminal_set_font_from_string(vte, font);
            g_free(font);
        }

        gint scrollback_lines = g_key_file_get_integer(config, "options", "scrollback_lines", &error);
        IGNORE_ON_ERROR(error) {
            vte_terminal_set_scrollback_lines(vte, scrollback_lines);
        }

        gchar *cursor_blink = g_key_file_get_string(config, "options", "cursor_blink", &error);
        IGNORE_ON_ERROR(error) {
            if (!strcmp(cursor_blink, "SYSTEM")) {
                vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_SYSTEM);
            } else if (!strcmp(cursor_blink, "ON")) {
                vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_ON);
            } else if (!strcmp(cursor_blink, "OFF")) {
                vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_OFF);
            }
            g_free(cursor_blink);
        }

        gchar *cursor_shape = g_key_file_get_string(config, "options", "cursor_shape", &error);
        IGNORE_ON_ERROR(error) {
            if (!strcmp(cursor_shape, "BLOCK")) {
                vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_BLOCK);
            } else if (!strcmp(cursor_shape, "IBEAM")) {
                vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_IBEAM);
            } else if (!strcmp(cursor_shape, "UNDERLINE")) {
                vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_UNDERLINE);
            }
            g_free(cursor_shape);
        }

        gchar *icon_name = g_key_file_get_string(config, "options", "icon_name", &error);
        IGNORE_ON_ERROR(error) {
            gtk_window_set_icon_name(window, icon_name);
            g_free(icon_name);
        }
    }
    g_key_file_free(config);
}

int main(int argc, char **argv) {
    GError *error = NULL;

    GOptionContext *context = g_option_context_new("[COMMAND]");
    const gchar *role = NULL;
    const GOptionEntry entries[] = {
        { "role", 'r', 0, G_OPTION_ARG_STRING, &role, "The role to use", "ROLE" },
        { NULL }
    };
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("option parsing failed: %s\n", error->message);
        return 1;
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    /*gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);*/

    if (role) {
        gtk_window_set_role(GTK_WINDOW(window), role);
    }

    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *vte = vte_terminal_new();

    char **command_argv;
    char *default_argv[2] = {NULL, NULL};

    if (argc > 1) {
        command_argv = &argv[1];
    } else {
        default_argv[0] = vte_get_user_shell();
        if (!default_argv[0]) default_argv[0] = "/bin/sh";
        command_argv = default_argv;
    }

    VtePty *pty = vte_terminal_pty_new(VTE_TERMINAL(vte), VTE_PTY_DEFAULT, &error);

    if (!pty) {
        g_printerr("Failed to create pty: %s\n", error->message);
        return 1;
    }

    vte_terminal_set_pty_object(VTE_TERMINAL(vte), pty);
    vte_pty_set_term(pty, term);

    GPid ppid;

    if (g_spawn_async(NULL, command_argv, NULL,
                      (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH),
                      (GSpawnChildSetupFunc)vte_pty_child_setup, pty,
                      &ppid, &error)) {
        vte_terminal_watch_child(VTE_TERMINAL(vte), ppid);
    } else {
        g_printerr("The new terminal's command failed to run: %s\n", error->message);
        return 1;
    }

    GtkWidget *alignment = gtk_alignment_new(0, 0, 1, 1);
    gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 5, 5, 5, 5);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), alignment);

    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_halign(entry, GTK_ALIGN_START);
    gtk_widget_set_valign(entry, GTK_ALIGN_END);

    gtk_container_add(GTK_CONTAINER(alignment), entry);
    gtk_container_add(GTK_CONTAINER(overlay), vte);
    gtk_container_add(GTK_CONTAINER(window), overlay);

    search_panel_info info = {vte, entry, GTK_BIN(alignment), OVERLAY_HIDDEN};

    g_signal_connect(window,  "destroy",            G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte,     "child-exited",       G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte,     "key-press-event",    G_CALLBACK(key_press_cb), &info);
    g_signal_connect(entry,   "key-press-event",    G_CALLBACK(entry_key_press_cb), &info);
    g_signal_connect(overlay, "get-child-position", G_CALLBACK(position_overlay_cb), NULL);

    gboolean dynamic_title = FALSE, urgent_on_bell = FALSE;
    load_config(GTK_WINDOW(window), VTE_TERMINAL(vte), &dynamic_title, &urgent_on_bell);

#ifdef TRANSPARENCY
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (!visual) {
        visual = gdk_screen_get_system_visual(screen);
    }
    gtk_widget_set_visual(window, visual);
    vte_terminal_set_background_saturation(VTE_TERMINAL(vte), TRANSPARENCY);
    vte_terminal_set_opacity(VTE_TERMINAL(vte), (guint16)(0xffff * (1 - TRANSPARENCY)));
#endif

    // set colors
    GdkColor foreground, background, cursor, palette[16];
    gdk_color_parse(foreground_color, &foreground);
    gdk_color_parse(background_color, &background);
    gdk_color_parse(cursor_color, &cursor);

    for (unsigned i = 0; i < 16; i++) {
        gdk_color_parse(colors[i], &palette[i]);
    }

    vte_terminal_set_colors(VTE_TERMINAL(vte), &foreground, &background, palette, 16);
    vte_terminal_set_color_cursor(VTE_TERMINAL(vte), &cursor);

#ifdef CLICKABLE_URL
    int tmp = vte_terminal_match_add_gregex(VTE_TERMINAL(vte),
                                            g_regex_new(url_regex,
                                                        G_REGEX_CASELESS,
                                                        G_REGEX_MATCH_NOTEMPTY,
                                                        NULL),
                                            (GRegexMatchFlags)0);
    vte_terminal_match_set_cursor_type(VTE_TERMINAL(vte), tmp, GDK_HAND2);
    g_signal_connect(vte, "button-press-event", G_CALLBACK(button_press_cb), NULL);
#endif

    if (urgent_on_bell) {
        g_signal_connect_swapped(vte, "beep", G_CALLBACK(beep_handler), window);
        g_signal_connect(window, "focus-in-event", G_CALLBACK(focus_in_handler), NULL);
    }

    if (dynamic_title) {
        window_title_cb(VTE_TERMINAL(vte), GTK_WINDOW(window));
        g_signal_connect(vte, "window-title-changed", G_CALLBACK(window_title_cb), window);
    }

    gtk_widget_grab_focus(vte);
    gtk_widget_show_all(window);
    gtk_widget_hide(alignment);
    gtk_main();
    return 0;
}
