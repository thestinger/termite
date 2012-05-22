#include <gtk/gtk.h>
#include <vte/vte.h>

#include "config.h"

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
        fprintf(stderr, "Failed to create pty: %s", error->message);
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
        return 1;
    }

    /*vte_terminal_fork_command_full(VTE_TERMINAL(vte), VTE_PTY_DEFAULT, NULL, command_argv, NULL, 0, NULL, NULL, NULL, NULL);*/

    gtk_container_add(GTK_CONTAINER(window), vte);

    g_signal_connect(vte, "child-exited", G_CALLBACK(gtk_main_quit), NULL);

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vte), scrollback_lines);
    vte_terminal_set_font_from_string(VTE_TERMINAL(vte), font);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(vte), scroll_on_output);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(vte), scroll_on_keystroke);

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

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
