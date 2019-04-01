/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "nactv-debug.h"

#include "config.h"

#include <glade/glade.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnome/libgnome.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs.h>

#include "definitions.h"
#include "mainwindow.h"
#include "net.h"

GtkWidget *window;

#include "tray.h"
#define TRAY_ICON1 "knemo-network-transmit-receive"
#define TRAY_ICON2 "knemo-network-offline"
static struct tray tray;

static void toggleCB(struct tray_menu *item) {
    item->checked = !item->checked;
    if (item->checked) {
        tray.icon = TRAY_ICON1;
        toggled_AutoRefreshEnabled(1);
    } else {
        tray.icon = TRAY_ICON2;
        toggled_AutoRefreshEnabled(0);
    }    
    tray_update(&tray);    
}

static gboolean remove_above(gpointer data){
    gtk_window_set_keep_above(GTK_WINDOW(window), FALSE);
    return FALSE; //stop timer
}

static void showMyWind() {
    gtk_widget_show(window);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    g_timeout_add(8500, &remove_above, NULL);
}

static void hideMyWind() {
    gtk_widget_hide(window);
    // gtk_widget_set_visible(window, 0);
}

static void toggleWind() {
    if (!gtk_widget_get_visible(window)) {
        showMyWind();
    } else {
        hideMyWind();
    }
}

static void showHide(struct tray_menu *item) {
    (void)item;
    toggleWind();
    tray_update(&tray);
}

static void quitCB(struct tray_menu *item) {
    (void)item;
    tray_exit();
    exit(0);
}

static struct tray tray = {
    .icon = TRAY_ICON1,
    .menu = (struct tray_menu[]){{.text = "Show/Hide", .cb = showHide}, 
                                {.text = "Auto refresh", .checked = 1, .checkbox = 1, .cb = toggleCB}, 
                                {.text = "-"}, 
                                {.text = "Quit", .cb = quitCB}, {.text = NULL}},
};

GladeXML *GladeXml = NULL;

static void on_aboutdialog_url_activated(GtkAboutDialog *about, const gchar *url, gpointer data) { gnome_vfs_url_show(url); }

static void on_aboutdialog_email_activated(GtkAboutDialog *about, const gchar *url, gpointer data) {
    GString *s = g_string_new("mailto:");
    g_string_append(s, url);
    gnome_vfs_url_show(s->str);
    g_string_free(s, TRUE);
}

int interval=0;

static gboolean resetCounter(gpointer data){
    interval=0;
    return TRUE;
}

static gboolean timeout_func(gpointer data){
    //printf("Update tray icon\n");
    tray_loop(1);

    /*while ((tray_loop(1) == 0) && (!interval++ || interval++ )) {
        if (interval > 15) {
            //printf("Interval %d\n", interval);
            //printf("Tray click\n");
            //if > 9 = click if > 15 = wheel
            toggleWind();
        }
    }*/
    return TRUE; //continue timer
}

int main(int argc, char *argv[]) {
    if (tray_init(&tray) < 0) {
        printf("Failed to create tray icon\n");
    }
    tray_loop(1);    
    g_timeout_add(5000, &timeout_func, NULL);

    //g_timeout_add(250, &resetCounter, NULL); //this is a loop
    // developer.gnome.org/gtk-tutorial/stable/c1759.html
    
    GnomeProgram *program;
    GOptionContext *option_context;

    g_type_init();
    g_thread_init(NULL);

    //#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    //#endif

    gtk_set_locale();

    option_context = g_option_context_new(_(" - view network connections"));

    gtk_init(&argc, &argv);
    program = gnome_program_init(PACKAGE, VERSION, LIBGNOME_MODULE, argc, argv, GNOME_PARAM_GOPTION_CONTEXT, 
                                 option_context, GNOME_PROGRAM_STANDARD_PROPERTIES, GNOME_PARAM_NONE);

    gnome_vfs_init();

    gtk_about_dialog_set_url_hook(&on_aboutdialog_url_activated, NULL, NULL);
    gtk_about_dialog_set_email_hook(&on_aboutdialog_email_activated, NULL, NULL);

    GladeXml = glade_xml_new(GLADEFILE, NULL, NULL);
    if (GladeXml != NULL) {
        nactv_net_init();

        window = main_window_create();
        // gtk_widget_show(window);
        g_signal_connect(window, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
        
        toggled_AutoRefreshEnabled(1);

        gtk_main();

        main_window_data_cleanup();
        nactv_net_free();
        g_object_unref(GladeXml);
        GladeXml = NULL;
    } else {
        g_printerr("Error loading %s \nThe application might not be correctly installed.\n", GLADEFILE);
    }

    g_object_unref(program);

    return 0;
}
