#include <gtk/gtk.h>
#include <string.h>
#include "store.h"
#include "tray.h"

#define PANEL_W 400
#define PANEL_H 420
#define MARGIN  8
/* A copy big enough to be a whole file was almost certainly wanted once; auto-capture leaves it
 * alone and Grab clipboard stays available for the times it was not. */
#define CLIP_AUTO_MAX_BYTES 8192
#define CLIP_PREVIEW_CHARS  160
#define CLIP_PREVIEW_LINES  3
#define COPY_FLASH_MS       1500
#define COPY_ICON           "edit-copy-symbolic"
#define COPIED_ICON         "object-select-symbolic"

typedef struct {
    Section section;
    GtkWidget *listbox;
    GtkWidget *entry;
    gboolean starred_only;
} Page;

static GtkWidget *window;
static GtkWidget *notebook;
static GtkWidget *foot_label;
static gint64 last_hidden_us;
static char *last_capture;
static char *skipped_digest;
static GtkWidget *copied_button;
static guint copied_timeout;
static guint clipboard_generation;
static Page pages[SECTION_COUNT];

static const char *EMPTY_TEXT[SECTION_COUNT] = {
    "No notes yet — wrote down whatever's on your mind.",
    "No tasks yet — add what you're tackling today.",
    "Nothing saved yet — grab your clipboard or paste something.",
};

static const char *EMPTY_STARRED = "No starred clips — press ☆ on a clip to keep it.";

static const char *STYLE =
    "window { background: transparent; }"
    ".panel { background: #1f1b2b; border: 1px solid #3a3350; border-radius: 14px; }"
    ".panel, .panel label { color: #efeaf7; }"
    /* The theme's own background and 1px border on the notebook painted across .panel's rounded
     * top corners (the footer has neither, which is why only the top two looked cut off) and left
     * the content area a different grey from the tab row. Clearing both leaves one flat panel. */
    "notebook, notebook > stack { background: transparent; border: 0; }"
    "notebook header { background: transparent; border: 0; border-bottom: 1px solid #2c2740; }"
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
    ".day { color: #a79fc0; font-size: 10px; font-weight: bold; letter-spacing: 1px; }"
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

static void set_clipboard(const char *text) {
    g_free(last_capture);
    last_capture = g_strdup(text);
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    /* Without this Nook owns the selection in-process only, so quitting takes the copy with it. */
    gtk_clipboard_set_can_store(clipboard, NULL, 0);
}

/* The tray icon already taught this project that a missing icon shows up as a placeholder rather
 * than an error, so a theme without the symbolic icon gets a word instead. */
static void set_button_icon(GtkButton *button, const char *icon_name, const char *fallback) {
    if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), icon_name)) {
        gtk_button_set_image(button, gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU));
        gtk_button_set_always_show_image(button, TRUE);
        /* A theme with only one of the two icons would otherwise leave the fallback text beside
         * the icon for good. */
        gtk_button_set_label(button, NULL);
    } else {
        gtk_button_set_image(button, NULL);
        gtk_button_set_label(button, fallback);
    }
}

static void describe_button(GtkWidget *button, const char *name) {
    gtk_widget_set_tooltip_text(button, name);
    atk_object_set_name(gtk_widget_get_accessible(button), name);
}

/* Dropped without touching the widget: rebuild_list is about to destroy it. */
static void cancel_copied_flash(void) {
    if (!copied_timeout)
        return;
    g_source_remove(copied_timeout);
    copied_timeout = 0;
    g_clear_object(&copied_button);
}

static gboolean clear_copied_flash(gpointer data) {
    (void) data;
    if (copied_button) {
        set_button_icon(GTK_BUTTON(copied_button), COPY_ICON, "Copy");
        describe_button(copied_button, "Copy");
        g_object_unref(copied_button);
        copied_button = NULL;
    }
    copied_timeout = 0;
    refresh_status();
    return G_SOURCE_REMOVE;
}

static void on_copy(GtkButton *button, gpointer data) {
    (void) data;
    set_clipboard(g_object_get_data(G_OBJECT(button), "item-text"));

    /* A second copy takes the flash over, rather than letting the first timer cut it short. */
    if (copied_timeout) {
        g_source_remove(copied_timeout);
        copied_timeout = 0;
        clear_copied_flash(NULL);
    }

    copied_button = g_object_ref(GTK_WIDGET(button));
    set_button_icon(button, COPIED_ICON, "\xe2\x9c\x93");
    describe_button(GTK_WIDGET(button), "Copied");
    gtk_label_set_text(GTK_LABEL(foot_label), "Copied to clipboard");
    copied_timeout = g_timeout_add(COPY_FLASH_MS, clear_copied_flash, NULL);
}

