#include <gtk/gtk.h>
#include "store.h"
#include "tray.h"

#define PANEL_W 400
#define PANEL_H 420
#define MARGIN  8
#define PAGE_CAPTURE SECTION_COUNT

typedef struct {
    Section section;
    GtkWidget *listbox;
    GtkWidget *entry;
} Page;

static GtkWidget *window;
static GtkWidget *notebook;
static GtkWidget *foot_label;
static gint64 last_hidden_us;
static Page pages[SECTION_COUNT];

static const char *EMPTY_TEXT[SECTION_COUNT] = {
    "No notes yet — jot down whatever's on your mind.",
    "No tasks yet — add what you're tackling today.",
    "Nothing saved yet — grab your clipboard or paste something.",
};

static const char *STYLE =
    "window { background: transparent; }"
    ".panel { background: #1f1b2b; border: 1px solid #3a3350; border-radius: 14px; }"
    ".panel, .panel label { color: #efeaf7; }"
    "notebook header { background: #1f1b2b; border-bottom: 1px solid #2c2740; }"
    "notebook header tab { color: #a79fc0; padding: 6px 7px; min-width: 0; }"
    "notebook header tab:checked { color: #a092f5; box-shadow: inset 0 -2px 0 #8c7cf0; }"
    "entry { background: #26213a; color: #efeaf7; border: 1px solid #3a3350; border-radius: 8px; }"
    "button.accent { background: #8c7cf0; color: #16101f; border: 0; border-radius: 8px; padding: 4px 12px; }"
    "button.accent:hover { background: #a092f5; }"
    "button.flat { background: transparent; border: 0; color: #766f8d;"
    "  padding: 2px 6px; min-width: 0; min-height: 0; }"
    "button.flat:hover { color: #efeaf7; }"
    "list, list row { background: transparent; }"
    "list row:hover { background: rgba(255,255,255,0.03); }"
    ".dim { color: #766f8d; font-size: 10px; }"
    ".foot { border-top: 1px solid #2c2740; color: #766f8d; font-size: 10px; }";

static void rebuild_list(Page *page);

static void refresh_status(void) {
    if (!foot_label)
        return;
    if (store_ok()) {
        gtk_label_set_text(GTK_LABEL(foot_label), "stored on this device");
    } else {
        gtk_label_set_markup(GTK_LABEL(foot_label),
            "<span foreground=\"#e88\">could not save — changes will be lost</span>");
    }
}

static void on_delete(GtkButton *button, gpointer data) {
    Page *page = data;
    store_remove(page->section, g_object_get_data(G_OBJECT(button), "item-id"));
    rebuild_list(page);
    refresh_status();
}

static void on_toggle(GtkToggleButton *check, gpointer data) {
    Page *page = data;
    const char *id = g_object_get_data(G_OBJECT(check), "item-id");
    GtkLabel *label = g_object_get_data(G_OBJECT(check), "item-label");
    const char *text = g_object_get_data(G_OBJECT(check), "item-text");

    store_toggle(page->section, id);
    refresh_status();

    g_autofree char *escaped = g_markup_escape_text(text, -1);
    g_autofree char *markup = gtk_toggle_button_get_active(check)
        ? g_strdup_printf("<s><span foreground=\"#766f8d\">%s</span></s>", escaped)
        : g_strdup(escaped);
    gtk_label_set_markup(label, markup);
}

static GtkWidget *build_row(Page *page, Item *item) {
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget *check = NULL;
    if (page->section == SECTION_TASKS) {
        check = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), item->done);
        gtk_widget_set_valign(check, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
    }

    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *label = gtk_label_new(NULL);
    g_autofree char *escaped = g_markup_escape_text(item->text, -1);
    g_autofree char *markup = item->done
        ? g_strdup_printf("<s><span foreground=\"#766f8d\">%s</span></s>", escaped)
        : g_strdup(escaped);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    /* A clipped URL or token has no spaces; word-only wrapping would make the label demand its
     * full single-line width and drag the whole window off screen. */
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 34);
    gtk_label_set_width_chars(GTK_LABEL(label), 1);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(text_box), label, FALSE, FALSE, 0);

    g_autoptr(GDateTime) when = g_date_time_new_from_unix_local(item->ts);
    g_autofree char *stamp = when ? g_date_time_format(when, "%H:%M") : NULL;
    GtkWidget *time_label = gtk_label_new(stamp ? stamp : "—");
    gtk_label_set_xalign(GTK_LABEL(time_label), 0.0);
    gtk_style_context_add_class(gtk_widget_get_style_context(time_label), "dim");
    gtk_box_pack_start(GTK_BOX(text_box), time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), text_box, TRUE, TRUE, 0);

    GtkWidget *remove = gtk_button_new_with_label("\xe2\x9c\x95");
    gtk_widget_set_valign(remove, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(remove, "Delete");
    atk_object_set_name(gtk_widget_get_accessible(remove), "Delete");
    gtk_style_context_add_class(gtk_widget_get_style_context(remove), "flat");
    g_object_set_data_full(G_OBJECT(remove), "item-id", g_strdup(item->id), g_free);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_delete), page);
    gtk_box_pack_start(GTK_BOX(box), remove, FALSE, FALSE, 0);

    if (check) {
        g_object_set_data_full(G_OBJECT(check), "item-id", g_strdup(item->id), g_free);
        g_object_set_data_full(G_OBJECT(check), "item-text", g_strdup(item->text), g_free);
        g_object_set_data(G_OBJECT(check), "item-label", label);
        g_signal_connect(check, "toggled", G_CALLBACK(on_toggle), page);
    }

    gtk_container_add(GTK_CONTAINER(row), box);
    return row;
}

