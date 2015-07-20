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
#include "filter.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

#define MAX_FILTER_LEN 100100100

#define WHITESPACECHARS " \t\r\n"

#define FindSChar(pps, ch) ((**(pps) == (ch)) ? ((*(pps))++, (ch)) : 0)
#define FindNotSChar(pps, ch) ( ((**(pps) != 0) && (**(pps)) != (ch)) ? ((*(pps))++, (ch)) : 0 )


static void SkipSep(const char** pCh);
static int FindSep(const char** pCh);
static char FindChar(const char** pCh, const char *fchls);
static char FindNotChar(const char** pCh, const char *fchls);
static int FindStr(const char** pCh, const char *str);
static int FindStrNoCase(const char** pCh, const char *str);

static FilterOperand* GetFilterExpression(const char** pCh, char** pMask);

static FilterOperand* CaseFoldOperandUTF8(FilterOperand* oper);
static int NodeIsFiltered (const char* entryText, FilterOperand* op, int caseSensitivity);
static void PrintNode(char** result, int* resultMaxLen, FilterOperand* op);
static void FreeOperand(FilterOperand* oper);


Filter* ParseFilter (const char* filterText, char **filterMask)
{
	char *localMask = NULL, *ptempMask = NULL;
	const char *ptempText = NULL;
	FilterOperand *filter = NULL;
	ERROR_IF(strlen(filterText) > MAX_FILTER_LEN);
	
	localMask = PreParseFilter(filterText);
	g_assert(strlen(filterText) == strlen(localMask));

	ptempText = filterText;
	ptempMask = localMask;
	filter = GetFilterExpression(&ptempText, &ptempMask);
	
	if (filterMask != NULL)
		*filterMask = localMask;
	else
		g_free(localMask);
	
	return filter;
}

Filter* ParseFilterNoOperators (const char* filterText)
{
	FilterOperand *filter = NULL, *lastNode = NULL, *node = NULL;
	gchar** pStr;
	gchar **strList;
	ERROR_IF(strlen(filterText) > MAX_FILTER_LEN);

	strList = g_strsplit_set(filterText, WHITESPACECHARS, -1);	
	for(pStr = strList; *pStr != 0; pStr++)
	{
		if (strlen(*pStr) > 0)
		{
			node = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
			node->value = g_strdup(*pStr);

			if (lastNode != NULL)
			{
				FilterOperand *andOp = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
				andOp->operator = ovAND;

				lastNode->sibling = andOp;
				andOp->sibling = node;				
			}else
				filter = node;
			lastNode = node;
		}
	}
	g_strfreev(strList);
		
	return filter;
}

int IsFiltered (const char* entryText, Filter* filter, int caseSensitivity)
{
	g_assert(entryText != NULL);
	return NodeIsFiltered(entryText, filter, caseSensitivity);	
}

char* PrintFilter (Filter* filter)
{
	char *result;
	int resultMaxLen;
	if (filter == NULL)
		return g_strdup("");

	resultMaxLen = 64;
	result = (char*)g_malloc0(resultMaxLen);

	PrintNode(&result, &resultMaxLen, filter);	
	
	return result;
}

Filter* AddFilterOperand (Filter *filter, int binOperator, int isNot, const char *operandText)
{
	return AddOperand(filter, binOperator, isNot, operandText);
}

FilterOperand* AddOperand (FilterOperand *operand, int binOperator, int isNot, const char *operandText)
{
	FilterOperand *newOperand = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
	g_assert(operandText != NULL);
	g_assert(binOperator == ovOR || binOperator == ovAND);
	ERROR_IF(strlen(operandText) > MAX_FILTER_LEN);
	
	newOperand->value = g_strdup(operandText);
	newOperand->operator = (isNot) ? ovNOT : ovNone;	
	if (operand == NULL)
		return newOperand;
	else
	{
		FilterOperand *node = operand;
		while(node->sibling != NULL)
			node = node->sibling;
		FilterOperand *newOperator = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
		newOperator->operator = binOperator;
		newOperator->sibling = newOperand;
		node->sibling = newOperator;
		return operand;
	}
}

