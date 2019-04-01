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
#include "net.h"
#include "utils.h"
#include "filter.h"
#include "mainwindow.h"
#include "definitions.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#include "config.h"

#include <libgnome/libgnome.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <gconf/gconf-client.h>


/*Column data types as used by compare functions*/
typedef enum {
	MVC_TYPE_STRING,
	MVC_TYPE_INT,
	MVC_TYPE_IP_ADDRESS,
	MVC_TYPE_HOST,
	MVC_TYPE_NOCOMPARE
} ColumnDataType;

/* The main view columns.
 */
typedef enum {
	MVC_PROTOCOL,
	MVC_LOCALHOST,
	MVC_LOCALADDRESS,
	MVC_LOCALPORT,
	MVC_STATE,
	MVC_REMOTEADDRESS,
	MVC_REMOTEPORT,
	MVC_REMOTEHOST,
	MVC_PID,
	MVC_PROGRAMNAME,
	MVC_PROGRAMCOMMAND,
	MVC_VISIBLE,
	MVC_DATA,
	MVC_COLOR,
	MVC_COLUMNSNUMBER
} ColumnIndex;

#define MVC_DATA_COLUMNSNUMBER 3
#define MVC_VIEW_COLUMNSNUMBER (MVC_COLUMNSNUMBER-MVC_DATA_COLUMNSNUMBER)

typedef struct {
	ColumnIndex index;
	const char *title;
	ColumnDataType datatype;
	float align;
	gboolean visible;
} ColumnData;

/*Keep it comlete and sorted by column index*/
static const ColumnData main_view_column_data[MVC_VIEW_COLUMNSNUMBER] = {
	{MVC_PROTOCOL, 		N_("Protocol"), 		MVC_TYPE_STRING, 0, TRUE},
	{MVC_LOCALHOST, 	N_("Local Host"), 		MVC_TYPE_HOST, 0, FALSE},
	{MVC_LOCALADDRESS, 	N_("Local Address"), 	MVC_TYPE_IP_ADDRESS, 0, FALSE},
	{MVC_LOCALPORT, 	N_("Local Port"), 		MVC_TYPE_INT, 0, TRUE},
	{MVC_STATE, 		N_("State"), 			MVC_TYPE_STRING, 0, TRUE},
	{MVC_REMOTEADDRESS, N_("Remote Address"), MVC_TYPE_IP_ADDRESS, 0, TRUE},
	{MVC_REMOTEPORT, 	N_("Remote Port"), 	MVC_TYPE_INT, 0, TRUE},
	{MVC_REMOTEHOST, 	N_("Remote Host"), 	MVC_TYPE_HOST, 0, TRUE},
	{MVC_PID, 			N_("Pid"), 				MVC_TYPE_INT, 0, TRUE},
	{MVC_PROGRAMNAME, 	N_("Program"), 			MVC_TYPE_STRING, 0, TRUE},
	{MVC_PROGRAMCOMMAND, N_("Command"), 		MVC_TYPE_STRING, 0, FALSE}
};

typedef enum {
	LLS_NEW,
	LLS_NORMAL,
	LLS_CLOSED
} ListLineState;

typedef struct {
	GtkTreeIter *iter;
	ListLineState state;
	GTimer *addedtime;
	GTimer *closedtime;
} ListLineUserData;


#define DEFAULT_CLOSED_SHOW_INT 3
#define DEFAULT_CLOSED_COLOR "#4d4d4d"
#define DEFAULT_NEW_SHOW_INT 3
#define DEFAULT_NEW_COLOR "green"

typedef struct {
	GtkTreeView *main_view;
	GtkListStore *main_store;
	GtkTreeModel *main_store_filtered;
	GtkLabel *label_count, *label_sent, *label_received, *label_visible;
	GtkMenu *mainPopup;
	GtkTreeViewColumn *last_popup_column;
	/*Associates the data store index with the graphical column*/
	GtkTreeViewColumn *main_view_columns[MVC_VIEW_COLUMNSNUMBER];
	GHashTable *column_to_index_hash;

	GArray *connections;
	gboolean auto_refresh;
	unsigned auto_refresh_interval, auto_refresh_id;
	gchar sel_arinterval_menu[128];
	gboolean view_local_host, view_remote_host, view_local_address, view_port_names;
	gboolean view_unestablished_connections, view_command;
	gboolean view_colors;
	gboolean show_closed_connections;
	gboolean keep_closed_connections;
	gboolean update_disabled, main_view_created, restart_requested;
	volatile gboolean exit_requested;
	char *save_location;

	int current_sort_column;
	int current_sort_direction;

	GHashTable *ip_host_hash, *requested_ip_hash;
	GThreadPool *host_loader_pool;
	GMutex *host_hash_lock;
	
	GThread *data_load_thread;
	GMutex *loaded_conn_lock, *refresh_request_lock;
	GCond *refresh_request_cond;
	gboolean refresh_requested;
	NetConnection *latest_connections;
	unsigned int nr_latest_connections;
	
	NetStatistics statistics, statistics_base;
	GTimer *statistics_timer;

	gboolean filtering;
	GString *filter;
	char *filterMask;
	Filter *filterTree, *filterTreeNoCase;
	gboolean caseSensitiveFilter, filterOperators;
	
	int columns_initial_view_order[MVC_VIEW_COLUMNSNUMBER]; /* [position] = index. */ 
	
	char *default_fixed_font;
	gboolean first_refresh, manual_refresh;

	int window_width, window_height, initial_window_width, initial_window_height;
	gboolean window_maximized;
} MainWindowData;

static void set_main_window_data_defaults (MainWindowData *m) {
	memset(m, 0, sizeof(MainWindowData)); 
	/*NULL, 0, and FALSE already set. Set it again only to make the option obvious.*/
	m->exit_requested = FALSE;
	m->auto_refresh = TRUE;
	m->auto_refresh_interval = 1000;
	m->auto_refresh_id = 0;
	n_strlcpy(m->sel_arinterval_menu, "menuAutoRefresh1", sizeof(m->sel_arinterval_menu));
	m->view_local_host = FALSE;
	m->view_remote_host = TRUE;
	m->view_local_address = FALSE;
	m->view_port_names = TRUE;
	m->keep_closed_connections = TRUE;
	m->view_unestablished_connections = TRUE;
	m->view_command = FALSE;
	m->view_colors = FALSE;
	m->show_closed_connections = TRUE;
	m->current_sort_column = MVC_PROTOCOL;
	m->current_sort_direction = GTK_SORT_ASCENDING;
	m->first_refresh = TRUE;

	m->filtering = FALSE;
	m->filter = g_string_new("");
	m->caseSensitiveFilter = TRUE;
	m->filterOperators = FALSE;
	
	{
		const int initial_order[MVC_VIEW_COLUMNSNUMBER] = { 
			MVC_PROTOCOL, MVC_LOCALHOST, MVC_LOCALADDRESS, MVC_LOCALPORT, 
			MVC_STATE, MVC_REMOTEADDRESS, MVC_REMOTEPORT, MVC_REMOTEHOST,
			MVC_PID, MVC_PROGRAMNAME, MVC_PROGRAMCOMMAND 
		};
		memcpy(m->columns_initial_view_order, initial_order, sizeof(m->columns_initial_view_order));
	}
}

static gboolean connection_filtered(NetConnection *conn);

#define VALUE_OR_DEF(s, def) (((s)!=NULL) ? (s) : (def))
#define connection_visible(conn) ((Mwd.view_unestablished_connections || (conn)->state==NC_TCP_ESTABLISHED) \
                                  && connection_filtered(conn))


extern GladeXML *GladeXml;
static MainWindowData Mwd;


static void disable_update();
static void restore_update();


static gboolean check_int_range_complete (int *arr, int narr, int range_low, int range_hi)
{
	gboolean result = FALSE;
	if (range_hi >= range_low && narr == (range_hi-range_low+1))
	{
		int i, j;
		for(i=0; i<narr; i++)
		{
			if (arr[i] < range_low || arr[i] > range_hi)
				break;
			for(j=i+1; j<narr; j++)
				if (arr[i] == arr[j])
					break;
			if (j != narr)
				break;
		}
		result = (i == narr);
	}
	return result;
}


static ListLineUserData *list_line_user_data_new (GtkTreeIter *iter) {
	ListLineUserData *llud;
	llud = (ListLineUserData*)g_malloc0(sizeof(ListLineUserData));
	llud->iter = gtk_tree_iter_copy(iter);
	llud->addedtime = g_timer_new();
	llud->state = LLS_NEW;
	return llud;
}


static void list_line_user_data_delete (ListLineUserData *llud) {
	if (llud->iter!=NULL)
		gtk_tree_iter_free(llud->iter);
	if (llud->addedtime!=NULL)
		g_timer_destroy(llud->addedtime);
	if (llud->closedtime!=NULL)
		g_timer_destroy(llud->closedtime);
	g_free(llud);
}

gboolean update_connections_hosts_on_idle(gpointer data);

#define MAX_HOST_HASH_SIZE 1100100

static void host_loader_thread_func (gpointer data, gpointer user_data) {
	char *ip = (char*)data;
	char *host;
	
	host = get_host_name_by_address(ip);
	if (host==NULL)
		host = g_strdup("."); /*convention string for no host found*/
	
	if (Mwd.exit_requested)
	{
		g_free(host);
		return;
	}
	
	g_mutex_lock(Mwd.host_hash_lock);
	
	/* just enforcing the limit, no cache management
	 * normal usage patterns for a netactview instance are not more than a few hours long */
	if (g_hash_table_size(Mwd.ip_host_hash) >= MAX_HOST_HASH_SIZE)
	{
		g_hash_table_destroy(Mwd.ip_host_hash);
		Mwd.ip_host_hash = g_hash_table_new_full(&g_str_hash, &g_str_equal, &g_free, &g_free);
	}
	
	g_assert(g_hash_table_lookup(Mwd.ip_host_hash, ip)==NULL);
	g_hash_table_insert(Mwd.ip_host_hash, g_strdup(ip), host);
	g_hash_table_remove(Mwd.requested_ip_hash, ip); 
	ip = NULL; /*it became invalid on the previous line*/
	
	g_mutex_unlock(Mwd.host_hash_lock);
	
	g_idle_add(&update_connections_hosts_on_idle, NULL);
}

static void init_host_loader () {
	Mwd.ip_host_hash = g_hash_table_new_full(&g_str_hash, &g_str_equal, &g_free, &g_free);
	Mwd.requested_ip_hash = g_hash_table_new_full(&g_str_hash, &g_str_equal, &g_free, NULL);
	Mwd.host_hash_lock = g_mutex_new();
	
	Mwd.host_loader_pool = g_thread_pool_new(&host_loader_thread_func, NULL, 5, TRUE, NULL);
}

static void stop_host_loader () {
	g_thread_pool_free(Mwd.host_loader_pool, TRUE, TRUE);
	Mwd.host_loader_pool = NULL;
}

static void free_host_loader () {	
	g_hash_table_destroy(Mwd.ip_host_hash); Mwd.ip_host_hash = NULL;
	g_hash_table_destroy(Mwd.requested_ip_hash); Mwd.requested_ip_hash = NULL;
	g_mutex_free(Mwd.host_hash_lock); Mwd.host_hash_lock = NULL;
}

#define MAX_HOST_REQUEST_QUEUE_LEN 100100

static char *get_host (const char *ip) {
	char *host_name = NULL;
	if (Mwd.exit_requested)
		return NULL;
	
	g_mutex_lock(Mwd.host_hash_lock);
	
	host_name = (char*)g_hash_table_lookup(Mwd.ip_host_hash, ip);
	if (host_name == NULL && g_hash_table_lookup(Mwd.requested_ip_hash, ip) == NULL && 
	    g_hash_table_size(Mwd.requested_ip_hash) < MAX_HOST_REQUEST_QUEUE_LEN)
	{
		char *hash_ip = g_strdup(ip);
		g_hash_table_insert(Mwd.requested_ip_hash, hash_ip, (gpointer)1);
		g_thread_pool_push(Mwd.host_loader_pool, hash_ip, NULL);
	}
		
	g_mutex_unlock(Mwd.host_hash_lock);
	
	return (host_name != NULL) ? g_strdup(host_name) : NULL;
}

static gboolean update_net_connection_hosts (NetConnection *conn) {
	gboolean updated = FALSE;
	if (Mwd.view_local_host && conn->localhost == NULL && conn->localaddress != NULL)
	{
		conn->localhost = get_host(conn->localaddress);
		updated = (updated || (conn->localhost != NULL));
	}
	if (Mwd.view_remote_host && conn->remotehost == NULL && conn->remoteaddress != NULL)
	{
		conn->remotehost = get_host(conn->remoteaddress);
		updated = (updated || (conn->remotehost != NULL));
	}
	return updated;
}

