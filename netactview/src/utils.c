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
#include "definitions.h"
#include "utils.h"

#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>


/* Exit the application with a message when a critical error occurs */
__attribute__((noreturn)) __attribute__((noinline))
void ErrorExit(const char *msg)
{
	syslog(LOG_CRIT, "Exit on error: %s", msg);
	g_error("%s", msg);
	exit(1);
}


gboolean convert_str_longl (const char *str, /*out*/long long *val, gboolean trim, 
                            long long minVal, long long maxVal, int base)
{
	char *endPtr = NULL;
	long long value1 = 0;
	g_assert(str != NULL && minVal <= maxVal && base >= 0);
	if (UNLIKELY(str == NULL))
		return FALSE;
	
	if (trim)
	{
		while(isspace(*str))
			str++;
	}
	else if (isspace(*str))
		return FALSE;
	
	errno = 0;
	value1 = strtoll(str, &endPtr, base);

	if (endPtr == str || errno != 0)
		return FALSE;

	if (trim)
	{
		while(isspace(*endPtr))
			endPtr++;
	}
	if (*endPtr != '\0')
		return FALSE;

	if (value1 < minVal || value1 > maxVal)
		return FALSE;

	if (val != NULL)
		*val = value1;
	return TRUE;
}

/* remove or add spaces at the end of a string to match, if possible, a given utf-8 length
 *  - dest should be a valid UTF-8 string allocated with glib malloc and may be reallocated
 *  - useful for constant display width as long as the variable part of the string does
 *    not contain combining marks; for complex scripts the utf8len is rarely the actual 
 *    display width in monospaced glyphs */
void match_string_utf8_len (char **dest, long utf8len)
{
	long dest_utf8len = g_utf8_strlen(*dest, strlen(*dest));
	if (dest_utf8len < utf8len)
	{
		char *sfill = g_strnfill(utf8len - dest_utf8len, ' ');
		char *new_str = g_strconcat(*dest, sfill, NULL);
		g_free(*dest);
		g_free(sfill);
		*dest = new_str;
	}
	else if (dest_utf8len > utf8len)
	{
		g_strchomp(g_utf8_offset_to_pointer(*dest, utf8len));
	}
}

char *get_bytes_string_base (unsigned long long nbytes, unsigned long long base,
							 const char *format, const char *formatd)
{
	char *str;
	unsigned long long decimalv; 
	decimalv = (nbytes < base * 10) ? (nbytes % base) * 10 / base : 0;
	if (decimalv == 0)
		str = g_strdup_printf(format, nbytes / base);
	else
		str = g_strdup_printf(formatd, nbytes / base, decimalv);
	return str;
}

char* get_bytes_string (unsigned long long nbytes)
{
	const unsigned long long ctKB = 1024, ctMB = ctKB*1024, 
		ctGB = ctMB*1024, ctTB = ctGB*1024;
	char *str;
	if (nbytes < ctKB)
		str = g_strdup_printf(_("%Lu B"), nbytes);
	else if (nbytes < ctMB)
		str = get_bytes_string_base(nbytes, ctKB, _("%Lu KB"), _("%Lu.%Lu KB"));
	else if (nbytes < ctGB)
		str = get_bytes_string_base(nbytes, ctMB, _("%Lu MB"), _("%Lu.%Lu MB"));
	else if (nbytes < ctTB)
		str = get_bytes_string_base(nbytes, ctGB, _("%Lu GB"), _("%Lu.%Lu GB"));
	else
		str = get_bytes_string_base(nbytes, ctTB, _("%Lu TB"), _("%Lu.%Lu TB"));
	return str;
}


struct _DropToSudoData
{
	size_t size;
	uid_t initialEffectiveUser;
	gid_t initialEffectiveGroup;
	int initialGroupsCount;
	gid_t *initialGroups;
};

/* Set the current effective user to the sudo user.
 * Used to save files with the sudo user as owner when possible (like in his home folder).
 * - Not thread safe. Other user and group functions may not be called at the same time.
 *   (ex: getpwuid, seteuid, setegid, setgroups) */
