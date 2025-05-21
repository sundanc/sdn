#include <gtk/gtk.h>
#include <vte/vte.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h> // Add this line for PATH_MAX
#include <glib.h> // For g_strcmp0

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define WINDOW_TITLE "SDN Terminal"
#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 500

// Theme definitions
typedef enum {
    THEME_DARK,
    THEME_LIGHT_BLUE,
    THEME_LIGHT_MODE,
    THEME_GRAY
} TerminalTheme;

static void apply_theme(VteTerminal *terminal, TerminalTheme theme);
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data); // Changed GtkWidget *terminal to GtkWidget *widget
static void on_child_exit(VteTerminal *terminal, gint status, gpointer user_data);
static gchar* get_shell_path();
static void spawn_callback(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data);

int main(int argc, char *argv[]) {
    GtkWidget *window, *terminal;
    char *shell_path;
    char **command;
    TerminalTheme current_theme = THEME_DARK; // Default theme

    // Initialize GTK (should be called before any GTK functions)
    gtk_init(&argc, &argv);

    // Parse command-line arguments for theme
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--theme") == 0 && i + 1 < argc) {
            i++; // Move to the theme name
            if (g_strcmp0(argv[i], "light-blue") == 0) {
                current_theme = THEME_LIGHT_BLUE;
            } else if (g_strcmp0(argv[i], "light") == 0) {
                current_theme = THEME_LIGHT_MODE;
            } else if (g_strcmp0(argv[i], "gray") == 0) {
                current_theme = THEME_GRAY;
            } else if (g_strcmp0(argv[i], "dark") == 0) {
                current_theme = THEME_DARK;
            } else {
                g_warning("Unknown theme: %s. Using default (dark).", argv[i]);
            }
        }
    }

    // Create window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), WINDOW_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(window), DEFAULT_WIDTH, DEFAULT_HEIGHT);
    g_signal_connect(window, "delete-event", G_CALLBACK(gtk_main_quit), NULL);

    // Create terminal widget
    terminal = vte_terminal_new();
    gtk_container_add(GTK_CONTAINER(window), terminal);

    // Set terminal colors based on selected theme
    apply_theme(VTE_TERMINAL(terminal), current_theme);

    // Set scrollback lines
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(terminal), 10000);

    // Connect signals
    g_signal_connect(terminal, "child-exited", G_CALLBACK(on_child_exit), window);
    g_signal_connect(terminal, "key-press-event", G_CALLBACK(on_key_press), NULL);

    // Get the path to the sdn shell
    shell_path = get_shell_path();
    if (!shell_path) {
        g_error("Could not find sdn shell executable.");
        return 1;
    }

    // Setup command: {shell_path, NULL}
    command = (char*[]){shell_path, NULL};

    // Start shell in terminal using the non-deprecated spawn_async function
    vte_terminal_spawn_async(
        VTE_TERMINAL(terminal),       // VteTerminal *terminal
        VTE_PTY_DEFAULT,              // VtePtyFlags pty_flags
        NULL,                         // const char *working_directory
        command,                      // char **argv
        NULL,                         // char **envv
        G_SPAWN_DEFAULT,              // GSpawnFlags spawn_flags
        NULL,                         // GSpawnChildSetupFunc child_setup
        NULL,                         // gpointer child_setup_data
        NULL,                         // GDestroyNotify child_setup_data_destroy
        -1,                           // int timeout
        NULL,                         // GCancellable *cancellable
        (VteTerminalSpawnAsyncCallback)spawn_callback, // VteTerminalSpawnAsyncCallback callback
        window                        // gpointer user_data
    );

    // Show everything
    gtk_widget_show_all(window);

    // Main loop
    gtk_main();

    free(shell_path);
    return 0;
}

// Function to apply theme colors
static void apply_theme(VteTerminal *terminal, TerminalTheme theme) {
    GdkRGBA foreground_color, background_color;

    switch (theme) {
        case THEME_LIGHT_BLUE:
            gdk_rgba_parse(&foreground_color, "#002B36"); // Dark blue/black for text
            gdk_rgba_parse(&background_color, "#A6D1E6"); // Light blue
            break;
        case THEME_LIGHT_MODE:
            gdk_rgba_parse(&foreground_color, "#657B83"); // Solarized dark gray for text
            gdk_rgba_parse(&background_color, "#FDF6E3"); // Solarized light background
            break;
        case THEME_GRAY:
            gdk_rgba_parse(&foreground_color, "#DCDCDC"); // Light gray for text
            gdk_rgba_parse(&background_color, "#3C3C3C"); // Darker gray
            break;
        case THEME_DARK:
        default:
            gdk_rgba_parse(&foreground_color, "#FFFFFF"); // White text
            gdk_rgba_parse(&background_color, "#1A1A1A"); // Original dark background
            break;
    }
    vte_terminal_set_colors(terminal, &foreground_color, &background_color, NULL, 0);
}

// Handle keyboard shortcuts
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) { // Changed GtkWidget *terminal to GtkWidget *widget
    (void)user_data; // Suppress unused parameter warning
    VteTerminal *terminal = VTE_TERMINAL(widget); // Cast widget to VteTerminal
    g_print("on_key_press: keyval=0x%x, state=0x%x, GDK_KEY_C=0x%x, GDK_CONTROL_MASK=0x%x, GDK_SHIFT_MASK=0x%x\n", 
            event->keyval, event->state, GDK_KEY_C, GDK_CONTROL_MASK, GDK_SHIFT_MASK);


    // Ctrl+Shift+C - Copy
    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) && event->keyval == GDK_KEY_C) {
        g_print("Ctrl+Shift+C detected for copy\n");
        vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
        return TRUE; // Event handled
    }
    
    // Ctrl+Shift+V - Paste
    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) && event->keyval == GDK_KEY_V) {
        g_print("Ctrl+Shift+V detected for paste\n");
        vte_terminal_paste_clipboard(terminal);
        return TRUE; // Event handled
    }
    
    g_print("Key press not handled by custom shortcuts.\n");
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