static void get_connection_port_names (NetConnection *conn, char **slocalport, char **sremoteport) {
	if (!Mwd.view_port_names)
	{
		*slocalport = get_port_text(conn->localport);
		*sremoteport = get_port_text(conn->remoteport);
	}else
	{
		*slocalport = get_full_port_text(conn->protocol, conn->localport);
		*sremoteport = get_full_port_text(conn->protocol, conn->remoteport);
	}
}

static void update_ports_text ()
{
	int i;
	for (i=0; i<Mwd.connections->len; i++)
	{
		NetConnection *conn = g_array_index(Mwd.connections, NetConnection*, i);
		ListLineUserData *llud = (ListLineUserData*)conn->user_data;
		char *slocalport, *sremoteport;
		
		get_connection_port_names(conn, &slocalport, &sremoteport);
		
		gtk_list_store_set(Mwd.main_store, llud->iter, 
						   MVC_LOCALPORT, slocalport,
						   MVC_REMOTEPORT, sremoteport,
						   -1);
		g_free(slocalport);
		g_free(sremoteport);
	}
}

static void list_append_connection (NetConnection *conn) {
	char *slocalport, *sremoteport, spid[48]="";
	GtkTreeIter iter;
	
	update_net_connection_hosts(conn);
	
	get_connection_port_names(conn, &slocalport, &sremoteport);
	if (conn->programpid > 0)
		n_snprintf(spid, sizeof(spid), "%ld", conn->programpid);
	
	gtk_list_store_insert_with_values(Mwd.main_store, &iter, G_MAXINT,
		MVC_PROTOCOL, net_connection_get_protocol_name(conn),
		MVC_LOCALHOST, VALUE_OR_DEF(conn->localhost, ""),
		MVC_LOCALADDRESS, VALUE_OR_DEF(conn->localaddress, ""),
		MVC_LOCALPORT, slocalport,
		MVC_REMOTEADDRESS, VALUE_OR_DEF(conn->remoteaddress, ""), 
		MVC_REMOTEPORT, sremoteport,
		MVC_REMOTEHOST, VALUE_OR_DEF(conn->remotehost, ""),
		MVC_STATE, net_connection_get_state_name(conn),
		MVC_PID, spid,
		MVC_PROGRAMNAME, VALUE_OR_DEF(conn->programname, ""), 
		MVC_PROGRAMCOMMAND, VALUE_OR_DEF(conn->programcommand, ""),
		MVC_VISIBLE, TRUE,
		MVC_DATA, conn,
		MVC_COLOR, (Mwd.view_colors && !Mwd.first_refresh) ? DEFAULT_NEW_COLOR : NULL,
		-1);
	conn->user_data = list_line_user_data_new(&iter);
	
	gtk_list_store_set(Mwd.main_store, ((ListLineUserData*)conn->user_data)->iter, 
				       MVC_VISIBLE, connection_visible(conn),
				       -1);
	
	g_free(slocalport);
	g_free(sremoteport);
}

static void list_remove_connection (NetConnection *conn)
{
	ListLineUserData *llud;
	g_assert(conn!=NULL && conn->user_data!=NULL);
	llud = (ListLineUserData*)conn->user_data;
	gtk_list_store_remove(Mwd.main_store, llud->iter);
}

static void list_set_closed_connection (NetConnection *conn)
{
	ListLineUserData *llud;
	g_assert(conn!=NULL && conn->user_data!=NULL);
	llud = (ListLineUserData*)conn->user_data;
	
	if (llud->closedtime == NULL)
	{
		llud->closedtime = g_timer_new();
		conn->state = NC_TCP_CLOSED;
		llud->state = LLS_CLOSED;
		gtk_list_store_set(Mwd.main_store, llud->iter, 
						   MVC_STATE, net_connection_get_state_name(conn),
						   MVC_COLOR, Mwd.view_colors ? DEFAULT_CLOSED_COLOR : NULL,
						   -1);
		gtk_list_store_set(Mwd.main_store, llud->iter, 
		                   MVC_VISIBLE, connection_visible(conn), -1);
	}
}

static void list_update_connection (NetConnection *conn)
{
	ListLineUserData *llud;
	char spid[48]="";
	g_assert(conn!=NULL && conn->user_data!=NULL);
	
	if (conn->programpid > 0)
		n_snprintf(spid, sizeof(spid), "%ld", conn->programpid);
	
	llud = (ListLineUserData*)conn->user_data;
	gtk_list_store_set(Mwd.main_store, llud->iter, 
					   MVC_STATE, net_connection_get_state_name(conn),
					   MVC_LOCALHOST, VALUE_OR_DEF(conn->localhost, ""),
					   MVC_REMOTEHOST, VALUE_OR_DEF(conn->remotehost, ""),
					   MVC_PID, spid,
					   MVC_PROGRAMNAME, VALUE_OR_DEF(conn->programname, ""), 
					   MVC_PROGRAMCOMMAND, VALUE_OR_DEF(conn->programcommand, ""),
					   -1);
	gtk_list_store_set(Mwd.main_store, llud->iter, 
	                   MVC_VISIBLE, connection_visible(conn), -1);
}

static void list_free_net_connection (NetConnection *conn)
{
	if (conn != NULL)
	{
		ListLineUserData *llud = (ListLineUserData*)conn->user_data;
		g_assert(llud!=NULL);
		list_line_user_data_delete(llud);
		net_connection_delete(conn);
	}
}

static void delete_closed_connections ()
{
	int i;	
	for (i=(int)Mwd.connections->len-1; i>=0; i--)
	{
		NetConnection* conn = g_array_index(Mwd.connections, NetConnection*, i);
		if ((conn->operation == NC_OP_DELETE) && (!Mwd.keep_closed_connections))
		{
			list_remove_connection(conn);
			list_free_net_connection(conn);
			g_array_remove_index_fast(Mwd.connections, i);
		}
	}
}

static void update_closed_connections ()
{
	int i;
	for (i=(int)Mwd.connections->len-1; i>=0; i--)
	{
		NetConnection* conn = g_array_index(Mwd.connections, NetConnection*, i);
		if (conn->operation == NC_OP_DELETE)
		{
			ListLineUserData *llud = (ListLineUserData*)conn->user_data;
			g_assert(llud!=NULL && llud->closedtime!=NULL);
			if ((Mwd.manual_refresh || 
				g_timer_elapsed(llud->closedtime, NULL) > DEFAULT_CLOSED_SHOW_INT) && (!Mwd.keep_closed_connections))
			{
				list_remove_connection(conn);
				list_free_net_connection(conn);
				g_array_remove_index_fast(Mwd.connections, i);
			}
		}
	}
}

static void update_colors ()
{
	int i;
	for (i=0; i<Mwd.connections->len; i++)
	{
		NetConnection* conn = g_array_index(Mwd.connections, NetConnection*, i);
		ListLineUserData *llud = (ListLineUserData*)conn->user_data;
		g_assert(llud!=NULL && llud->addedtime!=NULL);
	
		if (llud->state == LLS_NEW)
		{
			if (Mwd.manual_refresh || 
				(g_timer_elapsed(llud->addedtime, NULL) > DEFAULT_NEW_SHOW_INT))
			{
				llud->state = LLS_NORMAL;
				gtk_list_store_set(Mwd.main_store, llud->iter, MVC_COLOR, NULL, -1);
			}
		}
	}
}

static void clear_colors ()
{
	int i;
	for (i=0; i<Mwd.connections->len; i++)
	{
		NetConnection* conn = g_array_index(Mwd.connections, NetConnection*, i);
		ListLineUserData *llud = (ListLineUserData*)conn->user_data;
		g_assert(llud!=NULL && llud->addedtime!=NULL);
		gtk_list_store_set(Mwd.main_store, llud->iter, MVC_COLOR, NULL, -1);
	}
}

#define UNSIGNED_DIFF(a, b) (((a)>(b)) ? (a)-(b) : 0)

static void refresh_net_statistics ()
{
	double telapsed = 0.;
	if (Mwd.statistics_timer == NULL || 
		(telapsed = g_timer_elapsed(Mwd.statistics_timer, NULL)) >= 1.)
	{
		unsigned long long bytes_received, bytes_sent, dbytes_received = 0, dbytes_sent = 0;
		char *bytes_text, *dbytes_text, *status_text;
		/*End white space used only to specify a bigger fixed size*/
		const char *sent_text_format = _("Sent: %s +%s/s        ");
		/*End white space used only to specify a bigger fixed size*/
		const char *receive_text_format = _("Received: %s +%s/s        ");
		NetStatistics new_stats;
		
		net_statistics_get(&new_stats);
		
		/* If this is the first refresh or the new statistics are invalid */
		/* [TODO] Statistics can be reset on suspend/resume. Besides this failsafe  
		 * code we need a way to get signaled on resume. Only with this code 
		 * still remain cases where we don't detect invalid values. */
		if ( (Mwd.statistics_timer == NULL) || 
		     (new_stats.bytes_received < Mwd.statistics.bytes_received) || 
		     (new_stats.bytes_sent < Mwd.statistics.bytes_sent) )
		{
			memcpy(&Mwd.statistics, &new_stats, sizeof(NetStatistics));
			memcpy(&Mwd.statistics_base, &new_stats, sizeof(NetStatistics));
			if (Mwd.statistics_timer == NULL)
				Mwd.statistics_timer = g_timer_new();
			else
				g_timer_reset(Mwd.statistics_timer);
			telapsed = 0.;
		}
		
		bytes_received = UNSIGNED_DIFF(new_stats.bytes_received, Mwd.statistics_base.bytes_received);
		bytes_sent = UNSIGNED_DIFF(new_stats.bytes_sent, Mwd.statistics_base.bytes_sent);

		if (telapsed > 0.1)
		{
			unsigned long long t = (unsigned long long)(telapsed*100);
			dbytes_received = UNSIGNED_DIFF(new_stats.bytes_received, Mwd.statistics.bytes_received);
			dbytes_sent = UNSIGNED_DIFF(new_stats.bytes_sent, Mwd.statistics.bytes_sent);
			dbytes_received = (dbytes_received * 100) / t;
			dbytes_sent = (dbytes_sent * 100) / t;
		}
		
		bytes_text = get_bytes_string(bytes_received);
		dbytes_text = get_bytes_string(dbytes_received);
		status_text = g_strdup_printf(receive_text_format, bytes_text, dbytes_text);
		match_string_utf8_len(&status_text, g_utf8_strlen(receive_text_format, -1));
		gtk_label_set_text(Mwd.label_received, status_text);
		g_free(bytes_text); g_free(dbytes_text); g_free(status_text);
		
		bytes_text = get_bytes_string(bytes_sent);
		dbytes_text = get_bytes_string(dbytes_sent);
		status_text = g_strdup_printf(sent_text_format, bytes_text, dbytes_text);
		match_string_utf8_len(&status_text, g_utf8_strlen(sent_text_format, -1));
		gtk_label_set_text(Mwd.label_sent, status_text);
		g_free(bytes_text); g_free(dbytes_text); g_free(status_text);
		
		memcpy(&(Mwd.statistics), &new_stats, sizeof(NetStatistics));
		g_timer_reset(Mwd.statistics_timer);
	}
}

static void refresh_established_conn (unsigned int nconn, unsigned int nestablished)
{
	char *status_text;
	/*End white space used only to specify a bigger fixed size*/
	const char *text_format = _("Established: %u/%u");
	
	status_text = g_strdup_printf(text_format, nestablished, nconn);
	match_string_utf8_len(&status_text, g_utf8_strlen(text_format, -1));
	gtk_label_set_text(Mwd.label_count, status_text);
	g_free(status_text);
}

static void refresh_visible_conn_label ()
{	
	int nrvisible = gtk_tree_model_iter_n_children(Mwd.main_store_filtered, NULL);
	char *text = g_strdup_printf(_("Visible: %u"), nrvisible);
	gtk_label_set_text(Mwd.label_visible, text);
	g_free(text);
}

static gboolean refresh_main_view_on_idle (gpointer data);

