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
#include "process.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <glib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <glibtop/netload.h>
#include <glibtop/netlist.h>


static const char *protocol_name[NC_PROTOCOLS_NUMBER] =
{
	"tcp",
	"udp",
	"tcp6",
	"udp6"
};

static const char *protocol_file[NC_PROTOCOLS_NUMBER] =
{
	"/proc/net/tcp",
	"/proc/net/udp",
	"/proc/net/tcp6",
	"/proc/net/udp6"
};

static const char *tcp_state_name[NC_TCP_STATES_NUMBER] =
{
    "",
    "ESTABLISHED",
    "SYN_SENT",
    "SYN_RECV",
    "FIN_WAIT1",
    "FIN_WAIT2",
    "TIME_WAIT",
    "CLOSE",
    "CLOSE_WAIT",
    "LAST_ACK",
    "LISTEN",
    "CLOSING",
	"CLOSED"
};


static GHashTable *services_hash = NULL;

void nactv_net_init ()
{
	/*Load the services database*/
	struct servent *sentry;
	char key_port[128];
	
	g_assert(services_hash == NULL);
	services_hash = g_hash_table_new_full(&g_str_hash, &g_str_equal, &g_free, &g_free);
	
	setservent(0);
	
	while ( (sentry = getservent()) != NULL )
	{
		int prlen = g_snprintf(key_port, sizeof(key_port), "%d/%s", 
		                       ntohs(sentry->s_port), sentry->s_proto);
		if (prlen > 0 && prlen < (int)sizeof(key_port))
			g_hash_table_insert(services_hash, g_strdup(key_port), g_strdup(sentry->s_name));
	}
	
	endservent();
}

void nactv_net_free ()
{	
	if (services_hash != NULL)
	{
		g_hash_table_destroy(services_hash);
		services_hash = NULL;
	}
}

static const char *service_protocol_name[NC_PROTOCOLS_NUMBER] = {
	"tcp", "udp", "tcp", "udp"
};

static const char *net_service_get(int protocol, int port)
{
	char *service_name = NULL;
	if (services_hash != NULL)
	{
		char key_port[128];
		int prlen;
		g_assert(protocol>=0 && protocol<NC_PROTOCOLS_NUMBER);
		
		prlen = g_snprintf(key_port, sizeof(key_port), "%d/%s", 
		                   port, service_protocol_name[protocol]);
		if (prlen > 0 && prlen < (int)sizeof(key_port))
			service_name = (char*)g_hash_table_lookup(services_hash, key_port);
	}
	return service_name;
}


static GHashTable *get_open_sockets_for_processes (Process **processes, unsigned int *nprocesses)
{
	GHashTable *open_sockets_hash;
	unsigned int i;
	
	open_sockets_hash = g_hash_table_new(NULL, NULL);
	
	*nprocesses = get_running_processes(processes);
	for (i=0; i<*nprocesses; i++)
	{
		unsigned long *sockets = NULL;
		unsigned int nsockets, j;
		Process *process = *processes+i;
		
		nsockets = process_get_socket_inodes(process->pid, &sockets);
		if (nsockets > 0)
		{
			update_process_info(process);
			for (j=0; j<nsockets; j++)
				g_hash_table_insert(open_sockets_hash, (gpointer)sockets[j], process);
			
			g_free(sockets);
		}
	}
	return open_sockets_hash;
}


#define IN6_ADDR_IS_ZERO(addr) ((addr).s6_addr32[0]==0 && (addr).s6_addr32[1]==0 && \
	(addr).s6_addr32[2]==0 && (addr).s6_addr32[3]==0)


