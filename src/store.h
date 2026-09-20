#pragma once

#include <glib.h>

typedef enum {
    SECTION_NOTES = 0,
    SECTION_TASKS,
    SECTION_CLIPS,
    SECTION_COUNT
} Section;

typedef struct {
    char *id;
    char *text;
    gint64 ts;
    gboolean done;
} Item;

void   store_load(void);
void   store_save(void);
/* FALSE when the last write to disk failed, so the UI can say so instead of losing data quietly. */
gboolean store_ok(void);
GList *store_items(Section section);
void   store_add(Section section, const char *text);
void   store_remove(Section section, const char *id);
void   store_toggle(Section section, const char *id);
void   store_clear(void);
void   store_free(void);