static gpointer connections_load_thread_func (gpointer data)
{
	while (!Mwd.exit_requested)
	{
		NetConnection *new_connections = NULL;
		int nr_new_connections = 0;
		
		g_mutex_lock(Mwd.refresh_request_lock);
		while (!Mwd.refresh_requested && !Mwd.exit_requested)
			g_cond_wait(Mwd.refresh_request_cond, Mwd.refresh_request_lock);
		Mwd.refresh_requested = FALSE;
		g_mutex_unlock(Mwd.refresh_request_lock);
		
		if (Mwd.exit_requested)
			break;
		
		new_connections = NULL;
		nr_new_connections = get_net_connections(&new_connections);
		
		g_mutex_lock(Mwd.loaded_conn_lock);
		
		if (Mwd.latest_connections != NULL)
			free_net_connections(Mwd.latest_connections, Mwd.nr_latest_connections);
		Mwd.latest_connections = new_connections;
		Mwd.nr_latest_connections = nr_new_connections;
		
		g_mutex_unlock(Mwd.loaded_conn_lock);
	
		g_idle_add(&refresh_main_view_on_idle, NULL);
	}
	return NULL;
}

static void init_connections_loader ()
{
	Mwd.refresh_request_lock = g_mutex_new();
	Mwd.loaded_conn_lock = g_mutex_new();
	Mwd.refresh_request_cond = g_cond_new();
	
	Mwd.data_load_thread = g_thread_create(&connections_load_thread_func, NULL,
										   TRUE, NULL);
	g_assert(Mwd.data_load_thread != NULL);
}

static void stop_connections_loader ()
{
	Mwd.exit_requested = TRUE;
	g_cond_signal(Mwd.refresh_request_cond);
	g_thread_join(Mwd.data_load_thread);
	Mwd.data_load_thread = NULL;
}

static void free_connections_loader ()
{
	g_mutex_free(Mwd.refresh_request_lock);
	g_mutex_free(Mwd.loaded_conn_lock);
	g_cond_free(Mwd.refresh_request_cond);
	
	if (Mwd.latest_connections != NULL)
	{
		free_net_connections(Mwd.latest_connections, Mwd.nr_latest_connections);
		Mwd.latest_connections = NULL; Mwd.nr_latest_connections = 0;
	}
}


static void refresh_main_view (void)
{
	unsigned int i, nvalid_conn = 0, nestablished_conn = 0;
	
	if (Mwd.view_colors)
		update_colors();
	if (Mwd.show_closed_connections)
		update_closed_connections();
	
	g_mutex_lock(Mwd.loaded_conn_lock);
	
	net_connection_update_list_full(Mwd.connections, Mwd.latest_connections, 
									Mwd.nr_latest_connections);
	
	g_mutex_unlock(Mwd.loaded_conn_lock);
	
	for(i=0; i<Mwd.connections->len; i++)
	{
		NetConnection* conn = g_array_index(Mwd.connections, NetConnection*, i);
		
		switch(conn->operation)
		{
		case NC_OP_INSERT:
			list_append_connection(conn);
			break;
		case NC_OP_UPDATE:
			list_update_connection(conn);
			break;
		case NC_OP_DELETE:
			list_set_closed_connection(conn);
			break;
		}
		
		if (conn->operation != NC_OP_DELETE)
		{
			nvalid_conn++;
			if (conn->state == NC_TCP_ESTABLISHED)
				nestablished_conn++;
		}
	}
	if (!Mwd.show_closed_connections)
		delete_closed_connections();
	
	refresh_established_conn(nvalid_conn, nestablished_conn);
	refresh_visible_conn_label();
	refresh_net_statistics();
	Mwd.first_refresh = FALSE;
	Mwd.manual_refresh = FALSE;
}


static gboolean refresh_main_view_on_idle (gpointer data)
{
	if (Mwd.exit_requested)
		return FALSE;
	if (Mwd.update_disabled)
		return FALSE;
	
	refresh_main_view();
	
	return FALSE;
}


static void refresh_connections ()
{
	g_mutex_lock(Mwd.refresh_request_lock);
	Mwd.refresh_requested = TRUE;
	g_cond_signal(Mwd.refresh_request_cond);
	g_mutex_unlock(Mwd.refresh_request_lock);
}

static void manual_refresh_connections ()
{
	Mwd.manual_refresh = TRUE;
	refresh_connections();
}


static void update_connections_hosts()
{
	int i;
	
	if (Mwd.view_local_host || Mwd.view_remote_host)
	{
		for (i=0; i<Mwd.connections->len; i++)
		{
			NetConnection *conn = g_array_index(Mwd.connections, NetConnection*, i);					
			g_assert(conn->user_data!=NULL);
			
			if ( (Mwd.view_local_host && conn->localhost==NULL) || 
				(Mwd.view_remote_host && conn->remotehost==NULL) )
			{
				if (update_net_connection_hosts(conn))
					list_update_connection(conn);
			}
		}
	}
}

/* Get the visible columns indexes sorted by the columns positions.
 */
static GArray *get_visible_columns_indexes (void)
{
	int i;
	GArray *visible_col_idx = g_array_sized_new(FALSE, FALSE, sizeof(int), 16);
	
	for (i=0; i<MVC_VIEW_COLUMNSNUMBER; i++)
	{
		GtkTreeViewColumn *column;
		
		column = gtk_tree_view_get_column(GTK_TREE_VIEW(Mwd.main_view), i);
		g_assert(column != NULL);
		
		if (gtk_tree_view_column_get_visible(column))
		{
			int columnindex = (int)(long)g_hash_table_lookup(Mwd.column_to_index_hash, column);
			g_array_append_val(visible_col_idx, columnindex);
		}
	}
	return visible_col_idx;
}

static char* get_store_value (GtkTreeIter *iter, int columnindex)
{
	GValue value = {0, };
	gtk_tree_model_get_value(GTK_TREE_MODEL(Mwd.main_store), iter, 
							 columnindex, &value);
	char* result = g_value_dup_string(&value);
	g_value_unset (&value);
	return result;
}

static void append_get_store_value (GtkTreeIter *iter, int columnindex, 
									GString *s, const char *format)
{
	GValue value = {0, };
	gtk_tree_model_get_value(GTK_TREE_MODEL(Mwd.main_store), iter, 
							 columnindex, &value);
	g_string_append_printf(s, format, g_value_get_string(&value));
	g_value_unset (&value);
}

static GString *get_line_column_text_4filter (GtkTreeIter *iter, const int *columnindexes, int nindexes)
{
	int i;
	GString *text;
	
	text = g_string_new("   ");
	
	for (i=0; i<nindexes; i++)
	{
		append_get_store_value(iter, columnindexes[i], text, "%s");
		g_string_append(text, "   ");
	}
	
	return text;
}

static char*** get_selected_lines_matrix (const int *columnindexes, int nindexes, int *nlines)
{
	GtkTreeSelection *selection;
	GtkTreeModel *selection_model;
	GList *selected_rows;
	char ***lines_matrix = NULL;
	*nlines = 0;

	g_assert(nindexes > 0);
	
	selection = gtk_tree_view_get_selection(Mwd.main_view);
	selected_rows = gtk_tree_selection_get_selected_rows(selection, &selection_model);
	
	if (selected_rows != NULL)
	{
		GList *row = selected_rows;
		int rowidx;
		*nlines = g_list_length(selected_rows);
		g_assert(*nlines > 0);

		lines_matrix = (char***)g_malloc(sizeof(char**) * (*nlines));

		rowidx = 0;
		while (row != NULL)
		{		
			int i;
			GtkTreeIter iter;
			GtkTreePath *selected_path = (GtkTreePath*)row->data, *row_path = NULL;
		
			row_path = gtk_tree_model_filter_convert_path_to_child_path(
							GTK_TREE_MODEL_FILTER(selection_model), selected_path);
			gtk_tree_model_get_iter(GTK_TREE_MODEL(Mwd.main_store), &iter, row_path);

			char **line_list = (char**)g_malloc(sizeof(char*) * nindexes);		
			for (i=0; i<nindexes; i++)
				line_list[i] = get_store_value(&iter, columnindexes[i]);
			lines_matrix[rowidx] = line_list;
		
			gtk_tree_path_free(row_path);
		
			row = row->next;
			rowidx++;
		}		
		
		for(row=selected_rows; row!=NULL; row=row->next)
			gtk_tree_path_free((GtkTreePath*)row->data);
		g_list_free (selected_rows);
	}
	return lines_matrix;
}

static void free_string_matrix (char ***str_matrix, int nl, int nc)
{
	if (str_matrix == NULL)
		return;
	int i, j;
	for(i=0; i<nl; i++)
		for(j=0; j<nc; j++)
			g_free(str_matrix[i][j]);
	for(i=0; i<nl; i++)
		g_free(str_matrix[i]);
	g_free(str_matrix);
}

static GString *get_selected_lines_column_text_4copy (const int *columnindexes, int nindexes)
{
	GtkTreeSelection *selection;
	GtkTreeModel *selection_model;
	GList *selected_rows;
	GString *text = NULL;
	
	selection = gtk_tree_view_get_selection(Mwd.main_view);
	selected_rows = gtk_tree_selection_get_selected_rows(selection, &selection_model);
	
	if (selected_rows != NULL)
	{
		GList *row = selected_rows;
		text = g_string_new("");
		
		while (row != NULL)
		{		
			int i;
			GtkTreeIter iter;
			GtkTreePath *selected_path = (GtkTreePath*)row->data, *row_path = NULL;
			
			row_path = gtk_tree_model_filter_convert_path_to_child_path(
							GTK_TREE_MODEL_FILTER(selection_model), selected_path);
			gtk_tree_model_get_iter(GTK_TREE_MODEL(Mwd.main_store), &iter, row_path);
			
			for (i=0; i<nindexes; i++)
			{
				if (i > 0)
					g_string_append(text, " \t");
				append_get_store_value(&iter, columnindexes[i], text, "%s");
			}
			
			gtk_tree_path_free(row_path);
			
			row = row->next;
			if (row != NULL)
				g_string_append(text, "\n");
		}
		
		for(row=selected_rows; row!=NULL; row=row->next)
			gtk_tree_path_free((GtkTreePath*)row->data);
		g_list_free (selected_rows);
	}
	return text;
}

static void copy_selected_lines_column (int column)
{
	int columns[] = { column };
	GString *text;
	text = get_selected_lines_column_text_4copy(columns, 1);
	if (text != NULL)
	{
		set_clipboard_text(text->str);
		g_string_free(text, TRUE);
	}
}

static void copy_selected_lines ()
{
	GArray *visible_col_idx;
	visible_col_idx = get_visible_columns_indexes();
	
	if (visible_col_idx->len > 0)
	{
		GString *text;
		text = get_selected_lines_column_text_4copy((int*)visible_col_idx->data, visible_col_idx->len);
		if (text != NULL)
		{
			set_clipboard_text(text->str);
			g_string_free(text, TRUE);
		}
	}
	
	g_array_free(visible_col_idx, TRUE);
}


static gboolean connection_filtered (NetConnection *conn)
{
	g_assert(conn->user_data!=NULL);
	if (Mwd.filtering && Mwd.filter->len > 0)
	{
		ListLineUserData *llud;
		GArray *visible_col_idx;
		GString* text;
		gboolean filtered;
		
		llud = (ListLineUserData*)conn->user_data;
		
		visible_col_idx = get_visible_columns_indexes();
		text = get_line_column_text_4filter(llud->iter, (int*)visible_col_idx->data, visible_col_idx->len);

		if (Mwd.caseSensitiveFilter)
			filtered = IsFiltered(text->str, Mwd.filterTree, casSensitive);
		else
		{
			gchar *entryNcText = g_utf8_casefold(text->str, -1);
			filtered = IsFiltered(entryNcText, Mwd.filterTreeNoCase, casSensitive);
			g_free(entryNcText);
		}
		
		g_string_free(text, TRUE);
		g_array_free(visible_col_idx, TRUE);
		
		return filtered;
	}else
		return TRUE;
}

static void update_connections_visibility ()
{
	int i;
	for (i=0; i<Mwd.connections->len; i++)
	{
		ListLineUserData *llud;
		NetConnection *conn = g_array_index(Mwd.connections, NetConnection*, i);					
		g_assert(conn->user_data!=NULL);
		llud = (ListLineUserData*)conn->user_data;
		
		gtk_list_store_set(Mwd.main_store, llud->iter, 
						   MVC_VISIBLE, connection_visible(conn),
						   -1);
	}
	refresh_visible_conn_label();
}

static void FreeFilterData ()
{
	if (Mwd.filterMask != NULL)
		g_free(Mwd.filterMask);
	Mwd.filterMask = NULL;
	if (Mwd.filterTree != NULL)
		FreeFilter(Mwd.filterTree);
	Mwd.filterTree = NULL;
	if (Mwd.filterTreeNoCase != NULL)
		FreeFilter(Mwd.filterTreeNoCase);
	Mwd.filterTreeNoCase = NULL;
}