static void get_connections_from_kernel(int protocol, GArray *connections, GHashTable *open_sockets_hash)
{
	char buffer[8192];
	FILE *f;
	g_assert(protocol>=0 && protocol<NC_PROTOCOLS_NUMBER);
	
	f = fopen(protocol_file[protocol], "r");
	if (f != NULL)
	{
		fgets(buffer, sizeof(buffer), f); /*skip the first line*/
		while (fgets(buffer, sizeof(buffer), f) != NULL)
		{
			NetConnection net_line = {};
			Process *process = NULL;
			unsigned long rxq = 0, txq = 0, time_len = 0, retr = 0;
			unsigned long inode = 0;
			int num = 0, local_port = 0, rem_port = 0, d = -1, state = -1, uid = 0, timer_run = 0, timeout = 0;
			char rem_addr[136] = "", local_addr[136] = "", more[1032]="";
			
			memset(&net_line, 0, sizeof(net_line));
			
			state = -1;
			d = -1;
			num = sscanf(buffer,
				"%d: %64[0-9A-Fa-f]:%X %64[0-9A-Fa-f]:%X %X %lX:%lX %X:%lX %lX %d %d %lu %512s\n",
				&d, local_addr, &local_port, rem_addr, &rem_port, &state,
				&txq, &rxq, &timer_run, &time_len, &retr, &uid, &timeout, &inode, more);
				
			if (num < 10 || d < 0)
			{
				nactv_trace("Invalid connection line format for protocol %d\n", protocol);
				continue;
			}
			
			if (strlen(local_addr) > 8) /*IP v6*/
			{
				struct in6_addr nlocaladdr = {}, nremaddr = {};
				
				sscanf(local_addr, "%08X%08X%08X%08X",
					   &nlocaladdr.s6_addr32[0], &nlocaladdr.s6_addr32[1],
					   &nlocaladdr.s6_addr32[2], &nlocaladdr.s6_addr32[3]);
				sscanf(rem_addr, "%08X%08X%08X%08X",
					   &nremaddr.s6_addr32[0], &nremaddr.s6_addr32[1],
					   &nremaddr.s6_addr32[2], &nremaddr.s6_addr32[3]);
				
				if (!IN6_ADDR_IS_ZERO(nlocaladdr))
					inet_ntop(AF_INET6, &nlocaladdr, local_addr, sizeof(local_addr));
				else
					strcpy(local_addr, "*");
				if (!IN6_ADDR_IS_ZERO(nremaddr))
					inet_ntop(AF_INET6, &nremaddr, rem_addr, sizeof(rem_addr));
				else
					strcpy(rem_addr, "*");
			}else /*IP v4*/
			{
				struct in_addr nlocaladdr = {}, nremaddr = {};
				
				sscanf(local_addr, "%X", &(nlocaladdr.s_addr));
				sscanf(rem_addr, "%X", &(nremaddr.s_addr));
				if (nlocaladdr.s_addr != 0)
					inet_ntop(AF_INET, &nlocaladdr, local_addr, sizeof(local_addr));
				else
					strcpy(local_addr, "*");
				if (nremaddr.s_addr != 0)
					inet_ntop(AF_INET, &nremaddr, rem_addr, sizeof(rem_addr));
				else
					strcpy(rem_addr, "*");
			}
			
			if (state < 0 || state > NC_TCP_CLOSING)
			{
				nactv_trace("Unknown connection state %d\n", state);
				state = NC_TCP_EMPTY;
			}
			
			net_line.inode = inode;
			net_line.protocol = protocol;
			net_line.localaddress = g_strdup(local_addr);
			net_line.remoteaddress = g_strdup(rem_addr);
			net_line.localport = local_port;
			net_line.remoteport = rem_port;
			net_line.state = state;
			
			if (inode > 0)
			{
				process = (Process*)g_hash_table_lookup(open_sockets_hash, (gpointer)inode);
				if (process != NULL)
				{
					net_line.pid = process->pid;
					net_line.programpid = process->pid;
					net_line.programname = (process->name!=NULL) ? g_strdup(process->name) : NULL;
					net_line.programcommand = (process->commandline!=NULL) ? 
						g_strdup(process->commandline) : NULL;
				}
			}
			
			g_array_append_val(connections, net_line);
		}
		fclose(f);	
	}
}


NetConnection *net_connection_new()
{
	NetConnection *line = (NetConnection*)g_malloc0(sizeof(NetConnection));
	return line;
}

void net_connection_delete_contents(NetConnection *line)
{
	if (line != NULL)
	{
		if (line->localhost != NULL)
			g_free(line->localhost);
		if (line->localaddress != NULL)
			g_free(line->localaddress);
		if (line->remotehost != NULL)
			g_free(line->remotehost);
		if (line->remoteaddress != NULL)
			g_free(line->remoteaddress);
		if (line->programname != NULL)
			g_free(line->programname);
		if (line->programcommand != NULL)
			g_free(line->programcommand);
	}
}