static void rebuild_list(Page *page) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(page->listbox));
    g_list_free_full(children, (GDestroyNotify) gtk_widget_destroy);

    GList *items = store_items(page->section);
    if (!items) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        GtkWidget *label = gtk_label_new(EMPTY_TEXT[page->section]);
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_widget_set_margin_top(label, 14);
        gtk_widget_set_margin_bottom(label, 14);
        gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim");
        gtk_container_add(GTK_CONTAINER(row), label);
        gtk_container_add(GTK_CONTAINER(page->listbox), row);
    }

    /* Newest first, matching the original UI. */
    for (GList *l = g_list_last(items); l; l = l->prev)
        gtk_container_add(GTK_CONTAINER(page->listbox), build_row(page, l->data));

    gtk_widget_show_all(page->listbox);
}

static void commit_entry(Page *page) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(page->entry));
    g_autofree char *trimmed = g_strdup(text);
    g_strstrip(trimmed);
    if (!*trimmed)
        return;
    store_add(page->section, trimmed);
    gtk_entry_set_text(GTK_ENTRY(page->entry), "");
    rebuild_list(page);
    refresh_status();
}

static void on_add_clicked(GtkButton *button, gpointer data) {
    (void) button;
    commit_entry(data);
}

static void on_entry_activate(GtkEntry *entry, gpointer data) {
    (void) entry;
    commit_entry(data);
}

static void on_grab_clipboard(GtkButton *button, gpointer data) {
    (void) button;
    Page *page = data;
    g_autofree char *text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    if (!text) {
        gtk_widget_grab_focus(page->entry);
        return;
    }
    g_strstrip(text);
    if (*text) {
        store_add(page->section, text);
        rebuild_list(page);
        refresh_status();
    }
}

static GtkWidget *build_page(Section section, const char *placeholder) {
    Page *page = &pages[section];
    page->section = section;

    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(column), 10);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    page->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(page->entry), placeholder);
    gtk_entry_set_max_length(GTK_ENTRY(page->entry), 400);
    g_signal_connect(page->entry, "activate", G_CALLBACK(on_entry_activate), page);
    gtk_box_pack_start(GTK_BOX(row), page->entry, TRUE, TRUE, 0);

    GtkWidget *add = gtk_button_new_with_label(section == SECTION_CLIPS ? "Save" : "Add");
    gtk_style_context_add_class(gtk_widget_get_style_context(add), "accent");
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_clicked), page);
    gtk_box_pack_start(GTK_BOX(row), add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(column), row, FALSE, FALSE, 0);

    if (section == SECTION_CLIPS) {
        GtkWidget *grab = gtk_button_new_with_label("Grab clipboard");
        gtk_style_context_add_class(gtk_widget_get_style_context(grab), "accent");
        g_signal_connect(grab, "clicked", G_CALLBACK(on_grab_clipboard), page);
        gtk_box_pack_start(GTK_BOX(column), grab, FALSE, FALSE, 0);
    }

    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    page->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(page->listbox), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroller), page->listbox);
    gtk_box_pack_start(GTK_BOX(column), scroller, TRUE, TRUE, 0);

    rebuild_list(page);
    return column;
}

static GtkWidget *build_capture_page(void) {
    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(column), 10);

    GtkWidget *copy = gtk_label_new("Screenshot and screen recording — planned for a later version.");
    gtk_label_set_line_wrap(GTK_LABEL(copy), TRUE);
    gtk_label_set_xalign(GTK_LABEL(copy), 0.0);
    gtk_style_context_add_class(gtk_widget_get_style_context(copy), "dim");
    gtk_box_pack_start(GTK_BOX(column), copy, FALSE, FALSE, 0);

    const char *labels[] = { "Capture screenshot", "Record screen" };
    for (int i = 0; i < 2; i++) {
        GtkWidget *button = gtk_button_new_with_label(labels[i]);
        gtk_widget_set_sensitive(button, FALSE);
        gtk_box_pack_start(GTK_BOX(column), button, FALSE, FALSE, 0);
    }
    return column;
}

