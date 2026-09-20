#include "store.h"

#include <glib/gstdio.h>
#include <string.h>
#include <errno.h>

static const char *SECTION_KEYS[SECTION_COUNT] = { "notes", "tasks", "clips" };
static GList *lists[SECTION_COUNT];
static gboolean save_ok = TRUE;

static void item_free(Item *item) {
    g_free(item->id);
    g_free(item->text);
    g_free(item);
}

static char *data_path(void) {
    return g_build_filename(g_get_user_data_dir(), "nook", "data.ini", NULL);
}

static gint by_ts(gconstpointer a, gconstpointer b) {
    gint64 diff = ((const Item *) a)->ts - ((const Item *) b)->ts;
    return diff < 0 ? -1 : diff > 0 ? 1 : 0;
}

GList *store_items(Section section) {
    return lists[section];
}

gboolean store_ok(void) {
    return save_ok;
}

void store_load(void) {
    /* Reset first: loading twice would otherwise duplicate every item, and the duplicates share
     * ids, so edits would hit only the first copy. */
    store_free();

    g_autofree char *path = data_path();
    g_autoptr(GKeyFile) kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL))
        return;

    gsize count = 0;
    g_auto(GStrv) groups = g_key_file_get_groups(kf, &count);

    for (gsize i = 0; i < count; i++) {
        const char *dot = strchr(groups[i], '.');
        if (!dot)
            continue;

        for (int s = 0; s < SECTION_COUNT; s++) {
            gsize prefix_len = strlen(SECTION_KEYS[s]);
            if (strncmp(groups[i], SECTION_KEYS[s], prefix_len) != 0 || groups[i][prefix_len] != '.')
                continue;

            Item *item = g_new0(Item, 1);
            item->id = g_strdup(dot + 1);
            item->text = g_key_file_get_string(kf, groups[i], "text", NULL);
            item->ts = g_key_file_get_int64(kf, groups[i], "ts", NULL);
            item->done = g_key_file_get_boolean(kf, groups[i], "done", NULL);

            if (item->text)
                lists[s] = g_list_insert_sorted(lists[s], item, by_ts);
            else
                item_free(item);
            break;
        }
    }
}

void store_save(void) {
    g_autofree char *path = data_path();
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_warning("nook: cannot create %s: %s", dir, g_strerror(errno));
        save_ok = FALSE;
        return;
    }

    g_autoptr(GKeyFile) kf = g_key_file_new();
    for (int s = 0; s < SECTION_COUNT; s++) {
        for (GList *l = lists[s]; l; l = l->next) {
            Item *item = l->data;
            g_autofree char *group = g_strdup_printf("%s.%s", SECTION_KEYS[s], item->id);
            g_key_file_set_string(kf, group, "text", item->text);
            g_key_file_set_int64(kf, group, "ts", item->ts);
            if (s == SECTION_TASKS)
                g_key_file_set_boolean(kf, group, "done", item->done);
        }
    }

    g_autoptr(GError) error = NULL;
    save_ok = g_key_file_save_to_file(kf, path, &error);
    if (!save_ok)
        g_warning("nook: could not save %s: %s", path, error->message);
}

void store_add(Section section, const char *text) {
    Item *item = g_new0(Item, 1);
    item->id = g_uuid_string_random();
    item->text = g_strdup(text);
    item->ts = g_get_real_time() / G_USEC_PER_SEC;
    lists[section] = g_list_append(lists[section], item);
    store_save();
}

static GList *find(Section section, const char *id) {
    for (GList *l = lists[section]; l; l = l->next)
        if (g_strcmp0(((Item *) l->data)->id, id) == 0)
            return l;
    return NULL;
}

void store_remove(Section section, const char *id) {
    GList *link = find(section, id);
    if (!link)
        return;
    item_free(link->data);
    lists[section] = g_list_delete_link(lists[section], link);
    store_save();
}

void store_toggle(Section section, const char *id) {
    GList *link = find(section, id);
    if (!link)
        return;
    Item *item = link->data;
    item->done = !item->done;
    store_save();
}

void store_clear(void) {
    store_free();
    store_save();
}

void store_free(void) {
    for (int s = 0; s < SECTION_COUNT; s++) {
        g_list_free_full(lists[s], (GDestroyNotify) item_free);
        lists[s] = NULL;
    }
}
