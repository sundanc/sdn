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
#define ICON_NAME "sdn_terminal" // Define the icon name

// Theme definitions
typedef enum {
    THEME_DARK,
    THEME_LIGHT_BLUE,
    THEME_LIGHT_MODE,
    THEME_GRAY
} TerminalTheme;

static GtkNotebook *notebook; // Global notebook reference
static TerminalTheme global_current_theme;
static char* global_shell_path;

static void apply_theme(VteTerminal *terminal, TerminalTheme theme);
// Forward declaration for the new tab creation function
static void create_new_terminal_tab(GtkNotebook *notebook_widget, TerminalTheme theme, const char *shell_path_arg);
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static void on_child_exit(VteTerminal *terminal, gint status, gpointer user_data);
static gchar* get_shell_path();
static void spawn_callback(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data);

int main(int argc, char *argv[]) {
    GtkWidget *window;
    // char *shell_path; // Now global_shell_path
    // char **command; // Will be set in create_new_terminal_tab
    // TerminalTheme current_theme = THEME_DARK; // Now global_current_theme

    // Initialize GTK (should be called before any GTK functions)
    gtk_init(&argc, &argv);

    global_current_theme = THEME_DARK; // Default theme

    // Parse command-line arguments for theme
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--theme") == 0 && i + 1 < argc) {
            i++; // Move to the theme name
            if (g_strcmp0(argv[i], "light-blue") == 0) {
                global_current_theme = THEME_LIGHT_BLUE;
            } else if (g_strcmp0(argv[i], "light") == 0) {
                global_current_theme = THEME_LIGHT_MODE;
            } else if (g_strcmp0(argv[i], "gray") == 0) {
                global_current_theme = THEME_GRAY;
            } else if (g_strcmp0(argv[i], "dark") == 0) {
                global_current_theme = THEME_DARK;
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

    // Set window icon
    GtkWindow *gtk_window = GTK_WINDOW(window);
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    if (gtk_icon_theme_has_icon(icon_theme, ICON_NAME)) {
        gtk_window_set_icon_name(gtk_window, ICON_NAME);
    } else {
        // Fallback if icon is not found in theme, try to load from file
        gchar *icon_path = g_build_filename(g_get_user_data_dir(), "icons", "hicolor", "48x48", "apps", ICON_NAME ".png", NULL);
        if (g_file_test(icon_path, G_FILE_TEST_EXISTS)) {
            gtk_window_set_icon_from_file(gtk_window, icon_path, NULL);
        } else {
            g_warning("Could not load icon: %s or find it in path: %s", ICON_NAME, icon_path);
        }
        g_free(icon_path);
    }

    // Create notebook
    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(notebook));
    gtk_notebook_set_scrollable(notebook, TRUE); // Allow scrolling if many tabs

    // Get the path to the sdn shell
    global_shell_path = get_shell_path();
    if (!global_shell_path) {
        g_error("Could not find sdn shell executable.");
        return 1;
    }

    // Create the first tab
    create_new_terminal_tab(notebook, global_current_theme, global_shell_path);

    // Show everything
    gtk_widget_show_all(window);

    // Main loop
    gtk_main();

    free(global_shell_path);
    return 0;
}

