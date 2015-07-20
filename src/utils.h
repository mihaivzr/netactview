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

#ifndef NACTV_UTILS_H
#define NACTV_UTILS_H

#include "nactv-debug.h"
#include <stdint.h>
#include <limits.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>


#define QUOTE_ITEM(item) #item
#define QUOTE(str) QUOTE_ITEM(str)

#if defined(NACTV_DEBUG)
#define LIKELY(exp)    exp
#define UNLIKELY(exp)  exp
#else
#define LIKELY(exp)    __builtin_expect((exp) != 0, 1)
#define UNLIKELY(exp)  __builtin_expect((exp), 0)
#endif

void ErrorExit(const char *msg) __attribute__((noreturn)) __attribute__((noinline));
#define ERROR_IF(exp)  do{ if (UNLIKELY(exp)) ErrorExit("ERROR_IF(" QUOTE(exp) ") in \"" __FILE__ "\" at line " QUOTE(__LINE__) "."); }while(0)
#define ERROR_MSG_IF(exp, msg)  do{ if (UNLIKELY(exp)) ErrorExit(msg); }while(0)


/* remove or add spaces at the end of a string to match, if possible, a given utf-8 length
 *  - dest should be a valid UTF-8 string allocated with glib malloc and may be reallocated */
void match_string_utf8_len (char **dest, long utf8len);

char *get_bytes_string_base (unsigned long long nbytes, unsigned long long base,
                             const char *format, const char *formatd);
char* get_bytes_string (unsigned long long nbytes);

void set_clipboard_text (const char *text);

void EnsureStringLen (char **str, int *maxSize, int len);
/*Allocates a very large pesimistic buffer inside. Do not use if 'from' is very short compared to 'to' on a large 'str'.*/
char *string_replace (const char* str, const char * from, const char* to);

const char *get_file_extension (const char *fileName);


struct _DropToSudoData;
typedef struct _DropToSudoData DropToSudoData;

/* Set the current effective user to the sudo user.
 * Used to save files with the sudo user as owner when possible (like in his home folder).
 * - Not thread safe. Other user and group functions may not be called at the same time.
 *   (ex: getpwuid, seteuid, setegid, setgroups) */
DropToSudoData* drop_to_sudo_user ();
void restore_initial_user (DropToSudoData *sudoHandle);

char* get_effective_home_dir ();


typedef struct
{
	char *data; /* data[dataLen]=0; may contain '\0'; bufSize >= dataLen+1 */
	size_t dataLen;
	gboolean isComplete;
}FileReadBuf;

/* Read file and store up to maxDataLen bytes in FileReadBuf::data, plus a terminating null byte
 *  - data is NULL if the file can't be opened 
 *  - isComplete is true only if the file was completely read, without any errors, in a maxDataLen buffer */
FileReadBuf read_file_ex(const char *path, size_t maxDataLen, size_t initialBufSize);
void file_readbuf_free_data(FileReadBuf *readBuf);


/************** Inline implementations: */

/* snprintf function that does not allow string truncation */
__attribute__ ((format (printf, 3, 4)))
static inline int n_snprintf (char *str, size_t size, const char *format, ...)
{
	int print_len = 0;
	va_list args;
	va_start(args, format);
	print_len = vsnprintf(str, size, format, args);
	va_end(args);
	ERROR_IF(print_len < 0 || (size_t)print_len >= size);
	return print_len;
}

/* strlcpy function that does not allow string truncation */
static inline size_t n_strlcpy (char *dest, const char *src, size_t dest_size)
{
	size_t src_len = g_strlcpy(dest, src, dest_size);
	ERROR_IF(src_len >= dest_size);
	return src_len;
}


/**********************************IntArray*/

typedef struct
{
	int *data;
	int len;
	int allocatedLen;
}IntArray;

static inline void IntArrayInit (IntArray *arr, int startAllocLen)
{
	ERROR_IF(startAllocLen > INT_MAX/sizeof(int) || startAllocLen < 2);
	arr->allocatedLen = startAllocLen;
	arr->data = (int*)g_malloc0(arr->allocatedLen * sizeof(int));
	arr->len = 0;
}

static inline void IntArrayFreeInternal (IntArray *arr)
{
	if (arr->data != NULL)
	{
		g_free(arr->data);
		arr->data = NULL;
	}
	arr->len = arr->allocatedLen = 0;
}

static inline void IntArrayEnsureAlloc (IntArray *arr, int nrAddItems)
{
	if (nrAddItems > arr->allocatedLen - arr->len)
	{
		int newAllocLen;
		ERROR_IF(nrAddItems > INT_MAX - arr->len || 
		         arr->len + nrAddItems > INT_MAX/(2*sizeof(int)));
		newAllocLen = (arr->len + nrAddItems) * 2;		
		arr->data = (int*)g_realloc(arr->data, newAllocLen * sizeof(int));
		memset(arr->data + arr->allocatedLen, 0, (newAllocLen - arr->allocatedLen) * sizeof(int));
		arr->allocatedLen = newAllocLen;
	}	
}

static inline void IntArrayAdd (IntArray *arr, int x)
{
	IntArrayEnsureAlloc(arr, 1);
	arr->data[arr->len] = x;
	arr->len++;
}

static inline void IntArrayRemoveLast (IntArray *arr)
{
	ERROR_IF(arr->len <= 0);
	arr->len--;
}

static inline int IntArrayE (IntArray *arr, int i)
{
	g_assert(i>=0 && i<arr->len);
	return arr->data[i];
}

/**********************************IntArray*/


#endif /*NACTV_UTILS_H*/
