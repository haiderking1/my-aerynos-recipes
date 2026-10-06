/*
 * audio-panel - a small macOS-style sound menu for Waybar.
 *
 * Shows output and input devices with volume sliders. Click a device to make
 * it the default. Talks to PipeWire through wpctl. Running it again while it
 * is open closes it, so it works as a toggle from a Waybar on-click.
 */

#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <string.h>

#define ICON_SPEAKER     "\U000F057E"
#define ICON_SPEAKER_OFF "\U000F0581"
#define ICON_MIC         "\U000F036C"
#define ICON_MIC_OFF     "\U000F036D"
#define ICON_CHECK       "\U000F012C"

typedef struct {
    int id;
    char *name;
    double volume;
    gboolean muted;
    gboolean is_default;
} Device;

typedef struct {
    const char *title;
    const char *icon_on;
    const char *icon_off;
    GPtrArray *devices;
    int default_id;
    GtkWidget *mute_button;
    GtkWidget *scale;
    GtkWidget *list;
} Section;

static GtkWidget *window;
static Section outputs = { "Output", ICON_SPEAKER, ICON_SPEAKER_OFF };
static Section inputs = { "Input", ICON_MIC, ICON_MIC_OFF };
static gboolean updating;
static gboolean was_active;

static const char *CSS =
    "window { background: transparent; }"
    "* { font-family: \"Adwaita Sans\", \"Maple Mono NF\"; color: #f5f5f7; font-size: 14px; }"
    ".panel { background: #000000; border: 1px solid #2a2a2a; border-radius: 14px; padding: 12px; }"
    ".title { font-weight: bold; font-size: 12px; color: #8e8e93; margin: 4px 6px 2px 6px; }"
    ".icon { font-family: \"Maple Mono NF\"; font-size: 18px; }"
    ".icon-button { background: transparent; border: none; box-shadow: none; min-width: 34px; padding: 2px 4px; border-radius: 8px; }"
    ".icon-button:hover { background: #1c1c1e; }"
    ".muted .icon { color: #6e6e73; }"
    ".device { background: transparent; border: none; box-shadow: none; padding: 7px 8px; border-radius: 8px; }"
    ".device:hover { background: #1c1c1e; }"
    ".device .check { font-family: \"Maple Mono NF\"; color: #f5f5f7; }"
    ".device label.name { color: #c7c7cc; }"
    ".device.selected label.name { color: #f5f5f7; font-weight: bold; }"
    "scale { padding: 8px 6px; }"
    "scale trough { background: #2c2c2e; min-height: 6px; border-radius: 3px; border: none; }"
    "scale highlight { background: #f5f5f7; border-radius: 3px; border: none; margin: 0; padding: 0; min-height: 6px; }"
    "scale slider { background: #f5f5f7; min-width: 16px; min-height: 16px; margin: -6px; border-radius: 9999px; border: none; box-shadow: none; }"
    "separator { background: #2a2a2a; min-height: 1px; margin: 8px 4px; }";

static void
device_free (gpointer data)
{
    Device *dev = data;
    g_free (dev->name);
    g_free (dev);
}

/* Drop the noisy suffixes PipeWire puts on device names */
static char *
short_name (const char *name)
{
    static const char *suffixes[] = {
        " Digital Stereo (HDMI)", " Analog Stereo", " Digital Stereo", " Mono", NULL,
    };
    char *result = g_strdup (name);

    for (int i = 0; suffixes[i]; i++) {
        if (g_str_has_suffix (result, suffixes[i])) {
            result[strlen (result) - strlen (suffixes[i])] = '\0';
            break;
        }
    }
    return result;
}

static void
run (const char *cmdline, gboolean wait)
{
    GError *error = NULL;

    if (wait)
        g_spawn_command_line_sync (cmdline, NULL, NULL, NULL, &error);
    else
        g_spawn_command_line_async (cmdline, &error);

    if (error) {
        g_warning ("%s: %s", cmdline, error->message);
        g_error_free (error);
    }
}