// Helper function to create and add a new terminal tab
static void create_new_terminal_tab(GtkNotebook *notebook_widget, TerminalTheme theme, const char *shell_path_arg) {
    GtkWidget *terminal_widget = vte_terminal_new();
    VteTerminal *vte_term = VTE_TERMINAL(terminal_widget);

    apply_theme(vte_term, theme);
    vte_terminal_set_scrollback_lines(vte_term, 10000);

    // Connect signals for this new terminal
    g_signal_connect(vte_term, "child-exited", G_CALLBACK(on_child_exit), NULL); // user_data is NULL
    g_signal_connect(vte_term, "key-press-event", G_CALLBACK(on_key_press), NULL); // user_data is NULL

    char **command = (char*[]){(char*)shell_path_arg, NULL};

    vte_terminal_spawn_async(
        vte_term,
        VTE_PTY_DEFAULT,
        NULL, command, NULL, G_SPAWN_DEFAULT,
        NULL, NULL, NULL, -1, NULL,
        (VteTerminalSpawnAsyncCallback)spawn_callback,
        gtk_widget_get_ancestor(GTK_WIDGET(notebook_widget), GTK_TYPE_WINDOW) // Pass main window to spawn_callback
    );

    // Create a label for the tab
    GtkWidget *tab_label = gtk_label_new("Terminal"); 

    // Add the terminal to the notebook
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_widget), terminal_widget, tab_label);
    gtk_widget_show_all(GTK_WIDGET(notebook_widget)); 
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook_widget), gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook_widget)) - 1);
    gtk_widget_grab_focus(terminal_widget);
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
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) { 
    (void)user_data; // Suppress unused parameter warning
    VteTerminal *current_terminal = VTE_TERMINAL(widget); // This is the terminal that received the key press
    GtkWidget *current_page_widget = GTK_WIDGET(current_terminal);

    // Ctrl+Shift+T - New Tab
    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) && event->keyval == GDK_KEY_T) {
        create_new_terminal_tab(notebook, global_current_theme, global_shell_path);
        return TRUE; // Event handled
    }

    // Ctrl+W - Close current tab
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_w) {
        gint page_num = gtk_notebook_page_num(notebook, current_page_widget);
        if (page_num != -1) {
            // We need to find the VteTerminal within the page to call vte_terminal_get_child_pid
            // and then potentially kill the process if it's still running.
            // For now, just remove the page. A more robust solution would handle child processes.
            gtk_notebook_remove_page(notebook, page_num);
            if (gtk_notebook_get_n_pages(notebook) == 0) {
                gtk_main_quit();
            }
        }
        return TRUE; // Event handled
    }

    // Alt + Number to switch tabs (1-9)
    if ((event->state & GDK_MOD1_MASK) && (event->keyval >= GDK_KEY_1 && event->keyval <= GDK_KEY_9)) {
        gint target_page = event->keyval - GDK_KEY_1;
        gint n_pages = gtk_notebook_get_n_pages(notebook);
        if (target_page < n_pages) {
            gtk_notebook_set_current_page(notebook, target_page);
        }
        return TRUE; // Event handled
    }
    // Alt + 0 to switch to the 10th tab (if it exists)
    if ((event->state & GDK_MOD1_MASK) && event->keyval == GDK_KEY_0) {
        gint target_page = 9; // 0 maps to the 10th tab (index 9)
        gint n_pages = gtk_notebook_get_n_pages(notebook);
        if (target_page < n_pages) {
            gtk_notebook_set_current_page(notebook, target_page);
        }
        return TRUE; // Event handled
    }

    // Ctrl+Shift+C - Copy
    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) && event->keyval == GDK_KEY_C) {
        g_print("Ctrl+Shift+C detected for copy\n");
        vte_terminal_copy_clipboard_format(current_terminal, VTE_FORMAT_TEXT);
        return TRUE; // Event handled
    }
    
    // Ctrl+Shift+V - Paste
    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) && event->keyval == GDK_KEY_V) {
        g_print("Ctrl+Shift+V detected for paste\n");
        vte_terminal_paste_clipboard(current_terminal);
        return TRUE; // Event handled
    }
    
    g_print("Key press not handled by custom shortcuts.\n");
    // Let the terminal handle other keypresses
    return FALSE;
}

// Handle terminal exit
static void on_child_exit(VteTerminal *exited_terminal, gint status, gpointer user_data) {
    (void)status;   // Suppress unused parameter warning
    (void)user_data; // Suppress unused parameter warning, as exited_terminal is the one we need.

    gint page_num = gtk_notebook_page_num(notebook, GTK_WIDGET(exited_terminal));
    if (page_num != -1) {
        gtk_notebook_remove_page(notebook, page_num);
    }

    if (gtk_notebook_get_n_pages(notebook) == 0) {
        gtk_main_quit();
    }
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
    (void)pid;      // Suppress unused parameter warning
    (void)user_data; // Suppress unused parameter warning
    // GtkWidget *window = GTK_WIDGET(user_data); // Main window passed as user_data
    
    if (error != NULL) {
        g_printerr("Error spawning terminal in tab: %s\n", error->message);
        g_error_free(error);
        
        // Find and remove the tab associated with this terminal
        gint page_num = gtk_notebook_page_num(notebook, GTK_WIDGET(terminal));
        if (page_num != -1) {
            gtk_notebook_remove_page(notebook, page_num);
        }
        
        if (gtk_notebook_get_n_pages(notebook) == 0) {
            // If it was the last tab (or the first and only tab failing), quit.
            // GtkWidget *main_window = GTK_WIDGET(user_data);
            // if (main_window) gtk_widget_destroy(main_window); // Could also destroy the window explicitly
            gtk_main_quit();
        }
    }
}