DropToSudoData* drop_to_sudo_user ()
{
	enum DropToSudoState { DTSS_UNSET, DTSS_SET_ERR, DTSS_SET_OK };

	enum DropToSudoState dropState = DTSS_UNSET;
	DropToSudoData *sudoData = NULL;
	gid_t *initialGroups = NULL;
	gid_t *userGroups  = NULL;

	if (geteuid() == 0)
	{
		const char *sudo_uid = getenv("SUDO_UID");
		const char *sudo_gid = getenv("SUDO_GID");

		long long sudoUserID = 0;
		gboolean sudoUserValid = (sudo_uid != NULL) && convert_str_longl(sudo_uid, &sudoUserID, TRUE, 0, UINT32_MAX, 10);

		if (sudoUserValid)
		{
			int initialGroupsCount = 0, initialGroupsCount2 = 0;
			struct passwd *sudoUserInfo = NULL;
			const char *sudoUserName = NULL;
			gid_t sudoUserGroupID = 0;
			int ggl_ret = 0, userGroupsCount = 0, userGroupsCount2 = 0;
			long long sudoGroupID = 0;
			gboolean dropOK = FALSE;
			
			sudoData = (DropToSudoData*)g_malloc0(sizeof(DropToSudoData));
			sudoData->size = sizeof(DropToSudoData);
			sudoData->initialEffectiveUser = geteuid();
			sudoData->initialEffectiveGroup = getegid();

			initialGroupsCount = getgroups(0, NULL);
			if (initialGroupsCount < 0) {
				nactv_trace("DropToSudoUser: Error on calling getGroups (%d)\n", errno); goto final; }
			if (initialGroupsCount > 0)
			{
				initialGroups = (gid_t*)g_malloc0(initialGroupsCount*sizeof(gid_t));

				initialGroupsCount2 = getgroups(initialGroupsCount, initialGroups);
				if (initialGroupsCount2 != initialGroupsCount) {
					nactv_trace("DropToSudoUser: Error on calling getGroups\n"); goto final; }
			}
			sudoData->initialGroupsCount = initialGroupsCount;
			sudoData->initialGroups = initialGroups;

			errno = 0;
			sudoUserInfo = getpwuid((uid_t)sudoUserID);
			if (sudoUserInfo == NULL) {
				nactv_trace("DropToSudoUser: Error getting user info for user %lld (%d)\n", sudoUserID, errno); goto final; }
			sudoUserName = sudoUserInfo->pw_name;
			sudoUserGroupID = sudoUserInfo->pw_gid;

			userGroupsCount = 0;
			ggl_ret = getgrouplist(sudoUserName, sudoUserGroupID, NULL, &userGroupsCount);
			if (ggl_ret != -1 || userGroupsCount < 1) {
				nactv_trace("DropToSudoUser: Invalid getgrouplist return\n"); goto final; }

			userGroupsCount2 = userGroupsCount + 16; /* The group database may change while running this. */
			userGroups = (gid_t*)g_malloc0(userGroupsCount2*sizeof(gid_t));
			ggl_ret = getgrouplist(sudoUserName, sudoUserGroupID, userGroups, &userGroupsCount2);
			if (ggl_ret < 0) {
				nactv_trace("DropToSudoUser: getgrouplist call failed\n"); goto final; }
			if (userGroupsCount2 != userGroupsCount)
				nactv_trace("DropToSudoUser warning: group database changed.");
			userGroupsCount = userGroupsCount2;

			sudoGroupID = 0;
			if (sudo_gid == NULL || !convert_str_longl(sudo_gid, &sudoGroupID, TRUE, 0, UINT32_MAX, 10))
				sudoGroupID = sudoUserGroupID;

			dropOK = TRUE;
			dropOK = dropOK && 0==setgroups(userGroupsCount, userGroups);
			dropOK = dropOK && 0==setegid((gid_t)sudoGroupID);
			dropOK = dropOK && 0==seteuid((uid_t)sudoUserID);

			if (!dropOK)
				nactv_trace("DropToSudoUser: Error dropping to sudo user (%d: %s).\n", errno, strerror(errno));

			dropState = (dropOK) ? DTSS_SET_OK : DTSS_SET_ERR;
		}
	}

final:
	if (userGroups != NULL)
		g_free(userGroups);
	if (sudoData != NULL && dropState != DTSS_SET_OK)
	{
		if (dropState == DTSS_SET_ERR)
			restore_initial_user(sudoData);
		else
		{
			if (initialGroups != NULL)
				g_free(initialGroups);
			g_free(sudoData);
		}
		sudoData = NULL;
	}
	return sudoData;
}

void restore_initial_user (DropToSudoData *sudoData)
{
	gboolean restoreOK = TRUE;
	if (sudoData == NULL)
		return;
	ERROR_IF(sudoData->size != sizeof(DropToSudoData));

	restoreOK = 0==seteuid(sudoData->initialEffectiveUser);
	restoreOK = 0==setegid(sudoData->initialEffectiveGroup) && restoreOK;
	restoreOK = 0==setgroups(sudoData->initialGroupsCount, sudoData->initialGroups) && restoreOK;

	if (!restoreOK)
		ErrorExit("RestoreInitialUser: effective user restore failed.\n");

	if (sudoData->initialGroups != NULL) {
		g_free(sudoData->initialGroups);
		sudoData->initialGroups = NULL;
	}
	sudoData->size = 0;
	g_free(sudoData);
}