Filter* AddFilterGroup (Filter *filter, int binOperator, int isNot, FilterOperand* children)
{
	return AddGroup(filter, binOperator, isNot, children);
}

FilterOperand* AddGroup (FilterOperand *operand, int binOperator, int isNot, FilterOperand* children)
{
	FilterOperand *newOperand = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
	newOperand->child = children;
	newOperand->operator = (isNot) ? ovNOTGroup : ovGroup;
	
	g_assert(binOperator == ovOR || binOperator == ovAND);
	if (operand == NULL)
		return newOperand;
	else
	{
		FilterOperand *node = operand;
		while(node->sibling != NULL)
			node = node->sibling;
		FilterOperand *newOperator = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
		newOperator->operator = binOperator;
		newOperator->sibling = newOperand;
		node->sibling = newOperator;
		return operand;
	}
}

Filter* CaseFoldFilterUTF8 (Filter* filter)
{
	return CaseFoldOperandUTF8(filter);
}

void FreeFilter (Filter* filter)
{
	if (filter != NULL)
		FreeOperand(filter);
}





static FilterOperand* GetFilterExpression (const char** pCh, char** pMask)
{
	const char *pLocalCh = *pCh;
	char *pLocalMask = *pMask;
	FilterOperand *expression = NULL, *pLastNode = NULL, *pNode = NULL;
	int lastBinOp = ovNone;
	int insideExpression = TRUE;
	int notState = FALSE;
	
	while(*pLocalMask == ocIgnored || *pLocalMask == ocNone) {
		pLocalMask++; pLocalCh++;
	}

	while(insideExpression)
	{
		pNode = NULL;
		switch(*pLocalMask)
		{
			case 0:
			case ocEndGroup:
				insideExpression = FALSE;
				break;
			case ocStartGroup:
			{				
				pLocalMask++; pLocalCh++;
				FilterOperand *groupExpression = GetFilterExpression(&pLocalCh, &pLocalMask);
				g_assert(*pLocalMask == ocEndGroup);
				if (*pLocalMask == ocEndGroup) { pLocalMask++; pLocalCh++; }

				pNode = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
				pNode->operator = (notState) ? ovNOTGroup : ovGroup;
				notState = FALSE;
				pNode->child = groupExpression;
				break;
			}
			case ocNOT:
				pLocalMask++; pLocalCh++;
				notState = !notState;
				break;
			case ocNone:
				while(*pLocalMask == ocNone || *pLocalMask == ocIgnored) { pLocalMask++; pLocalCh++; }
				if (lastBinOp == ovNone)
					lastBinOp = ovAND;
				break;
			case ocOR:
				while(*pLocalMask == ocOR) { pLocalMask++; pLocalCh++; }
				lastBinOp = ovOR;
				break;
			case ocFreeString:
			case ocQuote:
			{
				int stringMaxLen = 64, actualLen = 0;
				char *string = (char*)g_malloc0(stringMaxLen);

				while(*pLocalMask == ocQuote || *pLocalMask == ocFreeString)
				{
					if (*pLocalMask == ocQuote)
					{
						const char *pLocalStr = NULL;
						int localStrLen = 0;
						
						pLocalMask++; pLocalCh++;
						pLocalStr = pLocalCh;
						while(*pLocalMask == ocQuoteString) { pLocalMask++; pLocalCh++; }
						localStrLen = pLocalCh - pLocalStr;
						if (localStrLen > 0)
						{
							actualLen += localStrLen;						
							EnsureStringLen(&string, &stringMaxLen, actualLen);
							strncat(string, pLocalStr, localStrLen);
						}
						g_assert(*pLocalMask == ocQuote);
						if (*pLocalMask == ocQuote) { pLocalMask++; pLocalCh++; } 
					}else
					{
						int localStrLen = 0;
						const char *pLocalStr = pLocalCh;
						while(*pLocalMask == ocFreeString) { pLocalMask++; pLocalCh++; }
						localStrLen = pLocalCh - pLocalStr;
						g_assert(localStrLen > 0);
						actualLen += localStrLen;						
						EnsureStringLen(&string, &stringMaxLen, actualLen);
						strncat(string, pLocalStr, localStrLen);
					}
					while(*pLocalMask == ocIgnored) { pLocalMask++; pLocalCh++; }
				}							
				
				pNode = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
				pNode->value = string_replace(string, "\"\"", "\"");	
				pNode->operator = (notState) ? ovNOT : ovNone;
				notState = FALSE;
				g_free(string);
				break;
			}
			default:
				g_assert(0);
				pLocalMask++; pLocalCh++;
				break;
		}
	
		if (pNode != NULL)
		{
			if (pLastNode != NULL)
			{
				g_assert(lastBinOp != ovNone);
				if (lastBinOp == ovNone)
					lastBinOp = ovAND;
				FilterOperand *oper = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
				oper->operator = lastBinOp;
				oper->sibling = pNode;
				pLastNode->sibling = oper;
			}
			else
				expression = pNode;
			pLastNode = pNode;
			lastBinOp = ovNone;
		}
		while(*pLocalMask == ocIgnored) { pLocalMask++; pLocalCh++; }
	}

	*pCh = pLocalCh;
	*pMask = pLocalMask;
	return expression;
}


