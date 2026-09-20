/* Round-trip check for the persistence layer: build -> save -> reload -> verify.
 * Run with `make check`. Uses a throwaway XDG_DATA_HOME so real data is untouched. */
#include "store.h"

#include <assert.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#define CLIP_LIMIT_FOR_TEST 50

static Item *nth(Section section, guint index) {
    return g_list_nth_data(store_items(section), index);
}

int main(void) {
    g_autofree char *tmp = g_dir_make_tmp("nook-test-XXXXXX", NULL);
    assert(tmp);
    g_setenv("XDG_DATA_HOME", tmp, TRUE);

    store_add(SECTION_NOTES, "first note", FALSE);
    store_add(SECTION_NOTES, "second note", FALSE);
    store_add(SECTION_TASKS, "a task", FALSE);
    store_add(SECTION_CLIPS, "clipped = text; with [brackets]\nand a newline", TRUE);

    assert(g_list_length(store_items(SECTION_NOTES)) == 2);
    assert(g_list_length(store_items(SECTION_TASKS)) == 1);

    Item *task = nth(SECTION_TASKS, 0);
    assert(task->done == FALSE);
    store_toggle(SECTION_TASKS, task->id);
    assert(nth(SECTION_TASKS, 0)->done == TRUE);

    g_autofree char *removed_id = g_strdup(nth(SECTION_NOTES, 0)->id);
    store_remove(SECTION_NOTES, removed_id);
    assert(g_list_length(store_items(SECTION_NOTES)) == 1);

    /* Reload from disk and confirm everything survived, including the awkward characters. */
    store_free();
    assert(store_items(SECTION_NOTES) == NULL);
    store_load();

    assert(g_list_length(store_items(SECTION_NOTES)) == 1);
    assert(strcmp(nth(SECTION_NOTES, 0)->text, "second note") == 0);
    assert(nth(SECTION_TASKS, 0)->done == TRUE);
    assert(strcmp(nth(SECTION_CLIPS, 0)->text,
                  "clipped = text; with [brackets]\nand a newline") == 0);
    /* Saved by hand, so it must come back kept and survive eviction. */
    assert(nth(SECTION_CLIPS, 0)->done == TRUE);
    assert(nth(SECTION_NOTES, 0)->ts > 0);

    store_clear();
    store_free();
    store_load();
    assert(store_items(SECTION_NOTES) == NULL);

    /* Loading twice must not duplicate: the copies would share ids, so edits would reach only
     * the first and a later save would silently drop the rest. */
    store_add(SECTION_NOTES, "duplicate check", FALSE);
    store_load();
    assert(g_list_length(store_items(SECTION_NOTES)) == 1);

    /* Auto-captured clips roll off at the cap; a kept one stays however much is copied after it. */
    store_add(SECTION_CLIPS, "kept clip", TRUE);
    for (int i = 0; i < 60; i++) {
        g_autofree char *text = g_strdup_printf("auto clip %d", i);
        store_add(SECTION_CLIPS, text, FALSE);
    }
    assert(g_list_length(store_items(SECTION_CLIPS)) == 50);
    assert(strcmp(nth(SECTION_CLIPS, 0)->text, "kept clip") == 0);
    assert(strcmp(nth(SECTION_CLIPS, 1)->text, "auto clip 11") == 0);
    store_free();
    store_load();
    assert(g_list_length(store_items(SECTION_CLIPS)) == 50);
    assert(nth(SECTION_CLIPS, 0)->done == TRUE);

    /* A cap full of kept clips must not make the newest arrival its own victim: that would drop
     * every auto-captured clip at the moment it was stored. */
    store_clear();
    for (int i = 0; i < CLIP_LIMIT_FOR_TEST; i++) {
        g_autofree char *text = g_strdup_printf("kept clip %d", i);
        store_add(SECTION_CLIPS, text, TRUE);
    }
    assert(g_list_length(store_items(SECTION_CLIPS)) == CLIP_LIMIT_FOR_TEST);
    store_add(SECTION_CLIPS, "auto clip that must survive", FALSE);
    assert(g_list_length(store_items(SECTION_CLIPS)) == CLIP_LIMIT_FOR_TEST + 1);
    assert(strcmp(nth(SECTION_CLIPS, CLIP_LIMIT_FOR_TEST)->text, "auto clip that must survive") == 0);
    store_add(SECTION_CLIPS, "the next auto clip replaces it", FALSE);
    assert(g_list_length(store_items(SECTION_CLIPS)) == CLIP_LIMIT_FOR_TEST + 1);
    assert(strcmp(nth(SECTION_CLIPS, CLIP_LIMIT_FOR_TEST)->text, "the next auto clip replaces it") == 0);

    assert(store_auto_clips());
    store_set_auto_clips(FALSE);
    store_free();
    store_load();
    assert(!store_auto_clips());
    store_set_auto_clips(TRUE);

    store_clear();

    /* A failed write must be visible to the UI, not just logged. */
    assert(store_ok());
    g_autofree char *nook_dir = g_build_filename(tmp, "nook", NULL);
    assert(g_chmod(nook_dir, 0500) == 0);
    store_add(SECTION_NOTES, "written while read-only", FALSE);
    assert(!store_ok());
    assert(g_chmod(nook_dir, 0700) == 0);
    store_add(SECTION_NOTES, "writable again", FALSE);
    assert(store_ok());
    assert(store_items(SECTION_TASKS) == NULL);

    store_free();
    g_autofree char *data = g_build_filename(tmp, "nook", "data.ini", NULL);
    g_unlink(data);
    printf("store round-trip: OK\n");
    return 0;
}
