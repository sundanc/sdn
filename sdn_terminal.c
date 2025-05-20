#include <gtk/gtk.h>
#include <vte/vte.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h> // Add this line for PATH_MAX

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define WINDOW_TITLE "SDN Terminal"
#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 500

static gboolean on_key_press(GtkWidget *terminal, GdkEventKey *event, gpointer user_data);
static void on_child_exit(VteTerminal *terminal, gint status, gpointer user_data);
static gchar* get_shell_path();
static void spawn_callback(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data);

int main(int argc, char *argv[]) {
    GtkWidget *window, *terminal;
    GdkRGBA foreground, background;
    char *shell_path;
    char **command;

    // Initialize GTK
    gtk_init(&argc, &argv);

    // Create window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), WINDOW_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(window), DEFAULT_WIDTH, DEFAULT_HEIGHT);
    g_signal_connect(window, "delete-event", G_CALLBACK(gtk_main_quit), NULL);

    // Create terminal widget
    terminal = vte_terminal_new();
    gtk_container_add(GTK_CONTAINER(window), terminal);

    // Set terminal colors
    gdk_rgba_parse(&foreground, "#FFFFFF");
    gdk_rgba_parse(&background, "#1A1A1A");
    vte_terminal_set_colors(VTE_TERMINAL(terminal), &foreground, &background, NULL, 0);

    // Set scrollback lines
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(terminal), 10000);

    // Connect signals
    g_signal_connect(terminal, "child-exited", G_CALLBACK(on_child_exit), window);
    g_signal_connect(terminal, "key-press-event", G_CALLBACK(on_key_press), NULL);

    // Get the path to the sdn shell
    shell_path = get_shell_path();
    if (!shell_path) {
        g_print("Could not find sdn shell executable.\n");
        return 1;
    }

    // Setup command: {shell_path, NULL}
    command = (char*[]){shell_path, NULL};

    // Start shell in terminal using the non-deprecated spawn_async function
    vte_terminal_spawn_async(
        VTE_TERMINAL(terminal),
        VTE_PTY_DEFAULT,
        NULL,                 // Working directory (NULL = current)
        command,              // Command
        NULL,                 // Environment
        G_SPAWN_DEFAULT,      // Spawn flags
        NULL, NULL,           // Child setup function, data
        NULL,                 // Cancellable
        -1,                   // Timeout (-1 = no timeout)
        NULL,                 // Cancellable
        spawn_callback,       // Callback
        window                // User data passed to callback
    );

    // Show everything
    gtk_widget_show_all(window);

    // Main loop
    gtk_main();

    free(shell_path);
    return 0;
}

// Handle keyboard shortcuts
static gboolean on_key_press(GtkWidget *terminal, GdkEventKey *event, gpointer user_data) {
    (void)user_data; // Suppress unused parameter warning
    // Ctrl+Shift+C - Copy
    if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && 
        event->keyval == GDK_KEY_C) {
        vte_terminal_copy_clipboard_format(VTE_TERMINAL(terminal), VTE_FORMAT_TEXT);
        return TRUE;
    }
    
    // Ctrl+Shift+V - Paste
    if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && 
        event->keyval == GDK_KEY_V) {
        vte_terminal_paste_clipboard(VTE_TERMINAL(terminal));
        return TRUE;
    }
    
    // Let the terminal handle other keypresses
    return FALSE;
}

// Handle terminal exit
static void on_child_exit(VteTerminal *terminal, gint status, gpointer user_data) {
    (void)terminal; // Suppress unused parameter warning
    (void)status;   // Suppress unused parameter warning
    GtkWidget *window = GTK_WIDGET(user_data);
    gtk_widget_destroy(window);
    gtk_main_quit();
}

// Get the path to the sdn shell
static gchar* get_shell_path() {
    char *path = NULL;
    char exec_path[PATH_MAX];
    ssize_t len;
    
    // Try to get the absolute path to this executable
    len = readlink("/proc/self/exe", exec_path, sizeof(exec_path) - 1);
    if (len != -1) {
        exec_path[len] = '\0';
        char *dir = dirname(exec_path);
        
        // Construct the path to the sdn shell (assume it's in the same directory)
        path = g_strdup_printf("%s/sdn", dir);
        
        // Check if the shell exists and is executable
        if (access(path, X_OK) != 0) {
            g_free(path);
            path = NULL;
        }
    }
    
    // If not found in the same directory, try to find it in PATH
    if (!path) {
        char *env_path = getenv("PATH");
        if (env_path) {
            char *path_copy = strdup(env_path);
            char *dir = strtok(path_copy, ":");
            
            while (dir && !path) {
                char *test_path = g_strdup_printf("%s/sdn", dir);
                if (access(test_path, X_OK) == 0) {
                    path = test_path;
                } else {
                    g_free(test_path);
                }
                dir = strtok(NULL, ":");
            }
            
            free(path_copy);
        }
    }
    
    return path;
}

// Callback for spawn_async
static void spawn_callback(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data) {
    (void)terminal; // Suppress unused parameter warning
    (void)pid;      // Suppress unused parameter warning
    GtkWidget *window = GTK_WIDGET(user_data);
    
    if (error != NULL) {
        g_printerr("Error spawning terminal: %s\n", error->message);
        g_error_free(error);
        gtk_widget_destroy(window);
        gtk_main_quit();
    }
}