static void update_filter ()
{	
	char *filterText = Mwd.filter->str;
	FreeFilterData();
	if (Mwd.filterOperators)
		Mwd.filterTree = ParseFilter(filterText, &Mwd.filterMask);
	else
		Mwd.filterTree = ParseFilterNoOperators(filterText);
	if (!Mwd.caseSensitiveFilter)
		Mwd.filterTreeNoCase = CaseFoldFilterUTF8(Mwd.filterTree);

	update_connections_visibility();	
}

static void clear_filter ()
{
	GtkEntry *filter_entry;
	filter_entry = GTK_ENTRY(glade_xml_get_widget(GladeXml, "txtFilter"));
	
	gtk_entry_set_text(filter_entry, "");
}

static void update_columns_initial_view_order ()
{
	int i;
	for (i=0; i<MVC_VIEW_COLUMNSNUMBER; i++)
	{
		GtkTreeViewColumn *column = gtk_tree_view_get_column(GTK_TREE_VIEW(Mwd.main_view), i);
		int columnindex;
		g_assert(column != NULL);
		columnindex = (int)(long)g_hash_table_lookup(Mwd.column_to_index_hash, column);
		Mwd.columns_initial_view_order[i] = columnindex;
	}
}


static char *get_config_file_location ()
{
	char *homedir = get_effective_home_dir();
	if (homedir != NULL)
	{
		char *config_file_location = g_strdup_printf("%s/.netactview", homedir);
		g_free(homedir);
		return config_file_location;
	}else
		return NULL;
}

#define MAX_CONFIG_FILE_SIZE 1100100

static GKeyFile *load_config_file ()
{
	GKeyFile *config_file = NULL;
	FileReadBuf readBuf = {};
	char *config_file_location;
	
	config_file_location = get_config_file_location();
	if (config_file_location == NULL)
		return NULL;
	
	readBuf = read_file_ex(config_file_location, MAX_CONFIG_FILE_SIZE, 1024);
	if (readBuf.isComplete)
	{
		config_file = g_key_file_new();
		if (!g_key_file_load_from_data(config_file, readBuf.data, readBuf.dataLen, G_KEY_FILE_NONE, NULL))
		{
			g_key_file_free(config_file);
			config_file = NULL;
		}
	}

	file_readbuf_free_data(&readBuf);
	g_free(config_file_location);
	
	return config_file;
}

static void save_config_file (GKeyFile *config_file)
{
	gsize length = 0;
	char *data = g_key_file_to_data(config_file, &length, NULL);
	if (data != NULL)
	{
		char *config_file_location = get_config_file_location();
		if (config_file_location != NULL)
		{
			FILE *f = fopen(config_file_location, "w");
			if (f != NULL)
			{
				fwrite(data, length, 1, f);
				fclose(f);
			}	
			g_free(config_file_location);
		}
		g_free(data);
	}
}

static gboolean get_boolean_preference (GKeyFile *config_file, const char *group,
									   const char *key, gboolean *preference)
{
	gboolean value;
	GError *error = NULL;
	value = g_key_file_get_boolean(config_file, group, key, &error);
	if (error != NULL)
	{
		g_error_free(error);
		return FALSE;
	}else
	{
		*preference = value;
		return TRUE;
	}
}

static gboolean get_int_preference (GKeyFile *config_file, const char *group,
								   const char *key, int *preference)
{
	gint value;
	GError *error = NULL;
	value = g_key_file_get_integer(config_file, group, key, &error);
	if (error != NULL)
	{
		g_error_free(error);
		return FALSE;
	}else
	{
		*preference = value;
		return TRUE;
	}
}

static void load_preferences ()
{
	GKeyFile *config_file;
	
	DropToSudoData *dtosH = drop_to_sudo_user();
	config_file = load_config_file();
	
	restore_initial_user(dtosH);
	if (config_file != NULL)
	{
		char *svalue;
		int intvalue;
		int *columns_order;
		gsize ncolumns;
		
		get_boolean_preference(config_file, "View", "RemoteHost", &Mwd.view_remote_host);
		get_boolean_preference(config_file, "View", "LocalHost", &Mwd.view_local_host);
		get_boolean_preference(config_file, "View", "LocalAddress", &Mwd.view_local_address);
		get_boolean_preference(config_file, "View", "Command", &Mwd.view_command);
		get_boolean_preference(config_file, "View", "PortName", &Mwd.view_port_names);
		get_boolean_preference(config_file, "View", "UnestablishedConn", &Mwd.view_unestablished_connections);
		get_boolean_preference(config_file, "View", "KeepClosedConn", &Mwd.keep_closed_connections);
		get_boolean_preference(config_file, "View", "ClosedConn", &Mwd.show_closed_connections);
		get_boolean_preference(config_file, "View", "Colors", &Mwd.view_colors);

		get_int_preference(config_file, "View", "WindowWidth", &Mwd.initial_window_width);
		get_int_preference(config_file, "View", "WindowHeight", &Mwd.initial_window_height);
		get_boolean_preference(config_file, "View", "WindowMaximized", &Mwd.window_maximized);
		Mwd.window_width = Mwd.initial_window_width;
		Mwd.window_height = Mwd.initial_window_height;
		
		get_boolean_preference(config_file, "Edit", "AutoRefresh", &Mwd.auto_refresh);
		svalue = g_key_file_get_string(config_file, "Edit", "AutoRefreshIntervalMenu", NULL);
		if (svalue != NULL)
		{
			if (strlen(svalue)<sizeof(Mwd.sel_arinterval_menu) && 
				strncmp(svalue, "menuAutoRefresh", strlen("menuAutoRefresh"))==0)
			{
				n_strlcpy(Mwd.sel_arinterval_menu, svalue, sizeof(Mwd.sel_arinterval_menu));
			}
			g_free(svalue); svalue = NULL;
		}
		
		get_boolean_preference(config_file, "Edit", "Filtering", &Mwd.filtering);
		get_boolean_preference(config_file, "Edit", "CaseSensitiveFilter", &Mwd.caseSensitiveFilter);
		get_boolean_preference(config_file, "Edit", "FilterOperators", &Mwd.filterOperators);
		svalue = g_key_file_get_string(config_file, "Edit", "Filter", NULL);
		if (svalue != NULL)
		{
			g_string_assign(Mwd.filter, svalue);
			g_free(svalue); svalue = NULL;
		}
		
		get_int_preference(config_file, "MainView", "SortColumnIndex", &intvalue);
		if (intvalue >= 0 && intvalue < MVC_VIEW_COLUMNSNUMBER)
			Mwd.current_sort_column = intvalue;
		get_int_preference(config_file, "MainView", "SortDirection", &intvalue);
		if (intvalue == GTK_SORT_ASCENDING || intvalue == GTK_SORT_DESCENDING)
			Mwd.current_sort_direction = intvalue;
		
		columns_order = g_key_file_get_integer_list(config_file, "MainView", "ColumnsOrder", 
												   &ncolumns, NULL);
		if (columns_order != NULL)
		{
			if (ncolumns == MVC_VIEW_COLUMNSNUMBER)
			{
				if (check_int_range_complete(columns_order, ncolumns, 0, MVC_VIEW_COLUMNSNUMBER-1))
					memcpy(Mwd.columns_initial_view_order, columns_order, sizeof(Mwd.columns_initial_view_order));
			}
			g_free(columns_order); columns_order = NULL;
		}
		
		g_key_file_free(config_file);
	}
}

static void save_preferences ()
{
	GKeyFile *config_file = g_key_file_new();
	DropToSudoData *dtosH = NULL;

	g_key_file_set_boolean(config_file, "View", "RemoteHost", Mwd.view_remote_host);
	g_key_file_set_boolean(config_file, "View", "LocalHost", Mwd.view_local_host);
	g_key_file_set_boolean(config_file, "View", "LocalAddress", Mwd.view_local_address);
	g_key_file_set_boolean(config_file, "View", "Command", Mwd.view_command);
	g_key_file_set_boolean(config_file, "View", "PortName", Mwd.view_port_names);
	g_key_file_set_boolean(config_file, "View", "UnestablishedConn", Mwd.view_unestablished_connections);
	g_key_file_set_boolean(config_file, "View", "ClosedConn", Mwd.show_closed_connections);
	g_key_file_set_boolean(config_file, "View", "KeepClosedConn", Mwd.keep_closed_connections);
	g_key_file_set_boolean(config_file, "View", "Colors", Mwd.view_colors);
	if (Mwd.window_maximized)
	{
		Mwd.window_width = Mwd.initial_window_width;
		Mwd.window_height = Mwd.initial_window_height;
	}
	g_key_file_set_integer(config_file, "View", "WindowWidth", Mwd.window_width);
	g_key_file_set_integer(config_file, "View", "WindowHeight", Mwd.window_height);
	g_key_file_set_boolean(config_file, "View", "WindowMaximized", Mwd.window_maximized);	
	g_key_file_set_boolean(config_file, "Edit", "AutoRefresh", Mwd.auto_refresh);
	g_key_file_set_string(config_file, "Edit", "AutoRefreshIntervalMenu", Mwd.sel_arinterval_menu);
	g_key_file_set_boolean(config_file, "Edit", "Filtering", Mwd.filtering);
	g_key_file_set_boolean(config_file, "Edit", "CaseSensitiveFilter", Mwd.caseSensitiveFilter);
	g_key_file_set_boolean(config_file, "Edit", "FilterOperators", Mwd.filterOperators);
	g_key_file_set_string(config_file, "Edit", "Filter", Mwd.filter->str);
	g_key_file_set_integer(config_file, "MainView", "SortColumnIndex", Mwd.current_sort_column);
	g_key_file_set_integer(config_file, "MainView", "SortDirection", Mwd.current_sort_direction);
	
	update_columns_initial_view_order();
	g_key_file_set_integer_list(config_file, "MainView", "ColumnsOrder", Mwd.columns_initial_view_order,
								MVC_VIEW_COLUMNSNUMBER);
	g_key_file_set_comment(config_file, "MainView", "ColumnsOrder", "View positions at index", NULL);
	
	dtosH = drop_to_sudo_user();
	save_config_file(config_file);
	
	restore_initial_user(dtosH);
	g_key_file_free(config_file);
}

static void set_menu_preferences ()
{
	GtkCheckMenuItem *checkMenuItem;
	
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewHostName")), 
								   Mwd.view_remote_host);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewLocalHostName")), 
								   Mwd.view_local_host);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewLocalAddress")), 
								   Mwd.view_local_address);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewCommand")), 
								   Mwd.view_command);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewPortName")), 
								   Mwd.view_port_names);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewUnestablishedConn")), 
								   Mwd.view_unestablished_connections);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewDeletedConn")), 
								   Mwd.show_closed_connections);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewKeepDeletedConn")), 
								   Mwd.keep_closed_connections);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewColors")), 
								   Mwd.view_colors);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuAutoRefreshEnabled")), 
								   Mwd.auto_refresh);
	checkMenuItem = GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, Mwd.sel_arinterval_menu));
	if (checkMenuItem != NULL && gtk_check_menu_item_get_draw_as_radio(checkMenuItem))
		gtk_check_menu_item_set_active(checkMenuItem, TRUE);
	
#ifdef HAVE_GKSU
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "menuAdminMode"), 
							 (gboolean)(geteuid()!=0));
#else
	gtk_widget_hide(glade_xml_get_widget(GladeXml, "menuAdminMode"));
#endif
	
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuFilter")),
								   Mwd.filtering);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(glade_xml_get_widget(GladeXml, "btnCaseSensitive")),
	                             Mwd.caseSensitiveFilter);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(glade_xml_get_widget(GladeXml, "btnOperators")),
	                             Mwd.filterOperators);
	if (Mwd.filtering && Mwd.filter->len > 0)
	{
		GtkEntry *filter_entry = GTK_ENTRY(glade_xml_get_widget(GladeXml, "txtFilter"));
		char *filter_data = g_strdup(Mwd.filter->str); /*need this as filter is modified by text changed*/
		gtk_entry_set_text(filter_entry, filter_data);
		g_free(filter_data);
	}
}


static gboolean on_AutoRefresh_timeout (gpointer data)
{
	if (Mwd.exit_requested)
		return FALSE;
	if ( (!Mwd.auto_refresh) || (Mwd.auto_refresh_id != (unsigned)(uintptr_t)data) )
		return FALSE;
	if (Mwd.update_disabled)
		return TRUE;
	
	refresh_connections();
	
	return TRUE;
}

static void update_auto_refresh ()
{
	if (Mwd.auto_refresh)
	{
		Mwd.auto_refresh_id++;
		g_timeout_add(Mwd.auto_refresh_interval, &on_AutoRefresh_timeout, 
		              (gpointer)(uintptr_t)Mwd.auto_refresh_id);
	}
}

