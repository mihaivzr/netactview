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

#include "nactv-debug.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "config.h"

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <glib.h>
#include <libgnome/libgnome.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <glib/gi18n.h>

#include "mainwindow.h"
#include "definitions.h"
#include "net.h"

GladeXML *GladeXml = NULL;


static void on_aboutdialog_url_activated (GtkAboutDialog *about, const gchar *url, 
										 gpointer data)
{
	gnome_vfs_url_show(url);
}

static void on_aboutdialog_email_activated (GtkAboutDialog *about, const gchar *url, 
										 	gpointer data)
{
	GString *s = g_string_new("mailto:");
	g_string_append(s, url);
	gnome_vfs_url_show(s->str);
	g_string_free(s, TRUE);
}


int
main (int argc, char *argv[])
{
	GtkWidget *window;
	GnomeProgram *program;
	GOptionContext *option_context;
	
	g_type_init();
	g_thread_init(NULL);
	
#ifdef ENABLE_NLS
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);
#endif
	gtk_set_locale();
	
	option_context = g_option_context_new(_(" - view network connections"));
	
	gtk_init(&argc, &argv);
	program = gnome_program_init(PACKAGE, VERSION, LIBGNOME_MODULE, argc, argv, 
								 GNOME_PARAM_GOPTION_CONTEXT, option_context,
								 GNOME_PROGRAM_STANDARD_PROPERTIES,
								 GNOME_PARAM_NONE);
	
	gnome_vfs_init();
	
	gtk_about_dialog_set_url_hook(&on_aboutdialog_url_activated, NULL, NULL);
	gtk_about_dialog_set_email_hook(&on_aboutdialog_email_activated, NULL, NULL);
	
	GladeXml = glade_xml_new(GLADEFILE, NULL, NULL);
	if (GladeXml != NULL)
	{
		nactv_net_init();
		
		window = main_window_create();
		gtk_widget_show(window);
		
		gtk_main();
		
		main_window_data_cleanup();
		nactv_net_free();
		g_object_unref(GladeXml); GladeXml = NULL;
	}else
	{
		g_printerr("Error loading %s \nThe application might not be correctly installed.\n", 
				   GLADEFILE);
	}
	
	g_object_unref (program);

	return 0;
}