void net_connection_delete(NetConnection *line)
{
	if (line != NULL)
	{
		net_connection_delete_contents(line);	
		g_free(line);
	}
}

void net_connection_copy(NetConnection *destination, NetConnection *source)
{
	destination->protocol = source->protocol;
	destination->localhost = (source->localhost!=NULL) ? g_strdup(source->localhost) : NULL;
	destination->localaddress = (source->localaddress!=NULL) ? g_strdup(source->localaddress) : NULL;
	destination->localport = source->localport;
	destination->remotehost = (source->remotehost!=NULL) ? g_strdup(source->remotehost) : NULL;
	destination->remoteaddress = (source->remoteaddress!=NULL) ? g_strdup(source->remoteaddress) : NULL;
	destination->remoteport = source->remoteport;
	destination->state = source->state;
	destination->pid = source->pid;
	destination->programpid = source->programpid;
	destination->programname = (source->programname!=NULL) ? g_strdup(source->programname) : NULL;
	destination->programcommand = (source->programcommand!=NULL) ? g_strdup(source->programcommand) : NULL;
	destination->inode = source->inode;
	destination->operation = source->operation;
	destination->user_data = source->user_data;
}

const char *net_connection_get_protocol_name (NetConnection *conn)
{
	g_assert(conn->protocol >=0 && conn->protocol < NC_PROTOCOLS_NUMBER);
	return protocol_name[conn->protocol];
}

const char *net_connection_get_state_name (NetConnection *conn)
{
	if (conn->protocol == NC_PROTOCOL_TCP || conn->protocol == NC_PROTOCOL_TCP6)
	{
		if (conn->state>=0 && conn->state<NC_TCP_STATES_NUMBER)
			return tcp_state_name[conn->state];
		else
			return "";
	}else /*UDP*/
	{
		switch (conn->state) 
		{
		case NC_TCP_ESTABLISHED:
		case NC_TCP_CLOSED:
			return tcp_state_name[conn->state];
			break;
		default: /* 7 (TCP_CLOSE value) is a kind of LISTEN, but netstat shows an empty string for that too */
			return "";
		}		
	}		
}


unsigned int get_net_connections(NetConnection **connections)
{
	unsigned int nr_connections = 0, nr_processes = 0;
	GArray *aconnections;
	GHashTable *open_sockets_hash = NULL;
	Process *processes = NULL;
	g_assert(*connections == NULL);
	*connections = NULL;
	
	open_sockets_hash = get_open_sockets_for_processes(&processes, &nr_processes);
	
	aconnections = g_array_sized_new(FALSE, TRUE, sizeof(NetConnection), 16);
	get_connections_from_kernel(NC_PROTOCOL_TCP, aconnections, open_sockets_hash);
	get_connections_from_kernel(NC_PROTOCOL_TCP6, aconnections, open_sockets_hash);
	get_connections_from_kernel(NC_PROTOCOL_UDP, aconnections, open_sockets_hash);
	get_connections_from_kernel(NC_PROTOCOL_UDP6, aconnections, open_sockets_hash);

	nr_connections = aconnections->len;
	*connections = (nr_connections > 0) ? (NetConnection*)aconnections->data : NULL;
	g_array_free(aconnections, *connections==NULL);
	
	free_processes (processes, nr_processes);
	g_hash_table_destroy(open_sockets_hash);
	
	return nr_connections;	
}

void free_net_connections(NetConnection *connections, unsigned int nconnections)
{
	unsigned int i;
	if (connections != NULL)
	{
		for(i=0; i<nconnections; i++)
			net_connection_delete_contents(connections+i);
		g_free(connections);
	}
}

void free_net_connections_array(GArray *connections)
{
	unsigned int i;
	if (connections != NULL)
	{
		for(i=0; i<connections->len; i++)
			net_connection_delete(g_array_index(connections, NetConnection*, i));
		g_array_free(connections, TRUE);
	}
}