static void set_auto_refresh (gboolean refresh)
{
	if (refresh)
		refresh_connections();
	Mwd.auto_refresh = refresh;
	update_auto_refresh ();
}

static void set_auto_refresh_interval (unsigned interval)
{
	Mwd.auto_refresh_interval = interval;
	update_auto_refresh();
}

gboolean update_connections_hosts_on_idle (gpointer data)
{
	if (Mwd.exit_requested)
		return FALSE;
	
	update_connections_hosts();
	
	return FALSE;
}


gboolean main_store_line_visible (GtkTreeIter *iter)
{
	gboolean result;
	GValue value = {0,};
	gtk_tree_model_get_value(GTK_TREE_MODEL(Mwd.main_store), iter, MVC_VISIBLE, &value);
	result = g_value_get_boolean(&value);
	g_value_unset(&value);
	return result;
}

static GString *get_saved_line_text (GtkTreeIter *iter)
{
	NetConnection *conn;
	GString *s = g_string_new("");
	GValue value = {0, };
	char *slocalport, *sremoteport, spid[48]="`";
	
	gtk_tree_model_get_value(GTK_TREE_MODEL(Mwd.main_store), iter, MVC_DATA, &value);
	conn = (NetConnection*)g_value_get_pointer(&value);
	g_assert(conn != NULL);
	
	slocalport = get_port_text(conn->localport);
	sremoteport = get_port_text(conn->remoteport);
	if (conn->programpid > 0) 
		n_snprintf(spid, sizeof(spid), "%ld", conn->programpid);
	
	g_string_append_printf(s, "%-5s  ", net_connection_get_protocol_name(conn));
	g_string_append_printf(s, "%16s : ", VALUE_OR_DEF(conn->localaddress, "`"));
	g_string_append_printf(s, "%-5s   ", slocalport);
	g_string_append_printf(s, "%-12s ", net_connection_get_state_name(conn));
	g_string_append_printf(s, "%16s : ", VALUE_OR_DEF(conn->remoteaddress, "`"));
	g_string_append_printf(s, "%-5s  ", sremoteport);
	g_string_append_printf(s, "%-20s   ", VALUE_OR_DEF(conn->remotehost, "`"));
	g_string_append_printf(s, "%-1s  ", VALUE_OR_DEF(conn->localhost, "`"));
	g_string_append_printf(s, "%s  ", spid);
	g_string_append_printf(s, "%s   ", VALUE_OR_DEF(conn->programname, "`"));
	g_string_append_printf(s, "%s", VALUE_OR_DEF(conn->programcommand, "`"));
	
	g_free(slocalport);
	g_free(sremoteport);
	g_value_unset(&value);
	return s;
}

gboolean write_saved_data_text (FILE *f, gboolean new_file, int *write_error)
{
	GtkTreeIter iter;
	gboolean biter;
	int i, nch;
	time_t time_val = 0;
	struct tm t = {};
	size_t ftres = 0;
	char time_str[256];
	
	if (new_file)
	{
		nch = fprintf(f, /*Header for the saved file. Keep the formatting intact.*/ 
				_("Protocol   Local Address : Local Port   State   Remote Address : Remote Port   "
				"Remote Host   Local Host   Pid   Program   Command"));
		if (nch < 0) goto error_label;
		if ( fprintf(f, "\n") < 0) goto error_label;
		for (i=0; i<nch; i++) 
			if ( fprintf(f, "-") < 0) goto error_label;
		if ( fprintf(f, "\n\n") < 0) goto error_label;
	}
	
	time(&time_val);
	localtime_r(&time_val, &t);
	ftres = strftime(time_str, sizeof(time_str), "%F  %T", &t);
	ERROR_IF(ftres == 0);
	nch = fprintf(f, "%s\n", time_str);
	if (nch < 0) goto error_label;
	for (i=0; i<nch; i++) 
		if ( fprintf(f, "-") < 0) goto error_label;
	if ( fprintf(f, "\n") < 0) goto error_label;
	
	biter = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(Mwd.main_store), &iter);
	while (biter)
	{
		if (main_store_line_visible(&iter))
		{
			GString *line_text;
			line_text = get_saved_line_text(&iter);
			nch = fprintf(f, "%s\n", line_text->str);
			g_string_free(line_text, TRUE);
			if (nch < 0) goto error_label;
		}
		
		biter = gtk_tree_model_iter_next(GTK_TREE_MODEL(Mwd.main_store), &iter);
	}
	if ( fprintf(f, "\n") < 0) goto error_label;
	
	return TRUE;
	
error_label:
	*write_error = errno;
	return FALSE;
}

static GString *get_saved_line_csv (GtkTreeIter *iter, struct tm *t)
{
	NetConnection *conn;
	GString *s = g_string_new("");
	GValue value = {0, };
	char *slocalport, *sremoteport, spid[48]="", *slocalportname, *sremoteportname;
	char *sprogramname, *sprogramcommand;
	char time_str[128], date_str[128];
	size_t ftres1, ftres2;
	
	ftres1 = strftime(time_str, sizeof(time_str), "%T", t);
	ftres2 = strftime(date_str, sizeof(date_str), "%F", t);
	ERROR_IF(ftres1 == 0 || ftres2 == 0);
	
	gtk_tree_model_get_value(GTK_TREE_MODEL(Mwd.main_store), iter, MVC_DATA, &value);
	conn = (NetConnection*)g_value_get_pointer(&value);
	g_assert(conn != NULL);
	
	slocalport = get_port_text(conn->localport);
	slocalportname = get_port_name(conn->protocol, conn->localport);
	sremoteport = get_port_text(conn->remoteport);
	sremoteportname = get_port_name(conn->protocol, conn->remoteport);
	if (conn->programpid > 0) 
		n_snprintf(spid, sizeof(spid), "%ld", conn->programpid);
	sprogramname = string_replace(VALUE_OR_DEF(conn->programname, ""), "\"", "\"\"");
	sprogramcommand = string_replace(VALUE_OR_DEF(conn->programcommand, ""), "\"", "\"\"");

	g_string_append_printf(s, "\"%s\",", date_str);
	g_string_append_printf(s, "\"%s\",", time_str);
	g_string_append_printf(s, "\"%s\",", net_connection_get_protocol_name(conn));
	g_string_append_printf(s, "\"%s\",", VALUE_OR_DEF(conn->localaddress, ""));
	g_string_append_printf(s, "\"%s\",", slocalport);
	g_string_append_printf(s, "\"%s\",", net_connection_get_state_name(conn));
	g_string_append_printf(s, "\"%s\",", VALUE_OR_DEF(conn->remoteaddress, ""));
	g_string_append_printf(s, "\"%s\",", sremoteport);
	g_string_append_printf(s, "\"%s\",", VALUE_OR_DEF(conn->remotehost, ""));	
	g_string_append_printf(s, "\"%s\",", spid);
	g_string_append_printf(s, "\"%s\",", sprogramname);
	g_string_append_printf(s, "\"%s\",", sprogramcommand);
	g_string_append_printf(s, "\"%s\",", VALUE_OR_DEF(conn->localhost, ""));
	g_string_append_printf(s, "\"%s\",", VALUE_OR_DEF(slocalportname, ""));
	g_string_append_printf(s, "\"%s\"", VALUE_OR_DEF(sremoteportname, ""));
	
	g_free(slocalport);
	g_free(slocalportname);
	g_free(sremoteport);
	g_free(sremoteportname);
	g_free(sprogramname);
	g_free(sprogramcommand);
	g_value_unset(&value);
	return s;
}

gboolean write_saved_data_csv (FILE *f, gboolean new_file, int *write_error)
{
	GtkTreeIter iter;
	gboolean biter;
	int nch;
	time_t time_val = 0;
	struct tm t = {};
	
	if (new_file)
	{
		nch = fprintf(f, /*Header for the saved CSV file. Keep the formatting intact. Do not add additional double quotes.*/ 
				_("\"Date\",\"Time\",\"Protocol\",\"Local Address\",\"Local Port\",\"State\",\"Remote Address\",\"Remote Port\","
				"\"Remote Host\",\"Pid\",\"Program\",\"Command\",\"Local Host\",\"Local Port Name\",\"Remote Port Name\""));
		if (nch < 0) goto error_label;
		if ( fprintf(f, "\n") < 0) goto error_label;
	}
	
	time(&time_val);
	localtime_r(&time_val, &t);	
	
	biter = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(Mwd.main_store), &iter);
	while (biter)
	{
		if (main_store_line_visible(&iter))
		{
			GString *line_text;
			line_text = get_saved_line_csv(&iter, &t);
			nch = fprintf(f, "%s\n", line_text->str);
			g_string_free(line_text, TRUE);
			if (nch < 0) goto error_label;
		}
		
		biter = gtk_tree_model_iter_next(GTK_TREE_MODEL(Mwd.main_store), &iter);
	}
	if ( fprintf(f, "\n") < 0) goto error_label;
	
	return TRUE;
	
error_label:
	*write_error = errno;
	return FALSE;
}

gboolean write_saved_data (const char *path, gboolean new_file, int *write_error)
{
	gboolean savedok = TRUE;
	DropToSudoData *dtosH = NULL;
	FILE *f = NULL;
	
	/*If root try first to write the file with the sudo user so that it is easilly accessible to the sudo user*/
	dtosH = drop_to_sudo_user();
	f = fopen(path, (new_file) ? "w" : "a");
	if (f == NULL && dtosH != NULL)
	{
		restore_initial_user(dtosH); /*Try again with full rights*/
		dtosH = NULL;
		f = fopen(path, (new_file) ? "w" : "a");
	}
		
	if (f != NULL)
	{
		const char *ext = get_file_extension(path);
		if (strcasecmp(ext, "csv") == 0)
			savedok = write_saved_data_csv(f, new_file, write_error);
		else
			savedok = write_saved_data_text(f, new_file, write_error);
		
		fclose(f);
	}else
	{
		savedok = FALSE;
		*write_error = errno;
	}
	
	restore_initial_user(dtosH);
	
	return savedok;
}

typedef struct
{
	GtkFileFilter *allFiles;
	GtkFileFilter *textFiles;
	GtkFileFilter *csvFiles;
}SaveDialogFilters;

static void save_dialog_filter_changed_cb (GObject *object, 
       GParamSpec *spec, gpointer data)
{
	GtkFileChooser *save_dialog = GTK_FILE_CHOOSER (object);
	GtkFileFilter *filter = gtk_file_chooser_get_filter(save_dialog);
	SaveDialogFilters *filters = (SaveDialogFilters*)data;
	
	if (filter != NULL)
	{
		char *filePath = gtk_file_chooser_get_filename(save_dialog);
		if (filePath != NULL && strlen(filePath) > 0)
		{
			char *fileName = g_path_get_basename(filePath);
			if (fileName != NULL && strlen(fileName) > 0)
			{
				const char *pExtStart = get_file_extension(fileName);
				char *fileExt = g_strdup(pExtStart);
				if (pExtStart > fileName)
					fileName[pExtStart - fileName - 1] = '\0';
				
				gchar *newFileName = NULL;
				if (filter == filters->csvFiles)
				{
					if (strcmp(fileExt, "csv") != 0)					
						newFileName = g_strdup_printf("%s.csv", fileName);
				}else if (filter == filters->textFiles)
				{
					if (strcmp(fileExt, "txt") != 0)
						newFileName = g_strdup_printf("%s.txt", fileName);
				}

				if (newFileName != NULL)
					gtk_file_chooser_set_current_name(save_dialog, newFileName);
				
				g_free(newFileName);
				g_free(fileExt);
			}
			g_free(fileName);
		}
		g_free(filePath);
	}

}