static void on_keep(GtkButton *button, gpointer data) {
    Page *page = data;
    store_toggle(page->section, g_object_get_data(G_OBJECT(button), "item-id"));
    rebuild_list(page);
    refresh_status();
}

/* The desktop's 12h/24h choice, resolved once. GSettings ships with GLib, so this adds no
 * dependency, and a system without the schema falls back to 24h. */
static const char *clock_format(void) {
    static const char *format;
    if (format)
        return format;

    format = "%H:%M";
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = source
        ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE)
        : NULL;
    if (schema && g_settings_schema_has_key(schema, "clock-format")) {
        g_autoptr(GSettings) settings = g_settings_new("org.gnome.desktop.interface");
        g_autofree char *value = g_settings_get_string(settings, "clock-format");
        if (g_strcmp0(value, "12h") == 0)
            format = "%l:%M %p";
    }
    return format;
}

static char *format_clock(GDateTime *when) {
    if (!when)
        return NULL;
    g_autofree char *stamp = g_date_time_format(when, clock_format());
    if (!stamp)
        return NULL;

    /* glib pads a single-digit %l hour with a figure space (U+2007), which g_strstrip leaves. */
    const char *start = stamp;
    while (*start && (g_ascii_isspace(*start) || g_str_has_prefix(start, "\xe2\x80\x87")))
        start = g_utf8_next_char(start);
    return g_strchomp(g_strdup(start));
}

static gint days_ago(GDateTime *when) {
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    GDate then_day, today;
    g_date_clear(&then_day, 1);
    g_date_clear(&today, 1);
    g_date_set_dmy(&then_day, g_date_time_get_day_of_month(when),
                   g_date_time_get_month(when), g_date_time_get_year(when));
    g_date_set_dmy(&today, g_date_time_get_day_of_month(now),
                   g_date_time_get_month(now), g_date_time_get_year(now));
    return g_date_days_between(&then_day, &today);
}

static GtkWidget *build_heading_row(GDateTime *when) {
    gint elapsed = when ? days_ago(when) : -1;
    g_autofree char *date = NULL;
    if (elapsed == 0)
        date = g_strdup("Today");
    else if (elapsed == 1)
        date = g_strdup("Yesterday");
    else
        date = when ? g_date_time_format(when, "%x") : g_strdup("Earlier");

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    GtkWidget *label = gtk_label_new(date);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_top(label, 8);
    gtk_widget_set_margin_bottom(label, 2);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "day");
    gtk_container_add(GTK_CONTAINER(row), label);
    return row;
}

/* NULL when the whole text fits, so callers can tell a preview from the real thing. */
static char *clip_preview(const char *text) {
    const char *end = text;
    int lines = 1;
    glong chars = 0;

    while (*end && chars < CLIP_PREVIEW_CHARS) {
        if (*end == '\n' && ++lines > CLIP_PREVIEW_LINES)
            break;
        end = g_utf8_next_char(end);
        chars++;
    }
    if (!*end)
        return NULL;

    g_autofree char *head = g_strndup(text, end - text);
    return g_strconcat(g_strchomp(head), "\xe2\x80\xa6", NULL);
}