/* Parse the Audio sinks and sources out of `wpctl status` */
static void
read_devices (void)
{
    char *out = NULL;
    GError *error = NULL;
    GRegex *re;
    char **lines;
    GPtrArray *current = NULL;
    gboolean in_audio = FALSE;

    g_ptr_array_set_size (outputs.devices, 0);
    g_ptr_array_set_size (inputs.devices, 0);
    outputs.default_id = inputs.default_id = -1;

    if (!g_spawn_command_line_sync ("wpctl status", &out, NULL, NULL, &error)) {
        g_warning ("wpctl status: %s", error->message);
        g_error_free (error);
        return;
    }

    re = g_regex_new ("(\\*)?\\s+(\\d+)\\.\\s+(.+?)\\s+\\[vol:\\s*([0-9.]+)(\\s+MUTED)?\\]",
                      0, 0, NULL);
    lines = g_strsplit (out, "\n", -1);

    for (int i = 0; lines[i]; i++) {
        const char *line = lines[i];
        GMatchInfo *match = NULL;

        if (g_str_equal (line, "Audio")) {
            in_audio = TRUE;
            continue;
        }
        if (!in_audio)
            continue;
        if (g_str_equal (line, "Video") || g_str_equal (line, "Settings"))
            break;

        if (strstr (line, "Sinks:"))
            current = outputs.devices;
        else if (strstr (line, "Sources:"))
            current = inputs.devices;
        else if (strstr (line, "Devices:") || strstr (line, "Filters:") || strstr (line, "Streams:"))
            current = NULL;
        else if (current && g_regex_match (re, line, 0, &match)) {
            Device *dev = g_new0 (Device, 1);
            char *star = g_match_info_fetch (match, 1);
            char *id = g_match_info_fetch (match, 2);
            char *name = g_match_info_fetch (match, 3);
            char *vol = g_match_info_fetch (match, 4);
            char *muted = g_match_info_fetch (match, 5);

            dev->id = atoi (id);
            dev->name = short_name (name);
            dev->volume = g_ascii_strtod (vol, NULL);
            dev->muted = muted && *muted;
            dev->is_default = star && *star;
            g_ptr_array_add (current, dev);

            if (dev->is_default) {
                if (current == outputs.devices)
                    outputs.default_id = dev->id;
                else
                    inputs.default_id = dev->id;
            }

            g_free (star);
            g_free (id);
            g_free (name);
            g_free (vol);
            g_free (muted);
        }
        g_clear_pointer (&match, g_match_info_free);
    }

    g_strfreev (lines);
    g_regex_unref (re);
    g_free (out);
}

static void refresh (void);

static void
on_device_clicked (GtkButton *button, gpointer user_data)
{
    int id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "device-id"));
    char *cmd = g_strdup_printf ("wpctl set-default %d", id);

    run (cmd, TRUE);
    g_free (cmd);
    refresh ();
}

static void
on_mute_clicked (GtkButton *button, gpointer user_data)
{
    Section *section = user_data;
    char *cmd;

    if (section->default_id < 0)
        return;

    cmd = g_strdup_printf ("wpctl set-mute %d toggle", section->default_id);
    run (cmd, TRUE);
    g_free (cmd);
    refresh ();
}

static void
on_volume_changed (GtkRange *range, gpointer user_data)
{
    Section *section = user_data;
    char value[G_ASCII_DTOSTR_BUF_SIZE];
    char *cmd;

    if (updating || section->default_id < 0)
        return;

    g_ascii_formatd (value, sizeof value, "%.2f", gtk_range_get_value (range) / 100.0);
    cmd = g_strdup_printf ("wpctl set-volume %d %s", section->default_id, value);
    run (cmd, FALSE);
    g_free (cmd);
}

static void
fill_section (Section *section)
{
    GtkWidget *child;
    Device *def = NULL;

    while ((child = gtk_widget_get_first_child (section->list)))
        gtk_box_remove (GTK_BOX (section->list), child);

    for (guint i = 0; i < section->devices->len; i++) {
        Device *dev = g_ptr_array_index (section->devices, i);
        GtkWidget *button = gtk_button_new ();
        GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *check = gtk_label_new (dev->is_default ? ICON_CHECK : "");
        GtkWidget *name = gtk_label_new (dev->name);

        gtk_widget_add_css_class (button, "device");
        gtk_widget_add_css_class (check, "check");
        gtk_widget_add_css_class (name, "name");
        gtk_widget_set_size_request (check, 18, -1);
        gtk_label_set_xalign (GTK_LABEL (name), 0);
        gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand (name, TRUE);

        gtk_box_append (GTK_BOX (row), check);
        gtk_box_append (GTK_BOX (row), name);
        gtk_button_set_child (GTK_BUTTON (button), row);

        if (dev->is_default) {
            gtk_widget_add_css_class (button, "selected");
            def = dev;
        }

        g_object_set_data (G_OBJECT (button), "device-id", GINT_TO_POINTER (dev->id));
        g_signal_connect (button, "clicked", G_CALLBACK (on_device_clicked), NULL);
        gtk_box_append (GTK_BOX (section->list), button);
    }

    updating = TRUE;
    gtk_range_set_value (GTK_RANGE (section->scale), def ? def->volume * 100.0 : 0);
    updating = FALSE;

    gtk_widget_set_sensitive (section->scale, def != NULL);
    gtk_label_set_text (GTK_LABEL (gtk_button_get_child (GTK_BUTTON (section->mute_button))),
                        def && def->muted ? section->icon_off : section->icon_on);
    if (def && def->muted)
        gtk_widget_add_css_class (section->mute_button, "muted");
    else
        gtk_widget_remove_css_class (section->mute_button, "muted");
}