static void save_data (gboolean always_ask_location)
{
	gboolean save_accepted = TRUE, save_asked = FALSE;
	
	disable_update();
	
	if (always_ask_location || Mwd.save_location==NULL)
	{
		GtkWidget *saveDialog;
		
		save_asked = TRUE;
		saveDialog = gtk_file_chooser_dialog_new(_("Save As..."), NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
												 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
												 GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);		
		gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER(saveDialog), TRUE);
		gtk_window_set_icon_from_file(GTK_WINDOW(saveDialog), GLADEDIR"netactview-icon.png", NULL);		

		SaveDialogFilters filters = {};
		filters.allFiles = gtk_file_filter_new();
		gtk_file_filter_add_pattern(filters.allFiles, "*");
		gtk_file_filter_set_name(filters.allFiles, _("All files"));
		filters.textFiles = gtk_file_filter_new();
		gtk_file_filter_add_pattern(filters.textFiles, "*.txt");
		gtk_file_filter_set_name(filters.textFiles, _("Text files (*.txt)"));
		filters.csvFiles = gtk_file_filter_new();
		gtk_file_filter_add_pattern(filters.csvFiles, "*.csv");
		gtk_file_filter_set_name(filters.csvFiles, _("CSV files (*.csv)"));
		
		g_signal_connect(saveDialog, "notify::filter", G_CALLBACK(save_dialog_filter_changed_cb), &filters);
		
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(saveDialog), filters.allFiles);
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(saveDialog), filters.textFiles);
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(saveDialog), filters.csvFiles);
		
		if (Mwd.save_location != NULL)
		{
			const char *ext = get_file_extension(Mwd.save_location);
			if (strcmp(ext, "csv") == 0)
				gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(saveDialog), filters.csvFiles);
			else if (strcmp(ext, "txt") == 0)
				gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(saveDialog), filters.textFiles);
			else
				gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(saveDialog), filters.allFiles);
				
			gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(saveDialog), Mwd.save_location);
		}
		else
		{
			gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(saveDialog), filters.textFiles);
			gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(saveDialog), _("connections.txt"));
		}
		
		save_accepted = (gtk_dialog_run (GTK_DIALOG(saveDialog)) == GTK_RESPONSE_ACCEPT);
		
		if (save_accepted)
		{
			if (Mwd.save_location != NULL)
				g_free(Mwd.save_location);
			Mwd.save_location = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (saveDialog));
		}
		
		gtk_widget_destroy (saveDialog);
	}
	
	if (save_accepted && Mwd.save_location!=NULL)
	{
		gboolean file_error = FALSE;
		int file_error_number = 0;
		
		file_error = !write_saved_data(Mwd.save_location, save_asked, &file_error_number);
		
		if (file_error)
		{
			GtkWidget *dialog;
			char *save_location_disp = g_filename_display_basename(Mwd.save_location);
			dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
											 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
											 _("Error saving file '%s'. \n%s"),
											 save_location_disp, g_strerror(file_error_number));
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			g_free(save_location_disp);
		}
	}
	
	restore_update();
}

static int selected_items_number ()
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection(Mwd.main_view);
	return gtk_tree_selection_count_selected_rows(selection);
}

static gboolean delete_event (GtkWidget *widget,
							 GdkEvent  *event,
							 gpointer   data )
{
	return FALSE;
}

static void nactv_exit_application (GtkWidget *main_window)
{
	if (!Mwd.restart_requested)
		save_preferences();
	Mwd.exit_requested = TRUE;
	gtk_main_quit();
}

static void destroy (GtkWidget *widget,
					gpointer   data )
{
	nactv_exit_application(widget);
}

static void on_menuAbout_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *aboutdialog;
	aboutdialog = glade_xml_get_widget(GladeXml, "aboutdialog");
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(aboutdialog), VERSION);
	gtk_about_dialog_set_name(GTK_ABOUT_DIALOG(aboutdialog), 
							  Q_("about.program_name|Net Activity Viewer"));
	gtk_dialog_run(GTK_DIALOG(aboutdialog));
	gtk_widget_hide(aboutdialog);
}

static void on_aboutdialog_close (GtkDialog *dialog, gpointer user_data)
{
	gtk_dialog_response(dialog, GTK_RESPONSE_OK);
}

static void on_menuWiki_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	const char *wikiURL = "http://netactview.sourceforge.net/wiki/";
	GnomeVFSResult res = gnome_vfs_url_show(wikiURL);
	if (res != GNOME_VFS_OK)
	{
		GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
		                        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
		                        _("Can't open wiki URL: \"%s\".\n"
		                          "Please check that gnome vfs and mime configurations work "
		                          "correctly with the default web browser."), 
		                        wikiURL);
		gtk_dialog_run(GTK_DIALOG (dialog));
		gtk_widget_destroy(dialog);
	}
}

static void on_tbtnSave_clicked (GtkToolButton *button, gpointer userdata)
{
	save_data(FALSE);
}

static void on_tbtnCopy_clicked (GtkToolButton *button, gpointer userdata)
{
	copy_selected_lines();
}

static void on_tbtnRefresh_clicked (GtkToolButton *button, gpointer userdata)
{
	manual_refresh_connections();
}

static void on_menuSave_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	save_data(FALSE);
}

static void on_menuSaveAs_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	save_data(TRUE);
}

static void on_menuQuit_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	GtkWidget *window = glade_xml_get_widget(GladeXml, "window");
	gtk_widget_destroy(window);
}

static void on_menuAdminMode_activate (GtkMenuItem *menuItem, gpointer userdata)
{
#ifdef HAVE_GKSU
	GtkWidget *window = glade_xml_get_widget(GladeXml, "window");
	char *execute_params[] = { GKSU_PATH, EXECUTABLE_PATH };
	int child_pid;
	save_preferences();
	
	child_pid = gnome_execute_async(NULL, sizeof(execute_params)/sizeof(char*), 
									execute_params);
	if (child_pid < 0) /*error*/
	{
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
										 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
										 _("Restart as root failed. You may need to install gksu.")
										 );
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		
	}else /*close current instance*/
	{
		Mwd.restart_requested = TRUE;
		gtk_widget_destroy(window);
	}
#endif
}

static void on_menuEdit_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	gboolean item_selected = (selected_items_number() > 0);
	
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "menuCopy"), item_selected);
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "menuCopyAddress"), item_selected);
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "menuCopyHost"), item_selected);
}

static void on_menuCopyColumn_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	if (Mwd.last_popup_column == NULL)
		return;
	int columnindex = (int)(long)g_hash_table_lookup(Mwd.column_to_index_hash, Mwd.last_popup_column);
	copy_selected_lines_column(columnindex);
}

static void on_menuCopy_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	copy_selected_lines();
}

static void on_menuCopyAddress_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	copy_selected_lines_column(MVC_REMOTEADDRESS);
}

static void on_menuCopyHost_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	copy_selected_lines_column(MVC_REMOTEHOST);
}

static char** compress_1col_str_matrix (char ***matrix, int nrows, int *nlistrows)
{
	int i, j;
	g_assert(nrows > 0);	
	char **str_list = (char**)g_malloc0(nrows * sizeof(char*));
	int str_list_rows = 0;
	for(i=0; i<nrows; i++)
	{
		char *str = matrix[i][0];
		if (str == NULL || strlen(str) == 0)
			continue;
		for(j=i+1; j<nrows; j++)
		{
			if (strcmp(str, matrix[j][0]) == 0)
				break;
		}
		if (j == nrows)
		{
			str_list[str_list_rows] = g_strdup(str);
			str_list_rows++;
		}
		
	}
	*nlistrows = str_list_rows;
	return str_list;
}

static void free_str_list(char **list, int nrows)
{
	if (list == NULL)
		return;
	int i;
	for(i=0; i<nrows; i++)
		g_free(list[i]);
	g_free(list);
}

static void AddColumnToFilter(gboolean negate)
{
	if (Mwd.last_popup_column == NULL)
		return;
	int columnindex = (int)(long)g_hash_table_lookup(Mwd.column_to_index_hash, Mwd.last_popup_column);

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuFilter")),
								   TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(glade_xml_get_widget(GladeXml, "btnOperators")),
	                             TRUE);
	GtkEntry *filter_entry;
	filter_entry = GTK_ENTRY(glade_xml_get_widget(GladeXml, "txtFilter"));

	int nrows;
	int columns[] = { columnindex };
	char ***selected_matrix = get_selected_lines_matrix(columns, 1, &nrows);
	if (selected_matrix != NULL)
	{
		int str_list_count = 0;
		char **str_list = compress_1col_str_matrix(selected_matrix, nrows, &str_list_count);

		if (str_list_count > 0)
		{		
			if (str_list_count == 1)
				Mwd.filterTree = AddFilterOperand(Mwd.filterTree, ovAND, negate, str_list[0]);
			else
			{
				int i;
				FilterOperand* operand = NULL;
				for(i=0; i<str_list_count; i++)
					operand = AddOperand(operand, ovOR, FALSE, str_list[i]);
				Mwd.filterTree = AddFilterGroup(Mwd.filterTree, ovAND, negate, operand);
			}
		}
	
		char *newFilterText = PrintFilter(Mwd.filterTree);	
		gtk_entry_set_text(filter_entry, newFilterText);
		
		g_free(newFilterText);
		free_str_list(str_list, str_list_count);
		free_string_matrix(selected_matrix, nrows, 1);
	}
}

static void on_menuFilterIn_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	AddColumnToFilter(FALSE);
}

static void on_menuFilterOut_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	AddColumnToFilter(TRUE);	
}

static void on_menuRefresh_activate (GtkMenuItem *menuItem, gpointer userdata)
{
	manual_refresh_connections();
}

static void on_menuAutoRefreshEnabled_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	GtkToggleToolButton *toggle_tool_button;
	toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(glade_xml_get_widget(GladeXml, "tbtnAutoRefresh"));
	gtk_toggle_tool_button_set_active(toggle_tool_button, checkmenuitem->active);
	set_auto_refresh(checkmenuitem->active);
}

static void on_tbtnAutoRefresh_clicked (GtkToolButton *toolbutton,
										gpointer user_data)
{
	GtkCheckMenuItem *menuItem;
	GtkToggleToolButton *toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(toolbutton);
	menuItem = GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuAutoRefreshEnabled"));
	gtk_check_menu_item_set_active(menuItem, gtk_toggle_tool_button_get_active(toggle_tool_button));
}

static void on_tbtnEstConnections_clicked (GtkToolButton *toolbutton, gpointer user_data)
{
	GtkCheckMenuItem *menuItem;
	GtkToggleToolButton *toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(toolbutton);
	menuItem = GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewUnestablishedConn"));
	gtk_check_menu_item_set_active(menuItem, !gtk_toggle_tool_button_get_active(toggle_tool_button));
}

static void on_menuAutoRefresh4_toggled (GtkCheckMenuItem *radiomenuitem, gpointer userdata)
{
	if (radiomenuitem->active)
	{
		n_strlcpy(Mwd.sel_arinterval_menu, glade_get_widget_name(GTK_WIDGET(radiomenuitem)), sizeof(Mwd.sel_arinterval_menu));
		set_auto_refresh_interval(4000);
	}
}

static void on_menuAutoRefresh1_toggled (GtkCheckMenuItem *radiomenuitem, gpointer userdata)
{
	if (radiomenuitem->active)
	{
		n_strlcpy(Mwd.sel_arinterval_menu, glade_get_widget_name(GTK_WIDGET(radiomenuitem)), sizeof(Mwd.sel_arinterval_menu));
		set_auto_refresh_interval(1000);
	}
}

static void on_menuAutoRefresh0_25_toggled (GtkCheckMenuItem *radiomenuitem, gpointer userdata)
{
	if (radiomenuitem->active)
	{
		n_strlcpy(Mwd.sel_arinterval_menu, glade_get_widget_name(GTK_WIDGET(radiomenuitem)), sizeof(Mwd.sel_arinterval_menu));
		set_auto_refresh_interval(250);
	}
}

static void on_menuAutoRefresh0_064_toggled (GtkCheckMenuItem *radiomenuitem, gpointer userdata)
{
	if (radiomenuitem->active)
	{
		n_strlcpy(Mwd.sel_arinterval_menu, glade_get_widget_name(GTK_WIDGET(radiomenuitem)), sizeof(Mwd.sel_arinterval_menu));
		set_auto_refresh_interval(64);
	}
}

static void menuView_activate (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "menuViewDeletedConn"), Mwd.view_unestablished_connections);
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "menuViewKeepDeletedConn"), Mwd.show_closed_connections);
}

static void on_menuViewHostName_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	Mwd.view_remote_host = checkmenuitem->active;
	gtk_tree_view_column_set_visible(Mwd.main_view_columns[MVC_REMOTEHOST], checkmenuitem->active);
	update_connections_hosts();
	update_connections_visibility();
}

static void on_menuViewLocalHostName_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	Mwd.view_local_host = checkmenuitem->active;
	gtk_tree_view_column_set_visible(Mwd.main_view_columns[MVC_LOCALHOST], Mwd.view_local_host);
	update_connections_hosts();
	update_connections_visibility();
}

static void on_menuViewLocalAddress_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	Mwd.view_local_address = checkmenuitem->active;
	gtk_tree_view_column_set_visible(Mwd.main_view_columns[MVC_LOCALADDRESS], Mwd.view_local_address);
	update_connections_visibility();
}