void net_statistics_get (NetStatistics *net_stats)
{
	glibtop_netlist netlist;
	char **ifnames;
	int i;
	memset(net_stats, 0, sizeof(NetStatistics));

	ifnames = glibtop_get_netlist(&netlist);

	for (i = 0; i < netlist.number; i++)
	{
		glibtop_netload netload;
		glibtop_get_netload(&netload, ifnames[i]);

		if (netload.if_flags & (1 << GLIBTOP_IF_FLAGS_LOOPBACK))
			continue;

		if ( !( netload.flags & (1 << GLIBTOP_NETLOAD_ADDRESS) ) && 
		      ( !(netload.flags & (1 << GLIBTOP_NETLOAD_ADDRESS6)) || netload.scope6 == GLIBTOP_IF_IN6_SCOPE_LINK ) )
			continue;

		net_stats->bytes_sent += netload.bytes_out;
		net_stats->packets_sent += netload.packets_out;
		net_stats->bytes_received += netload.bytes_in;
		net_stats->packets_received += netload.packets_in;
	}

	g_strfreev(ifnames);
}

char *get_port_text (int port)
{
	char *port_text;
	if (port > 0)
		port_text = g_strdup_printf("%d", port);
	else
		port_text = g_strdup("*");
	return port_text;
}

char *get_port_name (int protocol, int port)
{
	const char *service_name = net_service_get(protocol, port);
	return (service_name != NULL) ? g_strdup(service_name) : NULL;
}

char *get_full_port_text(int protocol, int port)
{
	char *port_text;
	if (port > 0)
	{
		const char *service_name = net_service_get(protocol, port);
		if (service_name != NULL)
			port_text = g_strdup_printf("%d %s", port, service_name);
		else
			port_text = g_strdup_printf("%d", port);
	}
	else
		port_text = g_strdup("*");
	return port_text;
}

/* - At any moment protocol, addresses and ports are expected to form an unique combination. 
 * They don't, but applications that use this exception on purpose tend to be rare. A relatively 
 * common situation today (2015) where the same protocol, addresses and ports are used by more 
 * than one application is multicast dns (port 5353). As we compare connections at different 
 * time moments we might also get the same combinations from different processes.
 * - The inode compare is not usable all the time as some connection states do not have a 
 * file descriptor inode (ex: TIME_WAIT).
 * - Netactview needs to compare connections for a proper list update and may have connection 
 * tracking issues when there is no information to differentiate them. Even if in a static list 
 * the connections look the same the insertion and deletion order may be wrong. Also, the deletion 
 * and insertion of a new connection may be identified as a simple update. Netactview 1.2.4 handles 
 * well multicast dns (as userspace udp always has inode information). Netactview 0.7 will do 
 * more checks to minimize the number of problematic situations (like verify connection states) 
 * and display a hint in the interface when the matching is unsure. 
 */
int net_connection_net_equals_exact (NetConnection *nc1, NetConnection *nc2)
{
	/* not socket exact; ( && nc1->inode != 0) would ensure that its the same socket 
	 * fuzzy if (nc1->inode == nc2->inode == 0) */
	return ((nc1->inode == nc2->inode) &&
			(nc1->localport == nc2->localport) &&
			(nc1->remoteport == nc2->remoteport) && /* changed UDP connections are considered new */
			(nc1->protocol == nc2->protocol) &&
			(strcmp(nc1->remoteaddress, nc2->remoteaddress)==0) &&
			(strcmp(nc1->localaddress, nc2->localaddress)==0)
	       );
}

int net_connection_net_equals_fuzzy (NetConnection *nc1, NetConnection *nc2)
{
	return ((nc1->localport == nc2->localport) &&
			(nc1->remoteport == nc2->remoteport) &&
			(nc1->protocol == nc2->protocol) &&
			(nc1->inode == nc2->inode || nc1->inode == 0 || nc2->inode == 0) &&
			(strcmp(nc1->remoteaddress, nc2->remoteaddress)==0) &&
			(strcmp(nc1->localaddress, nc2->localaddress)==0)
	       );
}

int net_connection_info_equals (NetConnection *nc1, NetConnection *nc2)
{
	/* the connections are already net_equal; protocol, addresses and ports are the same */
	return ((nc1->state == nc2->state) && 
			(nc1->pid == nc2->pid)
			/* Assuming name and command modify in sync with pid; if not, a rare process name change 
			 * does not trigger a refresh; the old name should be representative enough */
	       );
}


static void update_string (char **old_str, const char *new_str)
{
	if (*old_str != NULL)
		g_free(*old_str);
	*old_str = (new_str != NULL) ? g_strdup(new_str) : NULL;
}

