#include "platform/tray.h"
#include "platform/runtime.h"
#include "platform/devices.h"
#include "log.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <string.h>
#ifdef AUDIOSYNC_TRAY
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
static AppIndicator *indicator;
#endif
static GMainLoop *loop;
static GThread *worker;
static int paused, pending, stopping;
static drive_info_t previous[64];
static int previous_count;

static void start_sync(void);
static gboolean sync_done(gpointer data) {
    int result = GPOINTER_TO_INT(data);
    g_thread_join(worker);
    worker = NULL;
    log_info(result == 0 ? "Sync complete" : result == 3 ? "No mounted profile" : "Sync failed");
    if (pending && !paused && !stopping) {
        pending = 0;
        start_sync();
    }
    return G_SOURCE_REMOVE;
}
static gpointer sync_worker(gpointer data) {
    (void)data;
    int result = runtime_sync(NULL);
    g_idle_add(sync_done, GINT_TO_POINTER(result));
    return NULL;
}
static void start_sync(void) {
    if (stopping)
        return;
    if (worker) {
        pending = 1;
        return;
    }
    GError *error = NULL;
    worker = g_thread_try_new("audiosync", sync_worker, NULL, &error);
    if (!worker) {
        log_err("Could not start sync: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
    }
}
static gboolean poll_drives(gpointer data) {
    (void)data;
    drive_info_t drives[64];
    int count = devices_list(drives, 64), changed = count != previous_count;
    for (int i = 0; i < count && !changed; ++i) {
        const drive_info_t *old = devices_find_by_serial(previous, previous_count, drives[i].serial);
        if (!old || strcmp(old->mount, drives[i].mount))
            changed = 1;
    }
    memcpy(previous, drives, (size_t)count * sizeof *drives);
    previous_count = count;
    if (changed && !paused)
        start_sync();
    return G_SOURCE_CONTINUE;
}
static gboolean quit(gpointer data) {
    (void)data;
    stopping = 1;
    g_main_loop_quit(loop);
    return G_SOURCE_CONTINUE;
}
#ifdef AUDIOSYNC_TRAY
static void menu_sync(GtkMenuItem *item, gpointer data) {
    (void)item;
    (void)data;
    start_sync();
}
static void menu_pause(GtkCheckMenuItem *item, gpointer data) {
    (void)data;
    paused = gtk_check_menu_item_get_active(item);
    if (!paused)
        start_sync();
}
static void menu_autostart(GtkCheckMenuItem *item, gpointer data) {
    (void)data;
    int result = gtk_check_menu_item_get_active(item) ? autostart_enable() : autostart_disable();
    if (result)
        log_err("Could not change autostart");
}
static void menu_exit(GtkMenuItem *item, gpointer data) {
    (void)item;
    quit(data);
}
static int create_tray(void) {
    if (!gtk_init_check(NULL, NULL))
        return 0;
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *sync = gtk_menu_item_new_with_label("Sync now");
    GtkWidget *pause = gtk_check_menu_item_new_with_label("Pause auto-sync");
    GtkWidget *autostart = gtk_check_menu_item_new_with_label("Start at login");
    GtkWidget *exit = gtk_menu_item_new_with_label("Exit");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(autostart), autostart_is_enabled());
    g_signal_connect(sync, "activate", G_CALLBACK(menu_sync), NULL);
    g_signal_connect(pause, "toggled", G_CALLBACK(menu_pause), NULL);
    g_signal_connect(autostart, "toggled", G_CALLBACK(menu_autostart), NULL);
    g_signal_connect(exit, "activate", G_CALLBACK(menu_exit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sync);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), pause);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), autostart);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), exit);
    gtk_widget_show_all(menu);
    indicator = app_indicator_new("audiosync", "audio-x-generic", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_title(indicator, "AudioSync");
    app_indicator_set_menu(indicator, GTK_MENU(menu));
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    return 1;
}
#endif
int tray_run(int headless) {
    if (!headless) {
#ifdef AUDIOSYNC_TRAY
        if (!create_tray()) {
            log_err("No desktop display; use watch --no-tray");
            return 1;
        }
#else
        log_err("Built without tray support; use watch --no-tray");
        return 1;
#endif
    }
    loop = g_main_loop_new(NULL, FALSE);
    guint timer = g_timeout_add_seconds(5, poll_drives, NULL);
    guint interrupt = g_unix_signal_add(SIGINT, quit, NULL);
    guint terminate = g_unix_signal_add(SIGTERM, quit, NULL);
    previous_count = devices_list(previous, 64);
    start_sync();
    log_info("Watching mounted drives every 5 seconds");
    g_main_loop_run(loop);
    stopping = 1;
    g_source_remove(timer);
    g_source_remove(interrupt);
    g_source_remove(terminate);
    if (worker) {
        /* Process completion only after joining; no worker outlives main. */
        g_thread_join(worker);
        worker = NULL;
    }
#ifdef AUDIOSYNC_TRAY
    if (indicator)
        g_object_unref(indicator);
#endif
    g_main_loop_unref(loop);
    return 0;
}
static char *autostart_path(void) {
    return g_build_filename(g_get_user_config_dir(), "autostart", "audiosync.desktop", NULL);
}
int autostart_is_enabled(void) {
    char *path = autostart_path();
    int exists = g_file_test(path, G_FILE_TEST_IS_REGULAR);
    g_free(path);
    return exists;
}
int autostart_enable(void) {
#ifndef AUDIOSYNC_TRAY
    log_err("Autostart requires a build with tray support");
    return 1;
#else
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (!exe)
        return 1;
    /* Desktop Entry Exec quoting is different from shell quoting. */
    GString *quoted = g_string_new("\"");
    for (const char *p = exe; *p; ++p) {
        if (strchr("\"\\\x60$", *p))
            g_string_append_c(quoted, '\\');
        if (*p == '%')
            g_string_append_c(quoted, '%');
        g_string_append_c(quoted, *p);
    }
    g_string_append(quoted, "\" watch");
    GKeyFile *file = g_key_file_new();
    g_key_file_set_string(file, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(file, "Desktop Entry", "Name", "AudioSync tray");
    g_key_file_set_string(file, "Desktop Entry", "Exec", quoted->str);
    g_key_file_set_boolean(file, "Desktop Entry", "Terminal", FALSE);
    char *path = autostart_path();
    char *dir = g_path_get_dirname(path);
    int result = g_mkdir_with_parents(dir, 0700) == 0 && g_key_file_save_to_file(file, path, NULL) ? 0 : 1;
    g_free(dir);
    g_free(path);
    g_key_file_unref(file);
    g_string_free(quoted, TRUE);
    g_free(exe);
    return result;
#endif
}
int autostart_disable(void) {
    char *path = autostart_path();
    int result = g_remove(path);
    if (result && errno == ENOENT)
        result = 0;
    g_free(path);
    return result ? 1 : 0;
}