static GtkWidget *row_button(const char *glyph, const char *name, const char *id,
                             GCallback callback, gpointer data) {
    GtkWidget *button = glyph ? gtk_button_new_with_label(glyph) : gtk_button_new();
    gtk_widget_set_valign(button, GTK_ALIGN_START);
    describe_button(button, name);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "flat");
    g_object_set_data_full(G_OBJECT(button), "item-id", g_strdup(id), g_free);
    g_signal_connect(button, "clicked", callback, data);
    return button;
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
    g_autofree char *preview = page->section == SECTION_CLIPS ? clip_preview(item->text) : NULL;
    g_autofree char *escaped = g_markup_escape_text(preview ? preview : item->text, -1);
    g_autofree char *markup = item->done && page->section == SECTION_TASKS
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
    g_autofree char *stamp = format_clock(when);
    const char *shown = stamp ? stamp : "—";
    g_autofree char *detail = NULL;
    if (preview) {
        int lines = 1;
        for (const char *c = item->text; *c; c++)
            if (*c == '\n')
                lines++;
        detail = lines > 1
            ? g_strdup_printf("%s · %d lines", shown, lines)
            : g_strdup_printf("%s · %ld chars", shown, g_utf8_strlen(item->text, -1));
    }
    GtkWidget *time_label = gtk_label_new(detail ? detail : shown);
    gtk_label_set_xalign(GTK_LABEL(time_label), 0.0);
    gtk_style_context_add_class(gtk_widget_get_style_context(time_label), "dim");
    gtk_box_pack_start(GTK_BOX(text_box), time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), text_box, TRUE, TRUE, 0);

    if (page->section == SECTION_CLIPS) {
        GtkWidget *keep = row_button(item->done ? "\xe2\x98\x85" : "\xe2\x98\x86",
                                     item->done ? "Kept" : "Keep",
                                     item->id, G_CALLBACK(on_keep), page);
        gtk_box_pack_start(GTK_BOX(box), keep, FALSE, FALSE, 0);

        GtkWidget *copy = row_button(NULL, "Copy", item->id, G_CALLBACK(on_copy), page);
        set_button_icon(GTK_BUTTON(copy), COPY_ICON, "Copy");
        g_object_set_data_full(G_OBJECT(copy), "item-text", g_strdup(item->text), g_free);
        gtk_box_pack_start(GTK_BOX(box), copy, FALSE, FALSE, 0);
    }

    GtkWidget *remove = row_button("\xe2\x9c\x95", "Delete", item->id, G_CALLBACK(on_delete), page);
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
    cancel_copied_flash();

    GList *children = gtk_container_get_children(GTK_CONTAINER(page->listbox));
    g_list_free_full(children, (GDestroyNotify) gtk_widget_destroy);

    /* Newest first, matching the original UI. */
    int shown = 0;
    int heading_day = 0;
    for (GList *l = g_list_last(store_items(page->section)); l; l = l->prev) {
        Item *item = l->data;
        if (page->starred_only && !item->done)
            continue;

        g_autoptr(GDateTime) when = g_date_time_new_from_unix_local(item->ts);
        int day = when ? g_date_time_get_year(when) * 10000 + g_date_time_get_month(when) * 100
                             + g_date_time_get_day_of_month(when)
                       : 0;
        if (day != heading_day) {
            heading_day = day;
            gtk_container_add(GTK_CONTAINER(page->listbox), build_heading_row(when));
        }

        gtk_container_add(GTK_CONTAINER(page->listbox), build_row(page, item));
        shown++;
    }

    if (!shown) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        GtkWidget *label = gtk_label_new(page->starred_only ? EMPTY_STARRED : EMPTY_TEXT[page->section]);
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_widget_set_margin_top(label, 14);
        gtk_widget_set_margin_bottom(label, 14);
        gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim");
        gtk_container_add(GTK_CONTAINER(row), label);
        gtk_container_add(GTK_CONTAINER(page->listbox), row);
    }

    gtk_widget_show_all(page->listbox);
}

static void commit_entry(Page *page) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(page->entry));
    g_autofree char *trimmed = g_strdup(text);
    g_strstrip(trimmed);
    if (!*trimmed)
        return;
    store_add(page->section, trimmed, page->section == SECTION_CLIPS);
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
        g_free(last_capture);
        last_capture = g_strdup(text);
        store_add(page->section, text, TRUE);
        rebuild_list(page);
        refresh_status();
    }
}

static void on_starred_toggled(GtkToggleButton *toggle, gpointer data) {
    Page *page = data;
    page->starred_only = gtk_toggle_button_get_active(toggle);
    rebuild_list(page);
}

static void on_auto_toggled(GtkToggleButton *toggle, gpointer data) {
    (void) data;
    store_set_auto_clips(gtk_toggle_button_get_active(toggle));
    refresh_status();
}

/* The desktop's clipboard manager takes over when the owning app quits and re-announces the same
 * content without the password hint, so a secret is remembered by digest and skipped again rather
 * than kept around in full. */
static void on_secret_text(GtkClipboard *clipboard, const char *text, gpointer data) {
    (void) clipboard;
    if (!text || GPOINTER_TO_UINT(data) != clipboard_generation)
        return;
    g_free(skipped_digest);
    skipped_digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, -1);
}

static void on_clipboard_text(GtkClipboard *clipboard, const char *text, gpointer data) {
    (void) clipboard;
    if (!text)
        return;

    /* The targets check and this fetch are separate round-trips. A reply that belongs to a
     * superseded owner would pair one clip's hint with another clip's text — which is how a
     * password reaches data.ini, or an innocent clip gets blacklisted as a secret. */
    if (GPOINTER_TO_UINT(data) != clipboard_generation)
        return;

    if (skipped_digest) {
        g_autofree char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, -1);
        if (g_strcmp0(digest, skipped_digest) == 0)
            return;
    }

    g_autofree char *trimmed = g_strdup(text);
    g_strstrip(trimmed);
    if (!*trimmed || strlen(trimmed) > CLIP_AUTO_MAX_BYTES || g_strcmp0(trimmed, last_capture) == 0)
        return;

    g_free(last_capture);
    last_capture = g_strdup(trimmed);
    store_add(SECTION_CLIPS, trimmed, FALSE);
    if (gtk_widget_get_visible(window))
        rebuild_list(&pages[SECTION_CLIPS]);
    refresh_status();
}