static void SkipSep (const char** pCh)
{
	while(FindChar(pCh, WHITESPACECHARS)) (void)0;
}

static int FindSep (const char** pCh)
{
	int ret = FindChar(pCh, WHITESPACECHARS);
	if (ret)
		SkipSep(pCh);
	return (ret != 0);
}

static char FindChar (const char** pCh, const char *fchls)
{
	char ch = **pCh;
	if (ch == 0)
		return 0;
	while(*fchls != 0)
	{
		if (ch == *fchls)
		{
			(*pCh)++;
			return ch;
		}
		fchls++;
	}
	return 0;
}

static char FindNotChar (const char** pCh, const char *fchls)
{
	char ch = **pCh;
	if (ch == 0)
		return 0;
	while(*fchls != 0)
	{
		if (ch == *fchls)
			return 0;
		fchls++;
	}	
	(*pCh)++;
	return ch;
}


static int FindStr (const char** pCh, const char *str)
{
	int slen = strlen(str);
	int cmpret = strncmp(*pCh, str, slen);
	g_assert(slen > 0);
	if (cmpret == 0)
	{
		(*pCh)+=slen;
		return TRUE;
	}
	return FALSE;
}

static int FindStrNoCase (const char** pCh, const char *str)
{
	int slen = strlen(str);
	int cmpret = strncasecmp(*pCh, str, slen);
	g_assert(slen > 0);
	if (cmpret == 0)
	{
		(*pCh)+=slen;
		return TRUE;
	}
	return FALSE;
}	


static int NodeIsFiltered (const char* entryText, FilterOperand* op, int caseSensitivity)
{
	int filtered = TRUE;	
	int curentOp = ovNone;
	g_assert(caseSensitivity >= casSensitive);
	while(op != NULL)
	{
		if (op->value != NULL || op->operator == ovGroup || op->operator == ovNOTGroup)
		{
			if (curentOp == ovNone || (filtered && curentOp == ovAND) || (!filtered && curentOp == ovOR))
			{
				int elementFiltered = TRUE;
				if (op->value != NULL)
				{
					int contains = FALSE;
					switch(caseSensitivity)
					{
						case casSensitive:
							contains = (strstr(entryText, op->value) != 0);
							break;
						case casInsensitiveLibc:
							contains = (strcasestr(entryText, op->value) != 0);
							break;
						default:
							g_assert(0);
					}
					elementFiltered = (op->operator != ovNOT) ? contains : !contains;
				}else if (op->operator == ovGroup || op->operator == ovNOTGroup)
				{
					int groupFiltered = NodeIsFiltered(entryText, op->child, caseSensitivity);
					elementFiltered = (op->operator == ovGroup) ? groupFiltered : !groupFiltered;
				}

				switch(curentOp)
				{
					case ovNone:
						filtered = elementFiltered;
						break;
					case ovAND:
						filtered = filtered && elementFiltered;
						break;
					case ovOR:
						filtered = filtered || elementFiltered;
						break;
				}
			}
		}else if (op->operator == ovAND || op->operator == ovOR)
		{
			curentOp = op->operator;
		}else 
			g_assert(0);
		
		op = op->sibling;
	}
	return filtered;
}


