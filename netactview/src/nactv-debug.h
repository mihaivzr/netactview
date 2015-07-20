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


/* Include this file before all other includes.
 */

#ifndef NACTV_DEBUG_H
#define NACTV_DEBUG_H

#undef NACTV_DEBUG
#undef NACTV_LOCAL_BUILD /*define this only if you run from the source directory*/

#ifndef NACTV_DEBUG
#define G_DISABLE_ASSERT
#endif

#ifdef NACTV_DEBUG
#include <stdarg.h>
#define nactv_trace(format...) g_print(format)
#else
#define nactv_trace(format...) ((void)0)
#endif


#endif /*NACTV_DEBUG_H*/