char* get_effective_home_dir ()
{
	const char *path = NULL;
	struct passwd *pw = getpwuid(geteuid());
	if (pw != NULL && pw->pw_dir != NULL)
		path = pw->pw_dir;
	else
	{
		const char *home_dir = g_getenv("HOME");
		if (home_dir != NULL)
			path = home_dir;
		else
			path = g_get_home_dir(); /* before glib 2.36 it returns home for uid */
	}
	return (path != NULL && strlen(path) > 0) ? g_strdup(path) : NULL;
}

void set_clipboard_text (const char *text)
{
	GdkAtom atom;
	GtkClipboard *clipboard;
	
	atom = gdk_atom_intern("CLIPBOARD", FALSE);
	clipboard = gtk_clipboard_get(atom);
	
	gtk_clipboard_set_text(clipboard, text, -1);
}

void EnsureStringLen (char **str, int *maxSize, int len)
{
	int maxl = *maxSize;
	g_assert(str != NULL && *str != NULL && maxl > 0 && len >= 0);
	if (len < maxl)
		return;
	ERROR_IF(len > INT_MAX/2);
	while (len >= maxl)
		maxl *= 2;
	*str = (char*)g_realloc(*str, maxl);
	*maxSize = maxl;
}

char *string_replace (const char* str, const char * from, const char* to)
{
	size_t len = 0, fromlen = 0, tolen = 0, maxreslen = 0, i = 0, j = 0 , k = 0;
	char *res = NULL;
	g_assert(str != NULL && from != NULL && to != NULL);
	
	len = strlen(str);
	fromlen = strlen(from);
	tolen = strlen(to);	
	g_assert(fromlen > 0);
	
	ERROR_IF(tolen > 0 && (len > SIZE_MAX/tolen || len*tolen/fromlen > SIZE_MAX-2));
	maxreslen = (len*tolen)/fromlen + 1;
	if (maxreslen < len)
		maxreslen = len;
	res = (char*)g_malloc0(maxreslen + 1);

	for(i = 0, j = 0; i < len;)
	{
		k = 0;
		if (i < len-fromlen+1)
		{
			for(k=0; k<fromlen; k++)
				if (str[i+k] != from[k])
					break;
		}
		
		if (k == fromlen)
		{
			for(k=0; k<tolen; k++)
			{
				g_assert(j<maxreslen);
				res[j++] = to[k];
			}
			i+=fromlen;
		}else
		{
			g_assert(j<maxreslen);
			res[j++] = str[i++];			
		}
	}
	res[j] = '\0';
	return res;
}


const char *get_file_extension (const char *fileName)
{
	g_assert(fileName != NULL);
	const char *extension = strrchr(fileName, '.');
	if (extension != NULL && extension != fileName)
		extension++;
	else
		extension = fileName + strlen(fileName);
	return extension;
}


/* Read file and store up to maxDataLen bytes in FileReadBuf::data, plus a terminating null byte
 *  - data is NULL if the file can't be opened 
 *  - isComplete is true only if the file was completely read, without any errors, in a maxDataLen buffer */
FileReadBuf read_file_ex(const char *path, size_t maxDataLen, size_t initialBufSize)
{
	FileReadBuf readBuf = { NULL, 0, FALSE };
	FILE *f = NULL;
	ERROR_IF(path == NULL || maxDataLen < 1 || maxDataLen >= SSIZE_MAX || initialBufSize < 1);
	
	f = fopen(path, "r");
	if (f != NULL)
	{
		size_t maxBufSize = maxDataLen + 1;
		size_t bufSize = initialBufSize;
		readBuf.data = (char*)g_malloc(bufSize);
		readBuf.dataLen = fread(readBuf.data, 1, bufSize, f);
		while(readBuf.dataLen == bufSize && bufSize < maxBufSize)
		{
			size_t newBufSize = (bufSize <= SIZE_MAX/2) ? 2 * bufSize : SIZE_MAX;
			readBuf.data = (char*)g_realloc(readBuf.data, newBufSize);
			readBuf.dataLen += fread(readBuf.data+bufSize, 1, newBufSize-bufSize, f);
			bufSize = newBufSize;
		}
		
		readBuf.isComplete = (readBuf.dataLen <= maxDataLen && !ferror(f));
		if (readBuf.dataLen > maxDataLen)
			readBuf.dataLen = maxDataLen;
		readBuf.data[readBuf.dataLen] = '\0';
		
		fclose(f);
	}
	return readBuf;
}

void file_readbuf_free_data(FileReadBuf *readBuf)
{
	if (readBuf->data != NULL)
		g_free(readBuf->data);
	readBuf->data = NULL;
	readBuf->dataLen = 0;
	readBuf->isComplete = FALSE;
}