static void PrintNode (char** result, int* resultMaxLen, FilterOperand* op)
{
	int resultLen = strlen(*result); /*estimated result len >= real len*/
	while(op != NULL)
	{
		ERROR_IF(resultLen > MAX_FILTER_LEN);
		
		if (op->value != NULL)
		{
			char *outvalue = string_replace(op->value, "\"", "\"\"");
			resultLen += strlen("!") + strlen("\"\"") + strlen(outvalue);
			EnsureStringLen(result, resultMaxLen, resultLen);
			if (op->operator == ovNOT)
				strcat(*result, "!");
			strcat(*result, "\"");
			strcat(*result, outvalue);
			strcat(*result, "\"");
			g_free(outvalue);
			
		}else if (op->operator == ovAND)
		{
			resultLen += strlen(" ");
			EnsureStringLen(result, resultMaxLen, resultLen);
			strcat(*result, " ");
		}else if (op->operator == ovOR)
		{
			resultLen += strlen(" OR ");
			EnsureStringLen(result, resultMaxLen, resultLen);
			strcat(*result, " OR ");
		}else if (op->operator == ovGroup || op->operator == ovNOTGroup)
		{
			resultLen += strlen("!") + strlen("(");
			EnsureStringLen(result, resultMaxLen, resultLen);
			if (op->operator == ovNOTGroup)
				strcat(*result, "!");
			strcat(*result, "(");
			PrintNode(result, resultMaxLen, op->child);
			resultLen = strlen(*result) + strlen(")");
			EnsureStringLen(result, resultMaxLen, resultLen);
			strcat(*result, ")");
		}else
			g_assert(0);
		
		op = op->sibling;
	}
	ERROR_IF(resultLen > MAX_FILTER_LEN);
}		


static FilterOperand* CaseFoldOperandUTF8 (FilterOperand* oper)
{
	if (oper != NULL)
	{
		FilterOperand* res = (FilterOperand*)g_malloc0(sizeof(FilterOperand));
		res->operator = oper->operator;
		if (oper->value != NULL)
		{
			res->value = g_utf8_casefold(oper->value, -1);
			ERROR_IF(strlen(res->value) > MAX_FILTER_LEN);
		}
		if (oper->sibling != NULL)
			res->sibling = CaseFoldOperandUTF8(oper->sibling);
		if (oper->child != NULL)
			res->child = CaseFoldFilterUTF8(oper->child);
		return res;
	}
	return NULL;
}


static void FreeOperand (FilterOperand* oper)
{
	if (oper != NULL)
	{
		FreeOperand(oper->child);
		FreeOperand(oper->sibling);
		if (oper->value != NULL)
			g_free(oper->value);
		g_free(oper);
	}
}