static void
refresh (void)
{
    read_devices ();
    fill_section (&outputs);
    fill_section (&inputs);
}

static GtkWidget *
build_section (Section *section)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title = gtk_label_new (section->title);
    GtkWidget *slider_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *icon = gtk_label_new (section->icon_on);

    gtk_widget_add_css_class (title, "title");
    gtk_label_set_xalign (GTK_LABEL (title), 0);

    section->mute_button = gtk_button_new ();
    gtk_widget_add_css_class (section->mute_button, "icon-button");
    gtk_widget_add_css_class (icon, "icon");
    gtk_button_set_child (GTK_BUTTON (section->mute_button), icon);
    g_signal_connect (section->mute_button, "clicked", G_CALLBACK (on_mute_clicked), section);

    section->scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value (GTK_SCALE (section->scale), FALSE);
    gtk_widget_set_hexpand (section->scale, TRUE);
    g_signal_connect (section->scale, "value-changed", G_CALLBACK (on_volume_changed), section);

    section->list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);

    gtk_box_append (GTK_BOX (slider_row), section->mute_button);
    gtk_box_append (GTK_BOX (slider_row), section->scale);
    gtk_box_append (GTK_BOX (box), title);
    gtk_box_append (GTK_BOX (box), slider_row);
    gtk_box_append (GTK_BOX (box), section->list);
    return box;
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode,
                GdkModifierType state, gpointer user_data)
{
    if (keyval == GDK_KEY_Escape) {
        gtk_window_close (GTK_WINDOW (window));
        return TRUE;
    }
    return FALSE;
}

/* Close when focus moves to another window, like a real menu */
static void
on_active_changed (GObject *object, GParamSpec *pspec, gpointer user_data)
{
    if (gtk_window_is_active (GTK_WINDOW (window)))
        was_active = TRUE;
    else if (was_active)
        gtk_window_close (GTK_WINDOW (window));
}

static void
on_activate (GtkApplication *app, gpointer user_data)
{
    GtkCssProvider *css;
    GtkWidget *panel;
    GtkEventController *keys;

    /* Second launch while open: act as a toggle and close */
    if (window) {
        gtk_window_close (GTK_WINDOW (window));
        return;
    }

    outputs.devices = g_ptr_array_new_with_free_func (device_free);
    inputs.devices = g_ptr_array_new_with_free_func (device_free);

    css = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (css, CSS);
    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (css),
                                                GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_set (gtk_settings_get_default (), "gtk-interface-color-scheme",
                  GTK_INTERFACE_COLOR_SCHEME_DARK, NULL);

    window = gtk_application_window_new (app);
    gtk_window_set_default_size (GTK_WINDOW (window), 340, -1);

    gtk_layer_init_for_window (GTK_WINDOW (window));
    gtk_layer_set_namespace (GTK_WINDOW (window), "audio-panel");
    gtk_layer_set_layer (GTK_WINDOW (window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor (GTK_WINDOW (window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor (GTK_WINDOW (window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin (GTK_WINDOW (window), GTK_LAYER_SHELL_EDGE_TOP, 6);
    gtk_layer_set_margin (GTK_WINDOW (window), GTK_LAYER_SHELL_EDGE_RIGHT, 8);
    gtk_layer_set_keyboard_mode (GTK_WINDOW (window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    panel = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (panel, "panel");
    gtk_box_append (GTK_BOX (panel), build_section (&outputs));
    gtk_box_append (GTK_BOX (panel), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append (GTK_BOX (panel), build_section (&inputs));
    gtk_window_set_child (GTK_WINDOW (window), panel);

    keys = gtk_event_controller_key_new ();
    g_signal_connect (keys, "key-pressed", G_CALLBACK (on_key_pressed), NULL);
    gtk_widget_add_controller (window, keys);
    g_signal_connect (window, "notify::is-active", G_CALLBACK (on_active_changed), NULL);

    refresh ();
    gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char **argv)
{
    GtkApplication *app;
    int status;

    /* The GPU renderer costs ~300ms to initialise, cairo is plenty for a
     * small panel. Portals are skipped as the panel needs none of them. */
    g_setenv ("GSK_RENDERER", "cairo", FALSE);
    g_setenv ("GDK_DEBUG", "no-portals", FALSE);

    app = gtk_application_new ("dev.soka.AudioPanel", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
    status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    return status;
}
