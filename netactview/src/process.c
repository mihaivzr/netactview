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

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include "nactv-debug.h"
#include "process.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>


static int is_simple_number (const char* text)
{
	for (; *text!='\0'; text++)
		if (!isdigit(*text))
			break;
	return (*text=='\0');
}

#define MAX_CMDLINE_LEN (32*1024)

static char *read_cmdline_file (const char *path)
{
	char *contents = NULL;
	FILE *f;
	
	f = fopen(path, "r");
	if (f != NULL)
	{
		char *line;
		size_t line_size;
		ssize_t line_length;
	
		line_size = 128;
		line = malloc(line_size);
		ERROR_IF(line == NULL);
		line_length = getline(&line, &line_size, f);
		
		if (line_length > 0)
		{
			ssize_t i;
			if (line_length > MAX_CMDLINE_LEN)
			{
				line_length = MAX_CMDLINE_LEN;
				line[line_length] = '\0';
			}			
			for (i=0; i<line_length; i++)
				if (line[i] == '\0')
					line[i] = ' ';
			contents = g_filename_display_name(line);
		}
		
		free(line);
		fclose(f);
	}
	return contents;
}

unsigned int get_running_processes (Process **processes)
{
	unsigned int nrprocesses = 0;
	DIR *procdir;
	g_assert(*processes == NULL);
	*processes = NULL;
	
	procdir = opendir("/proc");
	if (procdir != NULL)
	{
		struct dirent dentry, *pdentry;
		GArray *aprocesses;
		
		aprocesses = g_array_sized_new(FALSE, TRUE, sizeof(Process), 128);
		
		while ( readdir_r(procdir, &dentry, &pdentry) == 0 )
		{
			Process process = {};
			
			if (pdentry == NULL)
				break;
			if (pdentry->d_type != DT_DIR)
				continue;
			if (!is_simple_number(pdentry->d_name))
				continue;
			long pid = atol(pdentry->d_name);
			if (pid == 0)
				continue;

			process.pid = pid;
			g_array_append_val(aprocesses, process);
		}
		closedir (procdir);
		
		nrprocesses = aprocesses->len;
		*processes = (nrprocesses > 0) ? (Process*)aprocesses->data : NULL;
		g_array_free(aprocesses, *processes==NULL);
	}
	
	return nrprocesses;
}

void update_process_info (Process *process)
{
	char spid[48];
	char *exeLinkPath, *exeFilePath, *cmdlineFilePath;

	process_delete_contents(process);
	
	n_snprintf(spid, sizeof(spid), "%ld", process->pid);
	
	exeLinkPath = g_build_path("/", "/proc", spid, "exe", NULL);
	exeFilePath = g_file_read_link(exeLinkPath, NULL);
	if (exeFilePath != NULL)
	{
		char *exeName = g_path_get_basename(exeFilePath);
		process->name = g_filename_display_name(exeName);
		g_free(exeName);
	}else
		process->name = NULL;

	cmdlineFilePath = g_build_path("/", "/proc", spid, "cmdline", NULL);
	process->commandline = read_cmdline_file(cmdlineFilePath);		
	
	if (exeFilePath != NULL)
		g_free(exeFilePath);
	g_free(exeLinkPath);
	g_free(cmdlineFilePath);	
}


static const size_t MaxSocketNameSize = 64;  /* max_size = max_len + 1 */
static const char ProcSocketFormat1[] = "socket:[";
static const size_t ProcSocketFormat1Len = sizeof(ProcSocketFormat1)-1;
static const char ProcSocketFormat2[] = "[0000]:";
static const size_t ProcSocketFormat2Len = sizeof(ProcSocketFormat2)-1;

