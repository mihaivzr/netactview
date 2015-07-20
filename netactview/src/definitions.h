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

#ifndef NACTV_DEFINITIONS_H
#define NACTV_DEFINITIONS_H

#include "config.h"
#include "nactv-debug.h"

#ifdef NACTV_LOCAL_BUILD
#define GLADEDIR "src/"
#define EXECUTABLE_PATH "src/netactview"

#else /*ndef NACTV_LOCAL_BUILD*/

#define GLADEDIR DATADIR"/netactview/glade/"
#define EXECUTABLE_PATH BINDIR"/netactview"
#endif

#define GLADEFILE GLADEDIR"netactview.glade"

#endif /*NACTV_DEFINITIONS_H*/