static void on_menuViewCommand_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	GtkToggleToolButton *toggle_tool_button;
	toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(glade_xml_get_widget(GladeXml, "tbtnCommands"));
	gtk_toggle_tool_button_set_active(toggle_tool_button, !checkmenuitem->active);

	Mwd.view_command = checkmenuitem->active;
	gtk_tree_view_column_set_visible(Mwd.main_view_columns[MVC_PROGRAMCOMMAND], Mwd.view_command);
	update_connections_visibility();
}

static void on_tbtnCommands_clicked (GtkToolButton *toolbutton, gpointer user_data)
{
	GtkCheckMenuItem *menuItem;
	GtkToggleToolButton *toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(toolbutton);
	menuItem = GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewCommand"));
	gtk_check_menu_item_set_active(menuItem, !gtk_toggle_tool_button_get_active(toggle_tool_button));
}

static void on_menuViewPortName_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	Mwd.view_port_names = checkmenuitem->active;
	update_ports_text();
	update_connections_visibility();
}

static void on_tbtnClrConnections_clicked (GtkToolButton *toolbutton, gpointer user_data)
{
	GtkCheckMenuItem *menuItem;
	GtkToggleToolButton *toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(toolbutton);
	menuItem = GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuViewKeepDeletedConn"));
	gtk_check_menu_item_set_active(menuItem, !gtk_toggle_tool_button_get_active(toggle_tool_button));
}

static void on_menuViewKeepDeletedConn_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
    GtkToggleToolButton *toggle_tool_button;
	toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(glade_xml_get_widget(GladeXml, "tbtnClrConnections"));
	gtk_toggle_tool_button_set_active(toggle_tool_button, !checkmenuitem->active);
    
	Mwd.keep_closed_connections = checkmenuitem->active;
}

static void on_menuViewDeletedConn_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	Mwd.show_closed_connections = checkmenuitem->active;
	if (!Mwd.show_closed_connections)
		delete_closed_connections();
}

static void on_menuViewUnestablishedConn_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	GtkToggleToolButton *toggle_tool_button;
	toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(glade_xml_get_widget(GladeXml, "tbtnEstConnections"));
	gtk_toggle_tool_button_set_active(toggle_tool_button, !checkmenuitem->active);
	
	int i;
	Mwd.view_unestablished_connections = checkmenuitem->active;	
	for (i=0; i<Mwd.connections->len; i++)
		list_update_connection(g_array_index(Mwd.connections, NetConnection*, i));
	refresh_visible_conn_label();
}

static void on_menuViewColors_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	Mwd.view_colors = checkmenuitem->active;
	if (!Mwd.view_colors)
		clear_colors();
}

static void set_menuitem_label(GtkMenuItem* mitem, const char* label_text)
{
#if (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 16)
		if (GTK_BIN(mitem)->child != NULL && GTK_IS_LABEL(GTK_BIN(mitem)->child))
		{
			gtk_label_set_label(GTK_LABEL(GTK_BIN(mitem)->child), (label_text!=NULL) ? label_text : "");
		}
#else
		gtk_menu_item_set_label(mitem, label_text);
#endif
}

static void show_popup_menu (int button, gboolean selecting, GtkTreeViewColumn *popup_column)
{
	Mwd.last_popup_column = popup_column;
	gboolean item_selected = ( selecting || (selected_items_number() > 0) );

	GtkMenuItem *copyColumnMenu = GTK_MENU_ITEM(glade_xml_get_widget(GladeXml, "popupCopyColumn"));
	if (popup_column != NULL)
	{
		const char * column_title = gtk_tree_view_column_get_title(popup_column);
		char *menu_text = g_strdup_printf(_("Copy by '%s'"), column_title);
		set_menuitem_label(copyColumnMenu, menu_text);
		g_free(menu_text);
	}else
		set_menuitem_label(copyColumnMenu, _("Copy by 'Column'"));
	
	gtk_widget_set_sensitive(GTK_WIDGET(copyColumnMenu), item_selected && (popup_column!=NULL));
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "popupCopyLine"), item_selected);
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "popupCopyRemoteAddress"), item_selected);
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "popupCopyRemoteHost"), item_selected);
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "popupFilterIn"), item_selected && (popup_column!=NULL));
	gtk_widget_set_sensitive(glade_xml_get_widget(GladeXml, "popupFilterOut"), item_selected && (popup_column!=NULL));
	
	gtk_menu_popup(Mwd.mainPopup, NULL, NULL, NULL, NULL, button, 
				   gtk_get_current_event_time());	
}

static gboolean on_mainView_popup_menu (GtkWidget *widget, gpointer user_data)
{
	show_popup_menu(0, FALSE, NULL);
	return TRUE;
}

static gboolean on_mainView_button_press_event (GtkWidget *widget, GdkEventButton *event, 
										 gpointer user_data)
{
	gboolean item_already_selected = FALSE, selecting = FALSE;
	if (event->button == 3)
	{
		GtkTreePath *position_path;
		GtkTreeViewColumn *popup_column = NULL;
		
		if (gtk_tree_view_get_path_at_pos(Mwd.main_view, event->x, event->y,  &position_path,
										  &popup_column, NULL, NULL))
		{
			GtkTreeSelection *selection = gtk_tree_view_get_selection(Mwd.main_view);
			if (gtk_tree_selection_path_is_selected(selection, position_path))
				item_already_selected = TRUE;
			else
				selecting = TRUE;
			gtk_tree_path_free(position_path);
		}else
			popup_column = NULL;
		
		show_popup_menu(event->button, selecting, popup_column);
	}
	return item_already_selected; /*stop event if TRUE*/
}

static void set_sort_column (int columnindex, gboolean init)
{
	GtkTreeViewColumn *column = Mwd.main_view_columns[columnindex];
	int sortdirection;
	
	if (!init)
	{
		if (Mwd.current_sort_column == columnindex)
		{
			sortdirection = (Mwd.current_sort_direction == GTK_SORT_DESCENDING) ? 
				GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
		}else
		{
			sortdirection = Mwd.current_sort_direction;
			if (Mwd.current_sort_column >= 0)
			{
				GtkTreeViewColumn *currentscolumn = Mwd.main_view_columns[Mwd.current_sort_column];
				gtk_tree_view_column_set_sort_indicator(currentscolumn, FALSE);
			}
		}
	}else
		sortdirection = Mwd.current_sort_direction;
	
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(Mwd.main_store), columnindex, 
										 sortdirection);
	
	gtk_tree_view_column_set_sort_indicator(column, TRUE);
	gtk_tree_view_column_set_sort_order(column, sortdirection);
	
	Mwd.current_sort_column = columnindex;
	Mwd.current_sort_direction = sortdirection;
}


static gint tree_sort_compare (GtkTreeModel *model, 
							   GtkTreeIter *a, GtkTreeIter *b, 
							   gpointer user_data)
{
	ColumnData *column_data = (ColumnData*)user_data;
	GValue v_a = {0,}, v_b = {0,};
	const char *s_a, *s_b;
	gint sort_result = 0;
	
	gtk_tree_model_get_value(model, a, column_data->index, &v_a);
	gtk_tree_model_get_value(model, b, column_data->index, &v_b);
	s_a = g_value_get_string(&v_a);
	s_b = g_value_get_string(&v_b);
	
	switch(column_data->datatype)
	{
		case MVC_TYPE_STRING:
			sort_result = strcmp(s_a, s_b);
			break;
		case MVC_TYPE_INT:
		{
			int n_a = atoi(s_a), n_b = atoi(s_b);
			sort_result = (n_a > n_b) ? 1 : ((n_a < n_b) ? -1 : 0);
		}
		break;
		case MVC_TYPE_IP_ADDRESS:
			sort_result = compare_addresses(s_a, s_b);
			break;
		case MVC_TYPE_HOST:
			sort_result = compare_hosts(s_a, s_b);
			break;
		default:
			sort_result = 0;
			break;
	}
	
	g_value_unset(&v_a);
	g_value_unset(&v_b);
	
	return sort_result;
}

static void on_tree_column_clicked (GtkTreeViewColumn *treeviewcolumn, gpointer user_data)
{
	int columnindex = (int)(long)user_data;
	set_sort_column(columnindex, FALSE);
}

static void on_btnCloseFilter_clicked (GtkButton *button)
{
	GtkCheckMenuItem *menuFilter;
	menuFilter = GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuFilter"));
	
	gtk_check_menu_item_set_active(menuFilter, FALSE);
}

static void on_btnClearFilter_clicked (GtkButton *button)
{
	clear_filter();
}

static void on_btnCaseSensitive_toggled (GtkToggleButton *button, gpointer user_data)
{
	Mwd.caseSensitiveFilter = gtk_toggle_button_get_active(button);
	update_filter();
}

static void on_btnOperators_toggled (GtkToggleButton *button, gpointer user_data)
{
	Mwd.filterOperators = gtk_toggle_button_get_active(button);
	update_filter();
}

static void on_filter_changed (GtkEditable *editable, gpointer user_data)
{
	static gboolean inside_filter_changed = FALSE;
	if (inside_filter_changed)
		return;
	inside_filter_changed = TRUE;
	
	GtkEntry *filter_entry;
	filter_entry = GTK_ENTRY(glade_xml_get_widget(GladeXml, "txtFilter"));	
	g_string_assign(Mwd.filter, gtk_entry_get_text(filter_entry));	

	if (strchr(Mwd.filter->str, '\n') != NULL)
	{
		char *newStr = NULL;
		if (Mwd.filterOperators)
			newStr = string_replace(Mwd.filter->str, "\n", " OR ");
		else
			newStr = string_replace(Mwd.filter->str, "\n", " ");
		g_string_assign(Mwd.filter, newStr);
		gtk_entry_set_text(filter_entry, Mwd.filter->str);
		g_free(newStr);
	}
	
	update_filter();

	inside_filter_changed = FALSE;
}

static void on_menuFilter_toggled (GtkCheckMenuItem *checkmenuitem, gpointer userdata)
{
	GtkWidget *filterHBox;
	filterHBox = GTK_WIDGET(glade_xml_get_widget (GladeXml, "hboxFilter"));
	
	Mwd.filtering = checkmenuitem->active;
	
	if (checkmenuitem->active)
	{
		GtkWidget *filter_entry;
		filter_entry = glade_xml_get_widget(GladeXml, "txtFilter");
		
		gtk_widget_show(filterHBox);
		gtk_widget_grab_focus(filter_entry);
	}else
	{
		clear_filter();
		gtk_widget_hide(filterHBox);		
	}

}

gboolean on_window_configure_event (GtkWidget *widget, GdkEventConfigure *event, gpointer user_data)
{
	Mwd.window_width = event->width;
	Mwd.window_height = event->height;
	return FALSE;
}

