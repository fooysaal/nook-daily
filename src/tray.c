/* StatusNotifierItem + com.canonical.dbusmenu, implemented directly on GDBus.
 *
 * libayatana-appindicator cannot do this: it exposes no Activate method, so a host can never
 * hand a click back to the application. Implementing the spec ourselves lets the panel open on
 * double-click, and drops a dependency — GDBus ships with glib. */

#include "tray.h"

#include <gio/gio.h>
#include <unistd.h>

#define WATCHER_NAME  "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH  "/StatusNotifierWatcher"
#define ITEM_PATH     "/StatusNotifierItem"
#define MENU_PATH     "/MenuBar"

static const char SNI_XML[] =
    "<node><interface name='org.kde.StatusNotifierItem'>"
    "  <property name='Category' type='s' access='read'/>"
    "  <property name='Id' type='s' access='read'/>"
    "  <property name='Title' type='s' access='read'/>"
    "  <property name='Status' type='s' access='read'/>"
    "  <property name='IconName' type='s' access='read'/>"
    "  <property name='IconThemePath' type='s' access='read'/>"
    "  <property name='ItemIsMenu' type='b' access='read'/>"
    "  <property name='Menu' type='o' access='read'/>"
    "  <method name='Activate'>"
    "    <arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>"
    "  <method name='SecondaryActivate'>"
    "    <arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>"
    "  <method name='ContextMenu'>"
    "    <arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>"
    "  <method name='Scroll'>"
    "    <arg name='delta' type='i' direction='in'/>"
    "    <arg name='orientation' type='s' direction='in'/></method>"
    "  <signal name='NewIcon'/><signal name='NewTitle'/>"
    "  <signal name='NewStatus'><arg name='status' type='s'/></signal>"
    "</interface></node>";

static const char MENU_XML[] =
    "<node><interface name='com.canonical.dbusmenu'>"
    "  <property name='Version' type='u' access='read'/>"
    "  <property name='TextDirection' type='s' access='read'/>"
    "  <property name='Status' type='s' access='read'/>"
    "  <property name='IconThemePath' type='as' access='read'/>"
    "  <method name='GetLayout'>"
    "    <arg name='parentId' type='i' direction='in'/>"
    "    <arg name='recursionDepth' type='i' direction='in'/>"
    "    <arg name='propertyNames' type='as' direction='in'/>"
    "    <arg name='revision' type='u' direction='out'/>"
    "    <arg name='layout' type='(ia{sv}av)' direction='out'/></method>"
    "  <method name='GetGroupProperties'>"
    "    <arg name='ids' type='ai' direction='in'/>"
    "    <arg name='propertyNames' type='as' direction='in'/>"
    "    <arg name='properties' type='a(ia{sv})' direction='out'/></method>"
    "  <method name='GetProperty'>"
    "    <arg name='id' type='i' direction='in'/><arg name='name' type='s' direction='in'/>"
    "    <arg name='value' type='v' direction='out'/></method>"
    "  <method name='Event'>"
    "    <arg name='id' type='i' direction='in'/><arg name='eventId' type='s' direction='in'/>"
    "    <arg name='data' type='v' direction='in'/>"
    "    <arg name='timestamp' type='u' direction='in'/></method>"
    "  <method name='EventGroup'>"
    "    <arg name='events' type='a(isvu)' direction='in'/>"
    "    <arg name='idErrors' type='ai' direction='out'/></method>"
    "  <method name='AboutToShow'>"
    "    <arg name='id' type='i' direction='in'/>"
    "    <arg name='needUpdate' type='b' direction='out'/></method>"
    "  <signal name='ItemsPropertiesUpdated'>"
    "    <arg type='a(ia{sv})'/><arg type='a(ias)'/></signal>"
    "  <signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
    "  <signal name='ItemActivationRequested'><arg type='i'/><arg type='u'/></signal>"
    "</interface></node>";

static struct {
    char *icon_name;
    char *icon_theme_path;
    char *title;
    TrayActivateFn on_activate;
    TrayMenuFn on_menu;
    gpointer user_data;
    GDBusConnection *bus;
    char *bus_name;
    gboolean registered;
    gboolean logged_menu_fetch;
    gboolean logged_icon_read;
    guint layout_revision;
} tray;

static const struct { int id; const char *label; } MENU_ITEMS[] = {
    { TRAY_MENU_OPEN, "Open Nook" },
    { TRAY_MENU_QUIT, "Quit Nook" },
};

static GVariant *build_item(int id, const char *label) {
    GVariantBuilder props, children;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(label));
    g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(TRUE));
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    return g_variant_new("(ia{sv}av)", id, &props, &children);
}

static GVariant *build_layout(void) {
    GVariantBuilder props, children;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "children-display", g_variant_new_string("submenu"));
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    for (gsize i = 0; i < G_N_ELEMENTS(MENU_ITEMS); i++)
        g_variant_builder_add(&children, "v", build_item(MENU_ITEMS[i].id, MENU_ITEMS[i].label));
    return g_variant_new("(ia{sv}av)", 0, &props, &children);
}

