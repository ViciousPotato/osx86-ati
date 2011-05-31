/*
 *  xf86.h
 *  RadeonHD
 *
 *  Created by Dong Luo on 6/20/08.
 *  Copyright 2008. All rights reserved.
 *
 */

#ifndef _XF86_H_
#define _XF86_H_

#include <IOKit/IOLib.h>
#include <IOKit/assert.h>
#include <string.h>
#include <IOKit/ndrvsupport/IOMacOSTypes.h>
#include "OS_Version.h"
#include "logMsg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef char Bool;
typedef int INT32;
typedef unsigned int CARD32;
typedef unsigned short CARD16;
typedef unsigned char CARD8;
typedef char * pointer;

typedef struct {
	unsigned short	red, green, blue;
} LOCO;

typedef union _DevUnion {
	pointer		ptr;
	long		val;
	unsigned long uval;
	pointer		(*fptr) (void);
} DevUnion;


// 10.5 condition
#ifdef MACOSX_10_5

#ifndef __IONDRV__
	struct RegEntryID
	{
		void * opaque[4];
	};
	typedef struct RegEntryID RegEntryID;
#endif	

#ifndef __IONDRVLIBRARIES__
	typedef RegEntryID *                    RegEntryIDPtr;
#endif
	
#endif
	
#ifndef __IONDRVLIBRARIES__
	OSErr ExpMgrConfigReadLong(
							   RegEntryIDPtr    node,
							   LogicalAddress   configAddr,
							   UInt32 *         valuePtr);
	
	OSErr ExpMgrConfigWriteLong(
								RegEntryIDPtr    node,
								LogicalAddress   configAddr,
								UInt32           value);
	
	OSErr ExpMgrConfigReadWord(
							   RegEntryIDPtr    node,
							   LogicalAddress   configAddr,
							   UInt16 *         valuePtr);
	
	OSErr ExpMgrConfigWriteWord(
								RegEntryIDPtr    node,
								LogicalAddress   configAddr,
								UInt16           value);
	
	OSErr ExpMgrConfigReadByte(
							   RegEntryIDPtr    node,
							   LogicalAddress   configAddr,
							   UInt8 *          valuePtr);
	
	OSErr ExpMgrConfigWriteByte(
								RegEntryIDPtr    node,
								LogicalAddress   configAddr,
								UInt8            value);
	
	OSErr ExpMgrIOReadLong(
						   RegEntryIDPtr    node,
						   LogicalAddress   ioAddr,
						   UInt32 *         valuePtr);

#endif

extern UInt32 myPCIReadLong(RegEntryIDPtr node, LogicalAddress addr);
extern UInt16 myPCIReadWord(RegEntryIDPtr node, LogicalAddress addr);
extern UInt8 myPCIReadByte(RegEntryIDPtr node, LogicalAddress addr);
#define pciReadLong(node, addr) myPCIReadLong((node), (LogicalAddress)(unsigned long)(addr))
#define pciWriteLong(node, addr, value) ExpMgrConfigWriteLong((node), (LogicalAddress)(unsigned long)(addr), (UInt32)(value))
#define pciReadWord(node, addr) myPCIReadWord((node), (LogicalAddress)(unsigned long)(addr))
#define pciWriteWord(node, addr, value) ExpMgrConfigWriteWord((node), (LogicalAddress)(unsigned long)(addr), (UInt16)(value))
#define pciReadByte(node, addr) myPCIReadByte((node), (LogicalAddress)(unsigned long)(addr))
#define pciWriteByte(node, addr, value) ExpMgrConfigWriteByte((node), (LogicalAddress)(unsigned long)(addr), (UInt8)(value))

#include <Kern/debug.h>
		
extern void *kern_os_malloc(size_t size);
extern void kern_os_free(void * addr);

#define xalloc(size) kern_os_malloc((size_t)(size))
#define xfree(addr) kern_os_free((void *)(addr))
	
extern char * xstrdup(const char *s);
#define strdup xstrdup
#define xnfstrdup xstrdup
#define abs(a) ((a) < 0)?(-(a)):(a)
	
#ifdef __cplusplus
}
#endif

#endif /* _XF86_H_ */
