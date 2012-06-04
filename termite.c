#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdlib.h>
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

typedef struct search_panel_info {
    GtkWidget *vte;
    GtkWidget *entry;
    GtkWidget *da;
    GtkBin *panel;
    bool reverse;
} search_panel_info;

typedef struct url_data {
    gchar *url;
    unsigned line;
    gint pos;
} url_data;

GList *list = NULL;

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

static gboolean search_key_press_cb(GtkEntry *entry, GdkEventKey *event, search_panel_info *info) {
    gboolean ret = FALSE;

    if (event->keyval == GDK_KEY_Escape) {
        ret = TRUE;
    } else if (event->keyval == GDK_KEY_Return) {
        search(VTE_TERMINAL(info->vte), gtk_entry_get_text(entry), info->reverse);
        ret = TRUE;
    }

    if (ret) {
        gtk_widget_hide(GTK_WIDGET(info->panel));
        gtk_widget_grab_focus(info->vte);
    }
    return ret;
}

static void find_urls(VteTerminal *vte) {
    GRegex *regex = g_regex_new(url_regex, G_REGEX_CASELESS, G_REGEX_MATCH_NOTEMPTY, NULL);
    gchar *content = vte_terminal_get_text(vte, NULL, NULL, NULL);

    char *s_ptr = content, *saveptr;

    for (unsigned line = 0; ; line++, s_ptr = NULL) {
        char *token = strtok_r(s_ptr, "\n", &saveptr);

        if (!token) {
            break;
        }

        GError *error = NULL;
        GMatchInfo *info;

        g_regex_match_full(regex, token, -1, 0, (GRegexMatchFlags)0, &info, &error);
        while (g_match_info_matches(info)) {
            url_data *node = g_malloc(sizeof(url_data));

            node->url = g_match_info_fetch(info, 0);
            node->line = line;
            g_match_info_fetch_pos(info, 0, &node->pos, NULL);

            list = g_list_append(list, node);
            g_match_info_next(info, &error);
        }

        g_match_info_free(info);

        if (error) {
            g_printerr("Error while matching: %s\n", error->message);
            g_error_free(error);
        }
    }
    g_regex_unref(regex);
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
                info->reverse = false;
                gtk_widget_show(GTK_WIDGET(info->panel));
                gtk_widget_grab_focus(info->entry);
                return TRUE;
            case KEY(KEY_RSEARCH):
                info->reverse = true;
                gtk_widget_show(GTK_WIDGET(info->panel));
                gtk_widget_grab_focus(info->entry);
                return TRUE;
            case KEY(KEY_URL):
                /* search(vte, url_regex, false); */
                find_urls(vte);
                gtk_widget_show(info->da);
                return TRUE;
            case KEY(KEY_RURL):
                search(vte, url_regex, true);
                return TRUE;
        }
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

#ifdef URGENT_ON_BEEP
static void beep_handler(GtkWindow *window) {
    gtk_window_set_urgency_hint(window, TRUE);
}

static gboolean focus_in_handler(GtkWindow *window) {
    gtk_window_set_urgency_hint(window, FALSE);
    return FALSE;
}
#endif

#ifdef DYNAMIC_TITLE
static void window_title_cb(VteTerminal *vte, GtkWindow *window) {
    const char *t = vte_terminal_get_window_title(vte);
    gtk_window_set_title(window, t ? t : "termite");
}
#endif

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

static gboolean draw_cb(GtkWidget *widget, cairo_t *cr, VteTerminal *vte) {
    if (list) {
        GList *l = list;

        glong cols = vte_terminal_get_column_count(vte);
        glong cw = vte_terminal_get_char_width(vte);
        glong ch = vte_terminal_get_char_height(vte);

        cairo_set_line_width(cr, 1);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_stroke(cr);

        unsigned offset = 0;
        char buffer[10];

        for (; l != NULL; l = l->next) {
            url_data *data = l->data;

            glong x = data->pos % cols * cw;
            offset += data->pos / cols;
            glong y = (data->line + offset) * ch;

            /* move this into a function? */
            cairo_rectangle(cr, x, y, 8, 8);
            cairo_stroke_preserve(cr);
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_fill(cr);

            cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_set_font_size(cr, 9);
            cairo_move_to(cr, x, y + 7);

            snprintf(buffer, 10, "%d", data->line);
            cairo_show_text(cr, buffer);
        }
    }

    return FALSE;
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

#ifdef ICON_NAME
    GdkPixbuf *icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), ICON_NAME, 48, 0, NULL);
    if (icon) {
        gtk_window_set_icon(GTK_WINDOW(window), icon);
    }