static void sni_method(GDBusConnection *conn, const char *sender, const char *path,
                       const char *iface, const char *method, GVariant *params,
                       GDBusMethodInvocation *invocation, gpointer data) {
    (void) conn; (void) sender; (void) path; (void) iface; (void) params; (void) data;

    if (g_strcmp0(method, "Activate") == 0 && tray.on_activate)
        tray.on_activate(tray.user_data);

    /* SecondaryActivate (middle click) and ContextMenu are accepted and ignored: replying keeps
     * the host from marking the item as broken. */
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static GVariant *sni_get_property(GDBusConnection *conn, const char *sender, const char *path,
                                  const char *iface, const char *name, GError **error,
                                  gpointer data) {
    (void) conn; (void) sender; (void) path; (void) iface; (void) error; (void) data;

    if (g_strcmp0(name, "Category") == 0)
        return g_variant_new_string("ApplicationStatus");
    if (g_strcmp0(name, "Id") == 0)
        return g_variant_new_string(tray.icon_name);
    if (g_strcmp0(name, "Title") == 0)
        return g_variant_new_string(tray.title);
    if (g_strcmp0(name, "Status") == 0)
        return g_variant_new_string("Active");
    if (g_strcmp0(name, "IconName") == 0) {
        if (!tray.logged_icon_read) {
            tray.logged_icon_read = TRUE;
            g_message("nook: host read the icon");
        }
        return g_variant_new_string(tray.icon_name);
    }
    if (g_strcmp0(name, "IconThemePath") == 0)
        return g_variant_new_string(tray.icon_theme_path);
    /* false tells the host we handle clicks ourselves, so it calls Activate. */
    if (g_strcmp0(name, "ItemIsMenu") == 0)
        return g_variant_new_boolean(FALSE);
    if (g_strcmp0(name, "Menu") == 0)
        return g_variant_new_object_path(MENU_PATH);
    return NULL;
}

static void menu_method(GDBusConnection *conn, const char *sender, const char *path,
                        const char *iface, const char *method, GVariant *params,
                        GDBusMethodInvocation *invocation, gpointer data) {
    (void) conn; (void) sender; (void) path; (void) iface; (void) data;

    if (g_strcmp0(method, "GetLayout") == 0) {
        if (!tray.logged_menu_fetch) {
            tray.logged_menu_fetch = TRUE;
            g_message("nook: host fetched the menu");
        }
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(u@(ia{sv}av))", tray.layout_revision, build_layout()));
        return;
    }

    if (g_strcmp0(method, "GetGroupProperties") == 0) {
        GVariantBuilder out;
        g_variant_builder_init(&out, G_VARIANT_TYPE("a(ia{sv})"));
        for (gsize i = 0; i < G_N_ELEMENTS(MENU_ITEMS); i++) {
            GVariantBuilder props;
            g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&props, "{sv}", "label",
                                  g_variant_new_string(MENU_ITEMS[i].label));
            g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
            g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(TRUE));
            g_variant_builder_add(&out, "(ia{sv})", MENU_ITEMS[i].id, &props);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(ia{sv}))", &out));
        return;
    }

    if (g_strcmp0(method, "GetProperty") == 0) {
        int id = 0;
        const char *name = NULL;
        g_variant_get(params, "(i&s)", &id, &name);
        for (gsize i = 0; i < G_N_ELEMENTS(MENU_ITEMS); i++) {
            if (MENU_ITEMS[i].id == id && g_strcmp0(name, "label") == 0) {
                g_dbus_method_invocation_return_value(invocation,
                    g_variant_new("(v)", g_variant_new_string(MENU_ITEMS[i].label)));
                return;
            }
        }
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(v)", g_variant_new_string("")));
        return;
    }

    if (g_strcmp0(method, "Event") == 0) {
        int id = 0;
        const char *event_id = NULL;
        g_autoptr(GVariant) event_data = NULL;
        guint32 timestamp = 0;
        g_variant_get(params, "(i&svu)", &id, &event_id, &event_data, &timestamp);
        if (g_strcmp0(event_id, "clicked") == 0 && tray.on_menu)
            tray.on_menu(id, tray.user_data);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        return;
    }

    if (g_strcmp0(method, "EventGroup") == 0) {
        GVariantBuilder errors;
        g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ai)", &errors));
        return;
    }

    g_dbus_method_invocation_return_value(invocation, NULL);
}

static GVariant *menu_get_property(GDBusConnection *conn, const char *sender, const char *path,
                                   const char *iface, const char *name, GError **error,
                                   gpointer data) {
    (void) conn; (void) sender; (void) path; (void) iface; (void) error; (void) data;

    if (g_strcmp0(name, "Version") == 0)
        return g_variant_new_uint32(3);
    if (g_strcmp0(name, "TextDirection") == 0)
        return g_variant_new_string("ltr");
    if (g_strcmp0(name, "Status") == 0)
        return g_variant_new_string("normal");
    if (g_strcmp0(name, "IconThemePath") == 0) {
        const char *paths[] = { tray.icon_theme_path, NULL };
        return g_variant_new_strv(paths, -1);
    }
    return NULL;
}