static void on_clipboard_targets(GtkClipboard *clipboard, GdkAtom *atoms, gint count, gpointer data) {
    /* A failed TARGETS conversion arrives as count == -1, which would otherwise walk past the
     * password check and capture the clip regardless. The next owner-change tries again. */
    if (count < 0 || GPOINTER_TO_UINT(data) != clipboard_generation)
        return;

    for (gint i = 0; i < count; i++) {
        /* Password managers tag their copies with this hint; capturing one would leave a secret
         * sitting in a plain-text file. */
        g_autofree char *name = gdk_atom_name(atoms[i]);
        if (name && strstr(name, "passwordManagerHint")) {
            gtk_clipboard_request_text(clipboard, on_secret_text, data);
            return;
        }
    }
    gtk_clipboard_request_text(clipboard, on_clipboard_text, data);
}

/* Asynchronous on purpose: this runs on the main loop the moment ownership changes, and a
 * blocking gtk_clipboard_wait_for_text() here stalls the UI until the new owner answers. */
static void on_clipboard_owner_change(GtkClipboard *clipboard, GdkEvent *event, gpointer data) {
    (void) event;
    (void) data;
    if (!store_auto_clips())
        return;
    /* Ctrl+C inside the panel is a copy *of* a clip, not a new one, and rebuilding the list under
     * the selection that made it would throw away what the user was reading. */
    if (gtk_widget_get_visible(window) && gtk_window_is_active(GTK_WINDOW(window)))
        return;
    clipboard_generation++;
    gtk_clipboard_request_targets(clipboard, on_clipboard_targets,
                                  GUINT_TO_POINTER(clipboard_generation));
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
        GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        GtkWidget *grab = gtk_button_new_with_label("Grab clipboard");
        gtk_style_context_add_class(gtk_widget_get_style_context(grab), "accent");
        g_signal_connect(grab, "clicked", G_CALLBACK(on_grab_clipboard), page);
        gtk_box_pack_start(GTK_BOX(tools), grab, FALSE, FALSE, 0);

        GtkWidget *auto_save = gtk_check_button_new_with_label("Auto-save");
        gtk_widget_set_tooltip_text(auto_save, "Save every copy automatically");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(auto_save), store_auto_clips());
        g_signal_connect(auto_save, "toggled", G_CALLBACK(on_auto_toggled), NULL);
        gtk_box_pack_start(GTK_BOX(tools), auto_save, FALSE, FALSE, 0);

        GtkWidget *starred = gtk_check_button_new_with_label("Starred");
        gtk_widget_set_tooltip_text(starred, "Show kept clips only");
        g_signal_connect(starred, "toggled", G_CALLBACK(on_starred_toggled), page);
        gtk_box_pack_start(GTK_BOX(tools), starred, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(column), tools, FALSE, FALSE, 0);
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
    /* Headings are relative to today, so a panel opened the next morning has to redraw all three
     * lists, not just the one auto-capture touches. */
    for (int s = 0; s < SECTION_COUNT; s++)
        rebuild_list(&pages[s]);
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
    if (argc > 1 && (g_strcmp0(argv[1], "--version") == 0 || g_strcmp0(argv[1], "-v") == 0)) {
        g_print("nook %s\n", NOOK_VERSION);
        return 0;
    }

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
    gtk_widget_set_tooltip_text(foot, "Nook " NOOK_VERSION);
    gtk_widget_set_margin_top(foot, 6);
    gtk_widget_set_margin_bottom(foot, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(foot), "foot");
    gtk_box_pack_start(GTK_BOX(panel), foot, FALSE, FALSE, 0);

    gtk_widget_show_all(panel);
    refresh_status();

    g_signal_connect(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "owner-change",
                     G_CALLBACK(on_clipboard_owner_change), NULL);

    tray_init("nook", NOOK_ICON_DIR, "Nook", on_tray_activate, on_tray_menu, NULL);

    gtk_main();

    g_free(last_capture);
    g_free(skipped_digest);
    store_free();
    return 0;
}
