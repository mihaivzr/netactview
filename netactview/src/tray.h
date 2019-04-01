#ifndef TRAY_H
#define TRAY_H

// ------------------------------------------------------------------------------------------------------------

struct tray_menu;

struct tray {
    char* icon;
    struct tray_menu* menu;
};

struct tray_menu {
    char* text;
    int disabled;
    int checked;
    int checkbox;
    void (*cb)(struct tray_menu*);
    void* context;
    struct tray_menu* submenu;
};

static void tray_update(struct tray* tray);
#if defined(TRAY_APPINDICATOR)
#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>
#define TRAY_APPINDICATOR_ID "tray-id"
static AppIndicator* indicator = NULL;
static int loop_result = 0;

// ------------------------------------------------------------------------------------------------------------

static void _tray_menu_cb(GtkMenuItem* item, gpointer data) {
    (void)item;
    struct tray_menu* m = (struct tray_menu*)data;
    m->cb(m);
}

// create menu
static GtkMenuShell* _tray_menu(struct tray_menu* m) {
    GtkMenuShell* menu = (GtkMenuShell*)gtk_menu_new();
    for (; m != NULL && m->text != NULL; m++) {
        GtkWidget* item;
        if (strcmp(m->text, "-") == 0) {
            item = gtk_separator_menu_item_new();
        } else {
            if (m->submenu != NULL) {
                item = gtk_menu_item_new_with_label(m->text);
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), GTK_WIDGET(_tray_menu(m->submenu)));
            } else if (m->checkbox) {
                item = gtk_check_menu_item_new_with_label(m->text);
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), !!m->checked);
            } else {
                item = gtk_menu_item_new_with_label(m->text);
            }
            gtk_widget_set_sensitive(item, !m->disabled);
            if (m->cb != NULL) {
                g_signal_connect(item, "activate", G_CALLBACK(_tray_menu_cb), m);
            }
        }
        gtk_widget_show(item);
        gtk_menu_shell_append(menu, item);
    }
    return menu;
}

/*void user_function(AppIndicator* arg0, gpointer user_data) {
    printf("ss update\n");
}
void scroll_event_cb(AppIndicator* indicator, gint delta, GdkScrollDirection direction, gpointer user_data) {
    printf("Tray update\n");
    g_print("Got scroll event! delta: %d, direction: %d\n", delta, direction);
}*/

static int tray_init(struct tray* tray) {
    if (gtk_init_check(0, NULL) == FALSE) {
        return -1;
    }
    indicator = app_indicator_new(TRAY_APPINDICATOR_ID, tray->icon, APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);

    // app_indicator_set_label (indicator, "xxxxx", "yyyyy");
    app_indicator_set_title(indicator, "Net Activity Viewer");

    //g_signal_connect(indicator, "scroll-event", G_CALLBACK(scroll_event_cb), NULL);
    //g_signal_connect(indicator, "new-icon", G_CALLBACK(user_function), NULL);

    tray_update(tray);
    return 0;
}

static int tray_loop(int blocking) {    
    gtk_main_iteration_do(blocking);
    return loop_result;
}

static void tray_update(struct tray* tray) {    
    app_indicator_set_icon(indicator, tray->icon);
    app_indicator_set_menu(indicator, GTK_MENU(_tray_menu(tray->menu)));
}

static void tray_exit() { loop_result = -1; }

// ------------------------------------------------------------------------------------------------------------
#else
static int tray_init(struct tray* tray) { return -1; }
static int tray_loop(int blocking) { return -1; }
static void tray_update(struct tray* tray) {}
static void tray_exit();
#endif
#endif /* TRAY_H */
