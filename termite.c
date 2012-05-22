#include <stdlib.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "config.h"

#ifndef __GNUC__
#  define  __attribute__(x)
#endif

enum search_direction {
    search_forward,
    search_backward
};

typedef struct search_dialog_info {
    GtkWidget *vte;
    GtkWidget *entry;
    enum search_direction direction;
} search_dialog_info;

static void search_response_cb(GtkDialog *dialog, gint response_id, search_dialog_info *info) {
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GRegex *regex = vte_terminal_search_get_gregex(VTE_TERMINAL(info->vte));
        if (regex) g_regex_unref(regex);
        regex = g_regex_new(gtk_entry_get_text(GTK_ENTRY(info->entry)), 0, 0, NULL);
        vte_terminal_search_set_gregex(VTE_TERMINAL(info->vte), regex);

        if (info->direction == search_forward) {
            vte_terminal_search_find_next(VTE_TERMINAL(info->vte));
        } else {
            vte_terminal_search_find_previous(VTE_TERMINAL(info->vte));
        }
    }

    free(info);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void open_search_dialog(GtkWidget *vte, enum search_direction direction) {
    search_dialog_info *info = malloc(sizeof (info));
    info->vte = vte;
    info->entry = gtk_entry_new();
    info->direction = direction;

    GtkWidget *dialog, *content_area;
    dialog = gtk_dialog_new_with_buttons("Search",
                                         GTK_WINDOW(gtk_widget_get_toplevel(vte)),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK,
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    g_signal_connect(dialog, "response", G_CALLBACK(search_response_cb), info);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_entry_set_activates_default(GTK_ENTRY(info->entry), TRUE);

    gtk_container_add(GTK_CONTAINER(content_area), info->entry);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(GTK_WIDGET(info->entry));
}

static gboolean key_press_cb(GtkWidget *vte, GdkEventKey *event) {
    const GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();

    if (modifiers == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
        switch (gdk_keyval_to_lower(event->keyval)) {
            case GDK_p:
                vte_terminal_search_find_previous(VTE_TERMINAL(vte));
                return TRUE;
            case GDK_n:
                vte_terminal_search_find_next(VTE_TERMINAL(vte));
                return TRUE;
            case GDK_question:
                open_search_dialog(vte, search_backward);
                return TRUE;
        }
    }
    if (modifiers == GDK_CONTROL_MASK && event->keyval == GDK_slash) {
        open_search_dialog(vte, search_forward);
        return TRUE;
    }
    return FALSE;
}

#if VTE_CHECK_VERSION(0, 24, 0)
static void get_vte_padding(VteTerminal *vte, int *w, int *h) {
    GtkBorder *border = NULL;

    gtk_widget_style_get(GTK_WIDGET(vte), "inner-border", &border, NULL);
    if (border == NULL) {
        g_warning("VTE's inner-border property unavailable");
        *w = *h = 0;
    } else {
        *w = border->left + border->right;
        *h = border->top + border->bottom;
        gtk_border_free(border);
    }
}
#else
#define get_vte_padding vte_terminal_get_padding
#endif

static char *check_match(VteTerminal *vte, int event_x, int event_y) {
    int xpad, ypad, tag;

    get_vte_padding(vte, &xpad, &ypad);
    return vte_terminal_match_check(vte,
                                    (event_x - ypad) / vte_terminal_get_char_width(vte),
                                    (event_y - ypad) / vte_terminal_get_char_height(vte),
                                    &tag);
}

static gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event) {
    char *match = check_match(vte, event->x, event->y);

    if (event->button == 1 && event->type == GDK_BUTTON_PRESS && match != NULL) {
        const char *argv[3] = {url_command, match, NULL};
        g_spawn_async(NULL, (char **)argv, NULL, 0, NULL, NULL, NULL, NULL);
        g_free(match);
        return TRUE;
    }

    return FALSE;
}

