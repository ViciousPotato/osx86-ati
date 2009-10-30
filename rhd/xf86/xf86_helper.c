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

#ifndef USEIOLOG && defined DEBUG
void lockMsgBuffer()
{
	IOLock *mMessageLock = xf86Msg.mMessageLock;
	if (mMessageLock == NULL) return;
	IOLockLock(mMessageLock);
}

void unlockMsgBuffer()
{
	IOLock *mMessageLock = xf86Msg.mMessageLock;
	if (mMessageLock == NULL) return;
	IOLockUnlock(mMessageLock);
}

static void messageHandler(UInt32 type, const char *format, va_list args)
{
	UInt32 mVerbose = xf86Msg.mVerbose;
	bool mMsgBufferEnabled = xf86Msg.mMsgBufferEnabled;
	char *mMsgBuffer = xf86Msg.mMsgBuffer;
	size_t mMsgBufferSize = xf86Msg.mMsgBufferSize;
	size_t mMsgBufferPos = xf86Msg.mMsgBufferPos;
	IOLock *mMessageLock = xf86Msg.mMessageLock;
	Bool lockExists;
	
	if (!type || !format || !args) return;
	
	if (mMessageLock == NULL) lockExists = FALSE;
	else lockExists = TRUE;
	
	if (lockExists)
		lockMsgBuffer(); // utilize message buffer lock for console logging as well
	
	switch (type) {
			int length;
		case kVoodooHDAMessageTypeDump:
			if (mVerbose < 2) break;
		case kVoodooHDAMessageTypeGeneral:
			if (mVerbose < 1) break;
		case kVoodooHDAMessageTypeError:
			vprintf(format, args);
			if (lockExists && mMsgBufferEnabled) {
				if (mMsgBufferPos < (mMsgBufferSize - 1)) {
					if (mMsgBufferPos != (mMsgBufferSize - 2)) {
						length = vsnprintf(mMsgBuffer + mMsgBufferPos, mMsgBufferSize - mMsgBufferPos,
										   format, args);
						if (length > 0)
							xf86Msg.mMsgBufferPos += length;
						else if (length < 0)
							IOLog("warning: vsnprintf in dumpMsg failed\n");
					}
				}
			}
		default:
			break;
	}
	
	if (lockExists)
		unlockMsgBuffer();
}

void logMsg(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	messageHandler(kVoodooHDAMessageTypeGeneral, format, args);
	va_end(args);
}

void errorMsg(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	messageHandler(kVoodooHDAMessageTypeError, format, args);
	va_end(args);
}

void dumpMsg(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	messageHandler(kVoodooHDAMessageTypeDump, format, args);
	va_end(args);
}

void enableMsgBuffer(bool isEnabled)
{
	if (xf86Msg.mMsgBufferEnabled == isEnabled) {
		errorMsg("warning: enableMsgBuffer(%d) has no effect\n", isEnabled);
		return;
	}
	
	lockMsgBuffer();
	xf86Msg.mMsgBufferEnabled = isEnabled;
	if (isEnabled) {
		bzero(xf86Msg.mMsgBuffer, xf86Msg.mMsgBufferSize);
		xf86Msg.mMsgBufferPos = 0;
	}
	unlockMsgBuffer();
}
#endif
