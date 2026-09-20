#pragma once

#include <glib.h>

/* Menu item ids surfaced through the single-click menu. */
enum {
    TRAY_MENU_OPEN = 1,
    TRAY_MENU_QUIT = 2,
};

typedef void (*TrayActivateFn)(gpointer user_data);
typedef void (*TrayMenuFn)(int item_id, gpointer user_data);

void tray_init(const char *icon_name,
               const char *icon_theme_path,
               const char *title,
               TrayActivateFn on_activate,
               TrayMenuFn on_menu,
               gpointer user_data);
