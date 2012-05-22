#include <stdlib.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "config.h"

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

    g_signal_connect(G_OBJECT (window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte, "key-press-event", G_CALLBACK(key_press_cb), NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