void net_connection_update (NetConnection *old_conn, NetConnection *new_conn)
{
	old_conn->state = new_conn->state;
	if (old_conn->localhost==NULL)
		update_string(&(old_conn->localhost), new_conn->localhost);
	if (old_conn->remotehost==NULL)
		update_string(&(old_conn->remotehost), new_conn->remotehost);
	old_conn->pid = new_conn->pid;
	if (new_conn->programpid != 0)
		old_conn->programpid = new_conn->programpid;
	if (new_conn->programname!=NULL)
		update_string(&(old_conn->programname), new_conn->programname);
	if (new_conn->programcommand!=NULL)
		update_string(&(old_conn->programcommand), new_conn->programcommand);
	old_conn->inode = new_conn->inode;
}

void net_connection_update_list_full (GArray *connections, NetConnection *latest_connections, 
                                      unsigned int nr_latest_connections)
{
	unsigned int i, j;
	GArray *valid_connections;
	
	g_assert(connections != NULL && (latest_connections != NULL || nr_latest_connections == 0));
	
	valid_connections = g_array_sized_new(FALSE, FALSE, sizeof(NetConnection*), connections->len);
	
	for (i=0; i<connections->len; i++)
	{
		NetConnection* conn = g_array_index(connections, NetConnection*, i);
		if (conn->operation != NC_OP_DELETE)
		{
			conn->operation = NC_OP_DELETE; /*DELETE if not found for update*/
			g_array_append_val(valid_connections, conn);
		}
	}
	
	for (i=0; i<nr_latest_connections; i++)
		latest_connections[i].operation = NC_OP_NONE;
	
	/* match first the connections that can be compared exactly */
	for (i=0; i<nr_latest_connections; i++)
	{
		NetConnection *new_conn = latest_connections + i, *old_conn = NULL;
		
		for (j=0; j<valid_connections->len; j++)
		{
			old_conn = g_array_index(valid_connections, NetConnection*, j);
			if (old_conn->operation == NC_OP_DELETE &&
			        net_connection_net_equals_exact(new_conn, old_conn))
			    break;
		}
		if (j < valid_connections->len) /*UPDATE or NONE*/
		{
			if (!net_connection_info_equals(new_conn, old_conn))
			{
				old_conn->operation = NC_OP_UPDATE;
				net_connection_update(old_conn, new_conn);
			}else
				old_conn->operation = NC_OP_NONE;
			new_conn->operation = NC_OP_DELETE;
		}
	}
	
	/* fuzzy matching on the remaining connections */
	for (i=0; i<nr_latest_connections; i++)
	{
		NetConnection *new_conn = latest_connections + i, *old_conn = NULL;
		if (new_conn->operation == NC_OP_DELETE)
			continue;
		
		for (j=0; j<valid_connections->len; j++)
		{
			old_conn = g_array_index(valid_connections, NetConnection*, j);
			if (old_conn->operation == NC_OP_DELETE && 
			        net_connection_net_equals_fuzzy(new_conn, old_conn))
			    break;
		}
		if (j < valid_connections->len) /*UPDATE or NONE*/
		{
			if (!net_connection_info_equals(new_conn, old_conn))
			{
				old_conn->operation = NC_OP_UPDATE;
				net_connection_update(old_conn, new_conn);
			}else
				old_conn->operation = NC_OP_NONE;
			new_conn->operation = NC_OP_DELETE;
		}
		else /*INSERT*/
		{
			NetConnection *added_conn = net_connection_new();
			net_connection_copy(added_conn, new_conn);
			added_conn->operation = NC_OP_INSERT;
			g_array_append_val(connections, added_conn);
		}
	}
	
	g_array_free(valid_connections, TRUE);
}


typedef union
{
	struct in_addr addr;
	struct in6_addr addr6;
}generic_in_addr;

static int generic_in_addr_is_zero (generic_in_addr *addr)
{
	return ( (addr->addr6.s6_addr32[0] == 0) && (addr->addr6.s6_addr32[1] == 0) && 
			(addr->addr6.s6_addr32[2] == 0) && (addr->addr6.s6_addr32[3] == 0) );
}

