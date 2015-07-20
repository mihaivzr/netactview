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
 
#ifndef NACTV_NET_H
#define NACTV_NET_H

#include <glib.h>

/*Protocols*/
enum {
	NC_PROTOCOL_TCP,
	NC_PROTOCOL_UDP,
	NC_PROTOCOL_TCP6,
	NC_PROTOCOL_UDP6,
	NC_PROTOCOLS_NUMBER
};

/*Tcp States*/
/*Use the same states for UDP - see net_connection_get_state_name*/
enum {
	NC_TCP_EMPTY,
    NC_TCP_ESTABLISHED,
    NC_TCP_SYN_SENT,
    NC_TCP_SYN_RECV,
    NC_TCP_FIN_WAIT1,
    NC_TCP_FIN_WAIT2,
    NC_TCP_TIME_WAIT,
    NC_TCP_CLOSE,
    NC_TCP_CLOSE_WAIT,
    NC_TCP_LAST_ACK,
    NC_TCP_LISTEN,
    NC_TCP_CLOSING,
	NC_TCP_CLOSED,
	NC_TCP_STATES_NUMBER
};

/*Connection items operations*/
enum {
	NC_OP_NONE,
	NC_OP_INSERT,
	NC_OP_UPDATE,
	NC_OP_DELETE
};


typedef struct
{
	int protocol;
	char *localhost;
	char *localaddress;
	int  localport;
	char *remotehost;
	char *remoteaddress;
	int  remoteport;
	int state;
	long pid; /*current program pid*/
	long  programpid; /*It does not change to 0*/
	char *programname;
	char *programcommand;
	unsigned long inode;
	int operation;
	void *user_data;
} NetConnection;


typedef struct
{
	unsigned long long bytes_sent;
	unsigned long long packets_sent;
	unsigned long long bytes_received;
	unsigned long long packets_received;
} NetStatistics;


/*Call this only once at application startup. It is not thread safe.*/
void nactv_net_init ();
/*Call this on application end.*/
void nactv_net_free ();

NetConnection *net_connection_new ();
void net_connection_delete (NetConnection *line);
void net_connection_delete_contents (NetConnection *line);
void net_connection_copy (NetConnection *destination, NetConnection *source);

const char *net_connection_get_protocol_name (NetConnection *conn);
const char *net_connection_get_state_name (NetConnection *conn);
	
int net_connection_net_equals_exact (NetConnection *nc1, NetConnection *nc2);
int net_connection_net_equals_fuzzy (NetConnection *nc1, NetConnection *nc2);
int net_connection_info_equals (NetConnection *nc1, NetConnection *nc2);

void net_connection_update (NetConnection *old_conn, NetConnection *new_conn);

/* Updates a NetConnection* list by adding the latest connections and setting the 
 * INSERT, UPDATE, DELETE operations*/
void net_connection_update_list_full (GArray *connections, NetConnection *latest, 
									  unsigned int nlatest);

unsigned int get_net_connections (NetConnection **connections);
void free_net_connections (NetConnection *connections, unsigned int nconnections);
void free_net_connections_array (GArray *connections);

void net_statistics_get (NetStatistics *net_stats);

char *get_port_text (int port);
char *get_port_name (int protocol, int port);
char *get_full_port_text(int protocol, int port);
char *get_host_name_by_address (const char* address);
int compare_addresses(const char *addr1, const char *addr2);
int compare_hosts(const char *addr1, const char *addr2);

#endif /*NACTV_NET_H*/

