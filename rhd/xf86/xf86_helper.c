/*
 *  xf86_helper.c
 *  RadeonHD
 *
 *  Created by Dong Luo on 10/6/09.
 *  Copyright 2009. All rights reserved.
 *
 */
#include "xf86str.h"

char * xstrdup(const char *s)
{
	int len = strlen(s) +1;
	char * ret = IOMalloc(sizeof(char) * len);
	if (ret) bcopy(s, ret, len);
	return ret;
}

UInt32
myPCIReadLong(RegEntryIDPtr node, LogicalAddress addr)
{
	UInt32 value;
	ExpMgrConfigReadLong(node, addr, &value);
	return value;
}

UInt16
myPCIReadWord(RegEntryIDPtr node, LogicalAddress addr)
{
	UInt16 value;
	ExpMgrConfigReadWord(node, addr, &value);
	return value;
}

UInt8
myPCIReadByte(RegEntryIDPtr node, LogicalAddress addr)
{
	UInt8 value;
	ExpMgrConfigReadByte(node, addr, &value);
	return value;
}

/*
 * Drivers can use these for using their own SymTabRecs.
 */

const char *
xf86TokenToString(SymTabPtr table, int token)
{
    int i;
	
    for (i = 0; table[i].token >= 0 && table[i].token != token; i++)
		;
	
    if (table[i].token < 0)
		return NULL;
    else
		return(table[i].name);
}