char *get_host_name_by_address (const char* saddress)
{
	char *host = NULL;
	
	if (strlen(saddress) <= 1)
	{
		host = NULL;
	}else
	{
		generic_in_addr addr;
		int address_format;
		
		memset(&addr, 0, sizeof(generic_in_addr));
		address_format = (strchr(saddress, ':')!=NULL) ? AF_INET6 : AF_INET;
		inet_pton(address_format, saddress, &addr);
		if (generic_in_addr_is_zero(&addr))
		{
			host = NULL;
		}else
		{
			int result, error;
			const size_t max_buffer_length = 128*1024;
			size_t buffer_length;
			struct hostent hostbuffer, *host_info = NULL;
			char *buffer;
			
			buffer_length = 1024;
			buffer = g_malloc(buffer_length);
			
			do
			{
				result = gethostbyaddr_r((char*)&addr, sizeof(generic_in_addr), address_format, 
										 &hostbuffer, buffer, buffer_length, &host_info, &error);
				if (result == ERANGE)
				{
					buffer_length *= 2;
					buffer = g_realloc(buffer, buffer_length);
				}
			}while (result == ERANGE && buffer_length <= max_buffer_length);
			
			host = (result == 0 && host_info!=NULL && host_info->h_name!=NULL) ? 
				g_strdup(host_info->h_name) : NULL;
			
			g_free(buffer);
		}
	}
	
	return host;
}

static int network_value_compare(unsigned int v1, unsigned int v2)
{
	v1 = ntohl(v1);
	v2 = ntohl(v2);
	return (v1 < v2) ? -1 : ( (v1 > v2) ? 1 : 0 );
}

int compare_addresses(const char *saddr1, const char *saddr2)
{
	int result = 0;
	
	if (strlen(saddr1)<=1 || strlen(saddr2)<=1) /*including '*' which is equivalent for 0.0.0.0, ::*/
	{
		result = strcmp(saddr1, saddr2);
	}else
	{
		int format1, format2;
		/* The address format is not always synchronized with the protocol.
		 * There are tcp6/udp6 connections with IPv4 addresses (wrapped IPv4 addresses) */
		format1 = (strchr(saddr1, ':')!=NULL) ?  AF_INET6 : AF_INET;
		format2 = (strchr(saddr2, ':')!=NULL) ?  AF_INET6 : AF_INET;
		if (format1 == format2)
		{
			if (format1 == AF_INET)
			{
				struct in_addr addr1 = {}, addr2 = {};		
				inet_pton(format1, saddr1, &addr1);
				inet_pton(format2, saddr2, &addr2);			
				result = network_value_compare(addr1.s_addr, addr2.s_addr);
			}else
			{
				struct in6_addr addr1 = {}, addr2 = {};		
				inet_pton(format1, saddr1, &addr1);
				inet_pton(format2, saddr2, &addr2);	
				result = network_value_compare(addr1.s6_addr32[0], addr2.s6_addr32[0]);
				if (result == 0)
					result = network_value_compare(addr1.s6_addr32[1], addr2.s6_addr32[1]);
				if (result == 0)
					result = network_value_compare(addr1.s6_addr32[2], addr2.s6_addr32[2]);
				if (result == 0)
					result = network_value_compare(addr1.s6_addr32[3], addr2.s6_addr32[3]);
			}
		}else
		{
			result = (format1 == AF_INET) ? -1 : 1;
		}
	}
	return result;
}

/* Compare domain names before subdomain names
 */
int compare_hosts(const char *host1, const char *host2)
{
	int result = 0;
	int len1 = strlen(host1), len2 = strlen(host2);
	if (len1 > 0 && len2 > 0)
	{
		while (result == 0 && len1 > 0 && len2 > 0)
		{
			int i1, i2, l1, l2;
			for(i1=len1-1; i1>=0; i1--)
				if (host1[i1]=='.')
					break;
			l1 = len1 - i1 - 1;
			for(i2=len2-1; i2>=0; i2--)
				if (host2[i2]=='.')
					break;
			l2 = len2 - i2 - 1;
			
			if (l1 > 0 && l2 > 0)
			{
				result = strncmp(host1+i1+1, host2+i2+1, (l1<l2) ? l1 : l2);
				if (result == 0 && l1 != l2)
					result = (l1 < l2) ? -1 : 1;
			}else
				result = (l1 < l2) ? -1 : ((l1 > l2) ? 1 : 0);
			
			len1 = i1;
			len2 = i2;
		}
		
	}else
		result = (len1 < len2) ? -1 : ((len1 > len2) ? 1 : 0);
	
	return result;
}