#endif

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

    if (!pty) { g_printerr("Failed to create pty: %s\n", error->message);
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

    GdkRGBA transparent = {0, 0, 0, 0};

    GtkWidget *overlay[2] = {
        gtk_overlay_new(),
        gtk_overlay_new()
    };

    GtkWidget *da = gtk_drawing_area_new();
    GtkWidget *align = gtk_alignment_new(0, 0, 1, 1);
    GtkWidget *entry = gtk_entry_new();

    gtk_widget_override_background_color(overlay[1], GTK_STATE_FLAG_NORMAL, &transparent);
    gtk_widget_override_background_color(da, GTK_STATE_FLAG_NORMAL, &transparent);

    gtk_widget_set_halign(da, GTK_ALIGN_FILL);
    gtk_widget_set_valign(da, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay[1]), da);

    gtk_alignment_set_padding(GTK_ALIGNMENT(align), 5, 5, 5, 5);
    gtk_widget_set_halign(align, GTK_ALIGN_START);
    gtk_widget_set_valign(align, GTK_ALIGN_END);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay[0]), align);

    gtk_container_add(GTK_CONTAINER(align), entry);
    gtk_container_add(GTK_CONTAINER(overlay[0]), overlay[1]);
    gtk_container_add(GTK_CONTAINER(overlay[1]), vte);
    gtk_container_add(GTK_CONTAINER(window), overlay[0]);

    search_panel_info info = {vte, entry, da, GTK_BIN(align), false};

    g_signal_connect(window,     "destroy",            G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(da,         "draw",               G_CALLBACK(draw_cb), vte);
    g_signal_connect(vte,        "child-exited",       G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte,        "key-press-event",    G_CALLBACK(key_press_cb), &info);
    g_signal_connect(entry,      "key-press-event",    G_CALLBACK(search_key_press_cb), &info);
    g_signal_connect(overlay[0], "get-child-position", G_CALLBACK(position_overlay_cb), NULL);

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vte), scrollback_lines);
    vte_terminal_set_font_from_string(VTE_TERMINAL(vte), font);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(vte), scroll_on_output);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(vte), scroll_on_keystroke);
    vte_terminal_set_audible_bell(VTE_TERMINAL(vte), audible_bell);
    vte_terminal_set_visible_bell(VTE_TERMINAL(vte), visible_bell);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(vte), mouse_autohide);
    vte_terminal_set_cursor_shape(VTE_TERMINAL(vte), CONCAT2(VTE_CURSOR_SHAPE_, CURSOR_SHAPE));
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(vte), CONCAT2(VTE_CURSOR_BLINK_, CURSOR_BLINK));
    gtk_window_set_has_resize_grip(GTK_WINDOW(window), resize_grip);

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

#ifdef URGENT_ON_BEEP
    g_signal_connect_swapped(vte, "beep", G_CALLBACK(beep_handler), window);
    g_signal_connect(window, "focus-in-event", G_CALLBACK(focus_in_handler), NULL);
#endif

#ifdef DYNAMIC_TITLE
    window_title_cb(VTE_TERMINAL(vte), GTK_WINDOW(window));
    g_signal_connect(vte, "window-title-changed", G_CALLBACK(window_title_cb), window);
#endif

    gtk_widget_grab_focus(vte);
    gtk_widget_show_all(window);
    gtk_widget_hide(da);
    gtk_widget_hide(align);
    gtk_main();
    return 0;
}