static void position_window(void) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    if (!monitor)
        monitor = gdk_display_get_monitor(display, 0);
    if (!monitor)
        return;

    GdkRectangle area;
    gdk_monitor_get_workarea(monitor, &area);

    /* Use the real size: GTK gives a non-resizable window max(natural, default), so a wider
     * natural size would otherwise push the panel off the right edge. */
    int width = PANEL_W, height = PANEL_H;
    gtk_window_get_size(GTK_WINDOW(window), &width, &height);

    gtk_window_move(GTK_WINDOW(window),
                    area.x + area.width - width - MARGIN,
                    area.y + MARGIN);
}

static void show_panel(int page) {
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page);
    position_window();
    gtk_widget_show(window);
    gtk_window_present(GTK_WINDOW(window));
}

static void on_close_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    gtk_widget_hide(window);
}

/* Double-clicking the tray icon lands here: open straight onto the last-used section. */
static void on_tray_activate(gpointer data) {
    (void) data;
    if (gtk_widget_get_visible(window)) {
        gtk_widget_hide(window);
        return;
    }
    /* Clicking the tray icon moves focus away first, so on_focus_out has usually already hidden
     * the panel by the time Activate arrives. Without this the click would re-open it and the
     * icon could never close the panel. */
    if (g_get_monotonic_time() - last_hidden_us < 250000)
        return;
    show_panel(gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook)));
}

static void on_tray_menu(int item_id, gpointer data) {
    (void) data;
    if (item_id == TRAY_MENU_QUIT)
        gtk_main_quit();
    else
        show_panel(gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook)));
}

static gboolean on_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void) event;
    (void) data;
    last_hidden_us = g_get_monotonic_time();
    gtk_widget_hide(widget);
    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void) data;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(widget);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void) event;
    (void) data;
    gtk_widget_hide(widget);
    return TRUE;
}

int main(int argc, char **argv) {
    /* Wayland forbids a client from positioning its own window or raising itself above others,
     * so the panel would open wherever the compositor liked. Must precede gtk_init.
     * ponytail: Xwayland dependency; a GNOME Shell extension is the upgrade path if it is dropped. */
    gdk_set_allowed_backends("x11,*");
    gtk_init(&argc, &argv);

    store_load();

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, STYLE, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Nook");
    gtk_window_set_default_size(GTK_WINDOW(window), PANEL_W, PANEL_H);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_UTILITY);

    GdkVisual *rgba = gdk_screen_get_rgba_visual(gdk_screen_get_default());
    if (rgba)
        gtk_widget_set_visual(window, rgba);
    gtk_widget_set_app_paintable(window, TRUE);

    g_signal_connect(window, "focus-out-event", G_CALLBACK(on_focus_out), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);

    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(panel), "panel");
    gtk_container_add(GTK_CONTAINER(window), panel);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(panel), notebook, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_page(SECTION_NOTES, "Quick note…"), gtk_label_new("Notes"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_page(SECTION_TASKS, "New task…"), gtk_label_new("Tasks"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_page(SECTION_CLIPS, "Paste text…"), gtk_label_new("Clips"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_capture_page(), gtk_label_new("Capture"));

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *close_button = gtk_button_new_with_label("\xe2\x9c\x95");
    gtk_widget_set_tooltip_text(close_button, "Close (Esc)");
    atk_object_set_name(gtk_widget_get_accessible(close_button), "Close");
    gtk_style_context_add_class(gtk_widget_get_style_context(close_button), "flat");
    g_signal_connect(close_button, "clicked", G_CALLBACK(on_close_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(actions), close_button, FALSE, FALSE, 0);

    gtk_widget_set_margin_end(actions, 6);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(notebook), actions, GTK_PACK_END);
    /* Notebook action widgets are internal children, so gtk_widget_show_all() on an ancestor
     * does not reach them; they must be shown explicitly or they never appear. */
    gtk_widget_show_all(actions);

    GtkWidget *foot = gtk_label_new("stored on this device");
    foot_label = foot;
    gtk_widget_set_margin_top(foot, 6);
    gtk_widget_set_margin_bottom(foot, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(foot), "foot");
    gtk_box_pack_start(GTK_BOX(panel), foot, FALSE, FALSE, 0);

    gtk_widget_show_all(panel);
    refresh_status();

    tray_init("nook", NOOK_ICON_DIR, "Nook", on_tray_activate, on_tray_menu, NULL);

    gtk_main();

    store_free();
    return 0;
}