static const GDBusInterfaceVTable SNI_VTABLE = { sni_method, sni_get_property, NULL, { 0 } };
static const GDBusInterfaceVTable MENU_VTABLE = { menu_method, menu_get_property, NULL, { 0 } };

/* Registering in the same second GNOME Shell starts can leave the AppIndicator extension showing
 * its "…" placeholder with no menu, while Activate still works; it never retries on its own, and
 * only a later redraw (e.g. after suspend) fixed it. Nudging it once the shell has settled makes it
 * re-read the icon and re-fetch the menu. */
static gboolean reannounce(gpointer data) {
    (void) data;
    tray.layout_revision++;
    g_dbus_connection_emit_signal(tray.bus, NULL, ITEM_PATH, "org.kde.StatusNotifierItem",
                                  "NewIcon", NULL, NULL);
    g_dbus_connection_emit_signal(tray.bus, NULL, MENU_PATH, "com.canonical.dbusmenu",
                                  "LayoutUpdated", g_variant_new("(ui)", tray.layout_revision, 0),
                                  NULL);
    return G_SOURCE_REMOVE;
}

static void on_registered(GObject *source, GAsyncResult *result, gpointer data) {
    (void) data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) reply =
        g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (reply) {
        tray.registered = TRUE;
        g_message("nook: registered with tray host");
        g_timeout_add_seconds(5, reannounce, NULL);
    } else {
        g_warning("nook: tray registration failed: %s", error->message);
    }
}

static void register_with_watcher(void) {
    g_dbus_connection_call(tray.bus, WATCHER_NAME, WATCHER_PATH, WATCHER_NAME,
                           "RegisterStatusNotifierItem",
                           g_variant_new("(s)", tray.bus_name),
                           NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_registered, NULL);
}

/* Without a StatusNotifier host — vanilla GNOME with no AppIndicator extension — no icon ever
 * appears. Left silent the process would sit invisible with no way to reach or quit it, so show
 * the panel once and say why. */
static gboolean warn_if_unregistered(gpointer data) {
    (void) data;
    if (!tray.registered) {
        g_warning("nook: no StatusNotifier host found — no tray icon will appear. "
                  "On GNOME install the AppIndicator extension. Opening the panel instead.");
        if (tray.on_activate)
            tray.on_activate(tray.user_data);
    }
    return G_SOURCE_REMOVE;
}

/* Re-register whenever the watcher reappears, so the icon survives a shell restart. */
static void on_watcher_appeared(GDBusConnection *conn, const char *name,
                                const char *owner, gpointer data) {
    (void) conn; (void) name; (void) owner; (void) data;
    register_with_watcher();
}

static void on_bus_acquired(GDBusConnection *conn, const char *name, gpointer data) {
    (void) name; (void) data;
    tray.bus = conn;

    g_autoptr(GError) error = NULL;
    g_autoptr(GDBusNodeInfo) sni = g_dbus_node_info_new_for_xml(SNI_XML, &error);
    g_autoptr(GDBusNodeInfo) menu = g_dbus_node_info_new_for_xml(MENU_XML, &error);
    if (!sni || !menu) {
        g_warning("nook: bad tray interface definition: %s", error->message);
        return;
    }

    if (!g_dbus_connection_register_object(conn, ITEM_PATH, sni->interfaces[0],
                                           &SNI_VTABLE, NULL, NULL, &error)) {
        g_warning("nook: cannot export %s: %s", ITEM_PATH, error->message);
        return;
    }
    if (!g_dbus_connection_register_object(conn, MENU_PATH, menu->interfaces[0],
                                           &MENU_VTABLE, NULL, NULL, &error)) {
        g_warning("nook: cannot export %s: %s", MENU_PATH, error->message);
        return;
    }

    g_bus_watch_name_on_connection(conn, WATCHER_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                   on_watcher_appeared, NULL, NULL, NULL);
    g_timeout_add_seconds(3, warn_if_unregistered, NULL);
}

static void on_name_lost(GDBusConnection *conn, const char *name, gpointer data) {
    (void) conn; (void) data;
    g_warning("nook: could not own %s on the session bus — no tray icon.", name);
    if (!tray.registered && tray.on_activate)
        tray.on_activate(tray.user_data);
}

void tray_init(const char *icon_name, const char *icon_theme_path, const char *title,
               TrayActivateFn on_activate, TrayMenuFn on_menu, gpointer user_data) {
    tray.icon_name = g_strdup(icon_name);
    tray.icon_theme_path = g_strdup(icon_theme_path);
    tray.title = g_strdup(title);
    tray.on_activate = on_activate;
    tray.on_menu = on_menu;
    tray.user_data = user_data;
    tray.layout_revision = 1;
    tray.bus_name = g_strdup_printf("org.kde.StatusNotifierItem-%d-1", getpid());

    g_bus_own_name(G_BUS_TYPE_SESSION, tray.bus_name, G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus_acquired, NULL, on_name_lost, NULL, NULL);
}