#ifdef URGENT_ON_BEEP
static void beep_handler(__attribute__((unused)) VteTerminal *vte, GtkWidget *window) {
    GtkWindow *gwin = GTK_WINDOW(window);
    if (!gtk_window_is_active(gwin)) {
        gtk_window_set_urgency_hint(gwin, TRUE);
    }
}
#endif

#ifdef DYNAMIC_TITLE
static void window_title_cb(VteTerminal *vte, GtkWindow *window) {
    const char *t = vte_terminal_get_window_title(vte);
    gtk_window_set_title(window, t ? t : "termite");
}
#endif

int main(int argc, char **argv) {
    GError *error = NULL;

    gtk_init(&argc, &argv);

    GtkWidget *window;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    /*gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);*/

    GtkWidget *vte = vte_terminal_new();

    char *command_argv[2] = {NULL, NULL};
    command_argv[0] = g_strdup(g_getenv("SHELL"));

    VtePty *pty = vte_terminal_pty_new(VTE_TERMINAL(vte), 0, &error);

    if (!pty) {
        fprintf(stderr, "Failed to create pty: %s\n", error->message);
        return 1;
    }

    vte_terminal_set_pty_object(VTE_TERMINAL(vte), pty);
    vte_pty_set_term(pty, term);

    GPid ppid;

    if (g_spawn_async(NULL, command_argv, NULL,
                      G_SPAWN_DO_NOT_REAP_CHILD,
                      (GSpawnChildSetupFunc)vte_pty_child_setup, pty,
                      &ppid, &error)) {
        vte_terminal_watch_child(VTE_TERMINAL(vte), ppid);
    } else {
        fprintf(stderr, "The new terminal's command failed to run: %s\n", error->message);
        return 1;
    }

    /*vte_terminal_fork_command_full(VTE_TERMINAL(vte), VTE_PTY_DEFAULT, NULL, command_argv, NULL, 0, NULL, NULL, NULL, NULL);*/

    gtk_container_add(GTK_CONTAINER(window), vte);

    g_signal_connect(vte, "child-exited", G_CALLBACK(gtk_main_quit), NULL);

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vte), scrollback_lines);
    vte_terminal_set_font_from_string(VTE_TERMINAL(vte), font);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(vte), scroll_on_output);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(vte), scroll_on_keystroke);
    vte_terminal_set_audible_bell(VTE_TERMINAL(vte), audible_bell);
    vte_terminal_set_visible_bell(VTE_TERMINAL(vte), visible_bell);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(vte), mouse_autohide);
    vte_terminal_set_backspace_binding(VTE_TERMINAL(vte), VTE_ERASE_ASCII_BACKSPACE);

    // url matching
    int tmp = vte_terminal_match_add_gregex(VTE_TERMINAL(vte),
                                            g_regex_new(url_regex,
                                                G_REGEX_CASELESS,
                                                G_REGEX_MATCH_NOTEMPTY,
                                                NULL),
                                            0);
    vte_terminal_match_set_cursor_type(VTE_TERMINAL(vte), tmp, GDK_HAND2);

    // set colors
    GdkColor foreground, background;
    gdk_color_parse(foreground_color, &foreground);
    gdk_color_parse(background_color, &background);

    GdkColor palette[16];

    for (size_t i = 0; i < 16; i++) {
        gdk_color_parse(colors[i], &palette[i]);
    }

    vte_terminal_set_colors(VTE_TERMINAL(vte), &foreground, &background, palette, 16);

    gtk_widget_grab_focus(vte);

    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte, "button-press-event", G_CALLBACK(button_press_cb), NULL);
    g_signal_connect(vte, "key-press-event", G_CALLBACK(key_press_cb), NULL);

#ifdef URGENT_ON_BEEP
    if (g_signal_lookup("beep", G_TYPE_FROM_INSTANCE(G_OBJECT(vte)))) {
        g_signal_connect(vte, "beep", G_CALLBACK(beep_handler), window);
    }
#endif

#ifdef DYNAMIC_TITLE
    window_title_cb(VTE_TERMINAL(vte), GTK_WINDOW(window));
    g_signal_connect(vte, "window-title-changed", G_CALLBACK(window_title_cb), window);
#endif

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