gboolean on_window_window_state_event (GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{	
	Mwd.window_maximized = ((event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0);
	return FALSE;
}

static void gconf_load ()
{
	GConfClient *conf = gconf_client_get_default();
	
	Mwd.default_fixed_font = gconf_client_get_string (conf, 
								"/desktop/gnome/interface/monospace_font_name", NULL);
	if (Mwd.default_fixed_font == NULL)
		Mwd.default_fixed_font = g_strdup("Monospace 10");
	
	g_object_unref(conf);
	
}


static void connect_signals (GtkWidget *window)
{
	glade_xml_signal_connect(GladeXml, "on_menuAbout_activate", G_CALLBACK(&on_menuAbout_activate));
	glade_xml_signal_connect(GladeXml, "on_menuWiki_activate", G_CALLBACK(&on_menuWiki_activate));
	glade_xml_signal_connect(GladeXml, "on_aboutdialog_close", G_CALLBACK(&on_aboutdialog_close));
	glade_xml_signal_connect(GladeXml, "on_tbtnSave_clicked", G_CALLBACK(&on_tbtnSave_clicked));
	glade_xml_signal_connect(GladeXml, "on_tbtnCopy_clicked", G_CALLBACK(&on_tbtnCopy_clicked));
	glade_xml_signal_connect(GladeXml, "on_tbtnRefresh_clicked", G_CALLBACK(&on_tbtnRefresh_clicked));
	glade_xml_signal_connect(GladeXml, "on_tbtnAutoRefresh_clicked", G_CALLBACK(&on_tbtnAutoRefresh_clicked));
	glade_xml_signal_connect(GladeXml, "on_tbtnEstConnections_clicked", G_CALLBACK(&on_tbtnEstConnections_clicked));
	glade_xml_signal_connect(GladeXml, "on_tbtnCommands_clicked", G_CALLBACK(&on_tbtnCommands_clicked));
	glade_xml_signal_connect(GladeXml, "on_tbtnClrConnections_clicked", G_CALLBACK(&on_tbtnClrConnections_clicked));
	glade_xml_signal_connect(GladeXml, "on_menuSave_activate", G_CALLBACK(&on_menuSave_activate));
	glade_xml_signal_connect(GladeXml, "on_menuSaveAs_activate", G_CALLBACK(&on_menuSaveAs_activate));
	glade_xml_signal_connect(GladeXml, "on_menuAdminMode_activate", G_CALLBACK(&on_menuAdminMode_activate));
	glade_xml_signal_connect(GladeXml, "on_menuQuit_activate", G_CALLBACK(&on_menuQuit_activate));
	glade_xml_signal_connect(GladeXml, "on_menuCopyColumn_activate", G_CALLBACK(&on_menuCopyColumn_activate));
	glade_xml_signal_connect(GladeXml, "on_menuCopy_activate", G_CALLBACK(&on_menuCopy_activate));
	glade_xml_signal_connect(GladeXml, "on_menuCopyAddress_activate", G_CALLBACK(&on_menuCopyAddress_activate));
	glade_xml_signal_connect(GladeXml, "on_menuCopyHost_activate", G_CALLBACK(&on_menuCopyHost_activate));
	glade_xml_signal_connect(GladeXml, "on_menuFilterIn_activate", G_CALLBACK(&on_menuFilterIn_activate));
	glade_xml_signal_connect(GladeXml, "on_menuFilterOut_activate", G_CALLBACK(&on_menuFilterOut_activate));
	glade_xml_signal_connect(GladeXml, "on_menuRefresh_activate", G_CALLBACK(&on_menuRefresh_activate));
	glade_xml_signal_connect(GladeXml, "on_menuAutoRefreshEnabled_toggled", G_CALLBACK(&on_menuAutoRefreshEnabled_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuAutoRefresh4_toggled", G_CALLBACK(&on_menuAutoRefresh4_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuAutoRefresh1_toggled", G_CALLBACK(&on_menuAutoRefresh1_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuAutoRefresh0_25_toggled", G_CALLBACK(&on_menuAutoRefresh0_25_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuAutoRefresh0_064_toggled", G_CALLBACK(&on_menuAutoRefresh0_064_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewLocalAddress_toggled", G_CALLBACK(&on_menuViewLocalAddress_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewHostName_toggled", G_CALLBACK(&on_menuViewHostName_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewLocalHostName_toggled", G_CALLBACK(&on_menuViewLocalHostName_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewCommand_toggled", G_CALLBACK(&on_menuViewCommand_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewPortName_toggled", G_CALLBACK(&on_menuViewPortName_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewKeepDeletedConn_toggled", G_CALLBACK(&on_menuViewKeepDeletedConn_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewDeletedConn_toggled", G_CALLBACK(&on_menuViewDeletedConn_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewUnestablishedConn_toggled", G_CALLBACK(&on_menuViewUnestablishedConn_toggled));
	glade_xml_signal_connect(GladeXml, "on_menuViewColors_toggled", G_CALLBACK(&on_menuViewColors_toggled));
	glade_xml_signal_connect(GladeXml, "on_mainView_popup_menu", G_CALLBACK(&on_mainView_popup_menu));
	glade_xml_signal_connect(GladeXml, "on_mainView_button_press_event", G_CALLBACK(&on_mainView_button_press_event));
	glade_xml_signal_connect(GladeXml, "menuView_activate", G_CALLBACK(&menuView_activate));
	glade_xml_signal_connect(GladeXml, "on_menuEdit_activate", G_CALLBACK(&on_menuEdit_activate));
	glade_xml_signal_connect(GladeXml, "on_btnCloseFilter_clicked", G_CALLBACK(&on_btnCloseFilter_clicked));
	glade_xml_signal_connect(GladeXml, "on_btnClearFilter_clicked", G_CALLBACK(&on_btnClearFilter_clicked));
	glade_xml_signal_connect(GladeXml, "on_btnCaseSensitive_toggled", G_CALLBACK(&on_btnCaseSensitive_toggled));
	glade_xml_signal_connect(GladeXml, "on_btnOperators_toggled", G_CALLBACK(&on_btnOperators_toggled));
	glade_xml_signal_connect(GladeXml, "on_filter_changed", G_CALLBACK(&on_filter_changed));
	glade_xml_signal_connect(GladeXml, "on_menuFilter_toggled", G_CALLBACK(&on_menuFilter_toggled));
	glade_xml_signal_connect(GladeXml, "on_window_configure_event", G_CALLBACK(&on_window_configure_event));
	glade_xml_signal_connect(GladeXml, "on_window_window_state_event", G_CALLBACK(&on_window_window_state_event));
	
	g_signal_connect(G_OBJECT (window), "delete_event", G_CALLBACK (delete_event), NULL);
	g_signal_connect(G_OBJECT (window), "destroy", G_CALLBACK (destroy), NULL);
}


static GtkTreeViewColumn *add_view_column (GtkTreeView *view, const ColumnData *column_data)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	
	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "xalign", column_data->align, NULL);
	
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _(column_data->title));
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	
	gtk_tree_view_column_set_attributes(column, renderer,
										"text", column_data->index,
										"background", MVC_COLOR,
										NULL);
	
	gtk_tree_view_column_set_clickable(column, TRUE);
	gtk_tree_view_column_set_reorderable(column, TRUE);
	gtk_tree_view_column_set_resizable(column, TRUE);
	
	g_signal_connect(G_OBJECT(column), "clicked", G_CALLBACK(on_tree_column_clicked), 
					 (gpointer)column_data->index);
	
	gtk_tree_view_append_column(view, column);
	
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(Mwd.main_store), column_data->index, 
									&tree_sort_compare, (gpointer)column_data, NULL);
	
	gtk_tree_view_column_set_visible(column, column_data->visible);
	
	return column;
}

static void setup_view (GtkWidget *window)
{
	GtkTreeSelection *main_view_selection;
	int ipos;
	
	/* Take into account the distinction between column index (or id) and column position.
	 * The column index is the index in the data store. The position is the graphical position.
	 * gtk_tree_view_get_column returns the column position. Most functions use the column index or the column object.
	 */
	Mwd.main_view = GTK_TREE_VIEW(glade_xml_get_widget (GladeXml, "mainView"));
	Mwd.main_store = gtk_list_store_new(MVC_COLUMNSNUMBER, G_TYPE_STRING, G_TYPE_STRING, 
					G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, 
					G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, 
					G_TYPE_BOOLEAN, G_TYPE_POINTER, G_TYPE_STRING);
	Mwd.main_store_filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(Mwd.main_store), NULL);
	gtk_tree_view_set_model(Mwd.main_view, Mwd.main_store_filtered);
	
	Mwd.column_to_index_hash = g_hash_table_new(NULL, NULL);
	
	/*Initial position set by add order*/
	for (ipos=0; ipos<MVC_VIEW_COLUMNSNUMBER; ipos++)
	{
		int columnindex = Mwd.columns_initial_view_order[ipos];
		g_assert(columnindex >= 0 && columnindex < MVC_VIEW_COLUMNSNUMBER);
		g_assert(Mwd.main_view_columns[columnindex] == NULL);
		GtkTreeViewColumn *column = add_view_column(Mwd.main_view, main_view_column_data+columnindex);
		Mwd.main_view_columns[columnindex] = column;
		g_hash_table_insert(Mwd.column_to_index_hash, column, (gpointer)(long)columnindex);
	}
	
	main_view_selection = gtk_tree_view_get_selection(Mwd.main_view);
	gtk_tree_selection_set_mode(main_view_selection, GTK_SELECTION_MULTIPLE);
	
	set_sort_column(Mwd.current_sort_column, TRUE);
	
	gtk_tree_model_filter_set_visible_column(GTK_TREE_MODEL_FILTER(Mwd.main_store_filtered), MVC_VISIBLE);
	
	gtk_tree_view_unset_rows_drag_dest(Mwd.main_view);
	gtk_tree_view_unset_rows_drag_source(Mwd.main_view);

	if (Mwd.initial_window_width > 0 && Mwd.initial_window_height > 0)
		gtk_window_set_default_size(GTK_WINDOW(window), Mwd.initial_window_width, Mwd.initial_window_height);
	if (Mwd.window_maximized)
		gtk_window_maximize(GTK_WINDOW(window));
}

static GtkLabel *add_status_bar_label (const char *text)
{
	GtkWidget* status_bar = glade_xml_get_widget(GladeXml, "mainstatusbar");
	GtkFrame* label_frame;
	GtkLabel* label;
	PangoFontDescription *font_desc;
	
	label = GTK_LABEL(gtk_label_new(text));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	
	font_desc = pango_font_description_from_string(Mwd.default_fixed_font);
	if (font_desc != NULL)
	{
		gtk_widget_modify_font(GTK_WIDGET(label), font_desc);
		pango_font_description_free(font_desc);
	}
	
	label_frame = GTK_FRAME(gtk_frame_new(NULL));
	gtk_frame_set_shadow_type(label_frame, GTK_SHADOW_IN);
	
	gtk_container_add (GTK_CONTAINER(label_frame), GTK_WIDGET(label));
	gtk_box_pack_start(GTK_BOX(status_bar), GTK_WIDGET(label_frame), FALSE, FALSE, 3);
	gtk_box_reorder_child(GTK_BOX(status_bar), GTK_WIDGET(label_frame), 0);
	
	gtk_widget_show(GTK_WIDGET(label_frame));
	gtk_widget_show(GTK_WIDGET(label));
	
	return label;
}

static void init_controls ()
{
	Mwd.label_visible = GTK_LABEL(glade_xml_get_widget(GladeXml, "lblVisibleConn"));
}

static void setup_status_bar ()
{
	Mwd.label_received = add_status_bar_label("Received: 0 B +0 B/s      ");
	Mwd.label_sent = add_status_bar_label("Sent: 0 B +0 B/s      ");
	Mwd.label_count = add_status_bar_label("Established: 0/0  ");
}

GtkWidget* main_window_create (void)
{
	GtkWidget *window;
	
	set_main_window_data_defaults(&Mwd);
	
	window = glade_xml_get_widget(GladeXml, "window");
	g_assert(window != NULL);
	Mwd.mainPopup = GTK_MENU(glade_xml_get_widget(GladeXml, "mainPopup"));
	
	gtk_window_set_title(GTK_WINDOW(window), Q_("main_window.title|Net Activity Viewer"));
	g_object_set(window, "allow-shrink", TRUE, NULL);

	Mwd.connections = g_array_sized_new(FALSE, TRUE, sizeof(NetConnection*), 16);
	
	load_preferences();
	gconf_load();

	init_controls();
	setup_status_bar();
	setup_view(window);
	init_connections_loader();
	init_host_loader();
	connect_signals(window);
	
	refresh_connections();
	update_auto_refresh();
	
	set_menu_preferences();

	Mwd.main_view_created = TRUE;
	
	return window;
}

void toggled_AutoRefreshEnabled (int state) {
    GtkToggleToolButton *toggle_tool_button;
	toggle_tool_button = GTK_TOGGLE_TOOL_BUTTON(glade_xml_get_widget(GladeXml, "tbtnAutoRefresh"));
    gtk_toggle_tool_button_set_active(toggle_tool_button, state);

	set_auto_refresh(state);

	//GtkCheckMenuItem *menuItem;
	//menuItem = GTK_CHECK_MENU_ITEM(glade_xml_get_widget(GladeXml, "menuAutoRefreshEnabled"));
	//gtk_check_menu_item_set_active(menuItem, state);    
}

void main_window_data_cleanup ()
{
	int i;
	
	Mwd.exit_requested = TRUE;
	if (!Mwd.main_view_created)
		return;
	
	stop_connections_loader();
	stop_host_loader();
	free_connections_loader();
	free_host_loader();
	
	if (Mwd.save_location != NULL)
	{
		g_free(Mwd.save_location);
		Mwd.save_location = NULL;
	}
	
	for (i=0; i<Mwd.connections->len; i++)
		list_free_net_connection(g_array_index(Mwd.connections, NetConnection*, i));
	g_array_free(Mwd.connections, TRUE);
	Mwd.connections = NULL;
	
	if (Mwd.statistics_timer != NULL)
		g_timer_destroy(Mwd.statistics_timer);
	
	g_hash_table_destroy(Mwd.column_to_index_hash);

	FreeFilterData();
	g_string_free(Mwd.filter, TRUE);
	g_free(Mwd.default_fixed_font);
}


static void disable_update ()
{
	Mwd.update_disabled = TRUE;
}

static void restore_update ()
{
	Mwd.update_disabled = FALSE;
}