/* lname = file descriptor linked name; NULL terminated at lnameLen */
static void extract_type_1_socket_inode (const char *lname, size_t lnameLen,
                                         unsigned long *inode)
{
	/* If lname is of the form "socket:[12345]", extract the "12345"
	 * as *inode. Otherwise set *inode = 0.
	 */
	*inode = 0;
	if (lnameLen < ProcSocketFormat1Len+2)
		*inode = 0;
	else if (memcmp(lname, ProcSocketFormat1, ProcSocketFormat1Len) != 0)
		*inode = 0;
	else if (lname[lnameLen-1] != ']')
		*inode = 0;
	else
	{
		const char *inodeStr = lname + ProcSocketFormat1Len;
		char *endP = NULL;
		
		errno = 0;
		unsigned long inodeVal = strtoul(inodeStr, &endP, 0);
		
		if (endP == (lname+lnameLen-1) && errno == 0)
			*inode = inodeVal;
		else
			*inode = 0;
	}
}

/* lname = file descriptor linked name; NULL terminated at lnameLen */
static void extract_type_2_socket_inode (const char *lname, size_t lnameLen,
                                         unsigned long *inode)
{
	/* If lname is of the form "[0000]:12345", extract the "12345"
	 * as *inode. Otherwise set *inode = 0.
	 */
	*inode = 0;
	if (lnameLen < ProcSocketFormat2Len+1)
		*inode = 0;
	else if (memcmp(lname, ProcSocketFormat2, ProcSocketFormat2Len) != 0)
		*inode = 0;
	else
	{
		const char *inodeStr = lname + ProcSocketFormat2Len;
		char *endP = NULL;
		
		errno = 0;
		unsigned long inodeVal = strtoul(inodeStr, &endP, 0);
		
		if (endP == (lname+lnameLen) && errno == 0)
			*inode = inodeVal;
		else
			*inode = 0;
	}
}


unsigned int process_get_socket_inodes (long pid, unsigned long **inodes)
{
	char spid[48];
	char *sfddir;
	DIR *fddir;
	unsigned int nsockets = 0;
	g_assert(*inodes == NULL);
	*inodes = NULL;
	
	n_snprintf(spid, sizeof(spid), "%ld", pid);
	sfddir = g_build_path("/", "/proc", spid, "fd", NULL);
	
	fddir = opendir (sfddir);
	if (fddir != NULL)
	{
		struct dirent *fdentry, dentry;
		GArray *ainodes;
		
		ainodes = g_array_new(FALSE, TRUE, sizeof(unsigned long));
		
		while ( readdir_r (fddir, &dentry, &fdentry) == 0 )
		{
			char *file_name, *link_path;
			
			if (fdentry==NULL)
				break;
			if (fdentry->d_type != DT_LNK)
				continue;
			link_path = g_build_path("/", sfddir, fdentry->d_name, NULL);
			file_name = g_file_read_link(link_path, NULL);
			
			if (file_name != NULL)
			{
				unsigned long inode = 0;
				size_t file_name_len = strlen(file_name);
				
				extract_type_1_socket_inode(file_name, file_name_len, &inode);
				if (inode == 0)
					extract_type_2_socket_inode(file_name, file_name_len, &inode);
					
				if (inode > 0)
				{
					g_array_append_val(ainodes, inode);
				}
			}
			
			if (file_name != NULL)
				g_free(file_name);
			g_free(link_path);
		}
		closedir(fddir);
		
		nsockets = ainodes->len;
		*inodes = (ainodes->len > 0) ? (unsigned long*)ainodes->data : NULL;
		g_array_free(ainodes, (*inodes==NULL));
	}
	
	g_free(sfddir);
	return nsockets;	
}


void process_delete_contents (Process *process)
{
	if (process != NULL)
	{
		if (process->name != NULL)
		{
			g_free(process->name);
			process->name = NULL;
		}
		if (process->commandline != NULL)
		{
			g_free(process->commandline);
			process->commandline = NULL;
		}
	}
}

void free_processes (Process *processes, unsigned int nprocesses)
{
	unsigned int i;
	if (processes != NULL)
	{
		for (i=0; i<nprocesses; i++)
			process_delete_contents(processes + i);
		g_free(processes);
	}
}