char* PreParseFilter (const char* filter)
{
	int len = 0;
	const char *pCh = NULL, *pLocalCh = NULL;
	char* filterMask = NULL;
	int pos = 0, i = 0;
	IntArray groupStack = {};
	int previous = ocNone;
	char lastOpElMask = ocNone;
	
	if (filter == NULL)
		return NULL;
	ERROR_IF(strlen(filter) > MAX_FILTER_LEN);
	len = strlen(filter);
	
	filterMask = (char*)g_malloc0(len + 1);
	if (len == 0)
		return filterMask;
	memset(filterMask, ocNone, len);
	
	IntArrayInit(&groupStack, 32);

	pCh = filter;
	previous = ocNone;
	while(*pCh != '\0')
	{
		if (FindSep(&pCh))
			previous = ocNone;
		
		if (FindSChar(&pCh, '('))
		{
			pos = pCh - filter - 1;
			if (previous == ocNone || previous == ocStartGroup || previous == ocNOT) 
			{				
				filterMask[pos] = ocStartGroup;
				previous = ocStartGroup;
				IntArrayAdd(&groupStack, pos);
			}else
			{
				filterMask[pos] = ocIgnored;
			}
		}else if (FindSChar(&pCh, ')'))
		{
			pos = pCh - filter - 1;
			if (groupStack.len > 0)
			{				
				filterMask[pos] = ocEndGroup;
				previous = ocEndGroup;
				IntArrayRemoveLast(&groupStack);
			}else
			{
				filterMask[pos] = ocIgnored;
			}
		}else if (FindSChar(&pCh, '\"'))
		{
			int quoteOpened = TRUE;
			pos = pCh - filter - 1;
			pLocalCh = pCh;			

			while(quoteOpened)
			{
				while(FindNotSChar(&pLocalCh, '\"')) (void)0;
				quoteOpened = FindStr(&pLocalCh, "\"\"");
			}

			if (FindSChar(&pLocalCh, '\"'))
			{
				int posEnd = pLocalCh - filter - 1;
				if (previous == ocNone || previous == ocStartGroup || previous == ocNOT || previous == ocQuote || previous == ocFreeString)
				{				
					filterMask[pos] = ocQuote;
					filterMask[posEnd] = ocQuote;
					for(i=pos+1; i<posEnd; i++)
						filterMask[i] = ocQuoteString;
					previous = ocQuote;					
				}else
				{
					for(i=pos; i<=posEnd; i++)
						filterMask[i] = ocIgnored;
				}
				pCh = pLocalCh;
			}else
			{
				filterMask[pos] = ocIgnored;	
			}
		}else if (FindSChar(&pCh, '!'))
		{
			pos = pCh - filter - 1;
			if (previous == ocNone || previous == ocNOT || previous == ocStartGroup)
			{
				filterMask[pos] = ocNOT;
				previous = ocNOT;
			}else
				filterMask[pos] = ocIgnored;
		}else
		{
			int orFound = FALSE;

			if ((previous == ocNone || previous == ocStartGroup || previous == ocNOT || previous == ocEndGroup)
		         && FindStrNoCase(&pCh, "OR"))
			{
				pos = pCh - filter - 2;
				if (*pCh == '\0' || FindChar(&pCh, WHITESPACECHARS))
				{
					if (previous == ocNone)
					{
						filterMask[pos] = filterMask[pos+1] = ocOR;						
					}else
						filterMask[pos] = filterMask[pos+1] = ocIgnored;
					previous = ocNone;
					orFound = TRUE;
				}else if (FindChar(&pCh, "()!"))
				{
					filterMask[pos] = filterMask[pos+1] =  ocIgnored;
					pCh--;
					orFound = TRUE;
				}
				else
					pCh = filter + pos;
			}

			if (!orFound)
			{
				if ((previous == ocNone || previous == ocStartGroup || previous == ocNOT || previous == ocQuote || previous == ocFreeString)
				    && FindNotChar(&pCh, WHITESPACECHARS "\"" "()!"))
				{
					pos = pCh - filter - 1;
					while(FindNotChar(&pCh, WHITESPACECHARS "\"" "()!")) (void)0;

					int posEnd = pCh - filter - 1;

					for(i=pos; i<=posEnd; i++)
						filterMask[i] = ocFreeString;
					previous = ocFreeString;
					
				}else
				{
					if (*pCh != '\0')
					{
						pos = pCh - filter;
						filterMask[pos] = ocIgnored;
						pCh++;
					}
				}
			}
		}		
	}

	for(i=0; i<groupStack.len; i++)
	{
		pos = IntArrayE(&groupStack, i);
		filterMask[pos] = ocIgnored;
	}
	IntArrayFreeInternal(&groupStack);

	lastOpElMask = ocNone;
	for(i=0; i<len; i++)
	{
		char mask = filterMask[i];
		if (mask == ocIgnored)
			continue;

		if (mask == ocOR)
		{
			if (lastOpElMask == ocNOT || lastOpElMask == ocNone || lastOpElMask == ocStartGroup || lastOpElMask == ocOR)
			{
				filterMask[i] = ocIgnored;
				filterMask[i+1] = ocIgnored;
			}
			i++;
		}

		if (mask != ocNone)
			lastOpElMask = mask;
	}
	ERROR_IF(filterMask[len] != '\0');
	
	return filterMask;
}




