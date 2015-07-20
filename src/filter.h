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

#ifndef NACTV_FILTER_H
#define NACTV_FILTER_H

enum OperatorValues { ovNone, ovAND, ovOR, ovGroup, ovNOTGroup, ovNOT };
enum OperatorChar   { ocNone = 48, ocIgnored, ocFreeString/*2*/, ocQuoteString, ocQuote/*4*/, ocOR, ocNOT, ocStartGroup/*7*/, ocEndGroup };

typedef struct _FilterOperand FilterOperand;

struct _FilterOperand
{
	char *value;
	int operator;
	FilterOperand *sibling;
	FilterOperand *child;
};

typedef FilterOperand Filter;

enum CaseSensitivity { casSensitive = 4, casInsensitiveLibc };

char* PreParseFilter (const char* filterText);
Filter* ParseFilter (const char* filterText, char **filterMask);
Filter* ParseFilterNoOperators (const char* filterText);
int IsFiltered (const char* entryText, Filter* filter, int caseSensitivity);

Filter* AddFilterOperand (Filter *filter, int binOperator, int isNot, const char *operandText);
FilterOperand* AddOperand (FilterOperand *operand, int binOperator, int isNot, const char *operandText);
Filter* AddFilterGroup (Filter *filter, int binOperator, int isNot, FilterOperand* children);
FilterOperand* AddGroup (FilterOperand *operand, int binOperator, int isNot, FilterOperand* children);

Filter* CaseFoldFilterUTF8 (Filter* filter);

char* PrintFilter (Filter* filter);

void FreeFilter (Filter* filter);


#endif
