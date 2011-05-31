/*
 *  logMsg.c
 *  radeonFB
 *
 *  Created by Dong Luo on 2/2/11.
 *  Copyright 2011 Boston University. All rights reserved.
 *
 */

#include "logMsg.h"

#ifdef DEBUG
struct radeonDumpMsg DumpMsg = {0, 0, false, NULL, 0, 0, NULL};

void lockMsgBuffer(void)
{
	IOLock *mMessageLock = DumpMsg.mMessageLock;
	if (mMessageLock == NULL) return;
	IOLockLock(mMessageLock);
}

void unlockMsgBuffer(void)
{
	IOLock *mMessageLock = DumpMsg.mMessageLock;
	if (mMessageLock == NULL) return;
	IOLockUnlock(mMessageLock);
}

static char lastMsg[256] = "/0";
static char newMsg[256] = "/0";
static int lastMsgRepeat = 0;
void logMsg(UInt32 type, const char *format, ...)
{
	UInt32 mVerbose = DumpMsg.mVerbose;
	bool mMsgBufferEnabled = DumpMsg.mMsgBufferEnabled;
	char *mMsgBuffer = DumpMsg.mMsgBuffer;
	size_t mMsgBufferSize = DumpMsg.mMsgBufferSize;
	size_t mMsgBufferPos = DumpMsg.mMsgBufferPos;
	IOLock *mMessageLock = DumpMsg.mMessageLock;
	bool lockExists;
	int length;
	
	if (!mMsgBuffer) return;
	if (!type || !format) return;
	
	if (mMessageLock == NULL) lockExists = FALSE;
	else lockExists = TRUE;
	
	if (lockExists)
		lockMsgBuffer(); // utilize message buffer lock for console logging as well
	
	switch (type) {
		case kRadeonDumpMessageTypeDump:
			if (mVerbose < 2) break;
		case kRadeonDumpMessageTypeGeneral:
			if (mVerbose < 1) break;
		case kRadeonDumpMessageTypeError:
			IOLog("[RadeonHD]: ");
			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);
			va_start(args, format);
			length = vsnprintf(newMsg, 256, format, args);
			va_end(args);
			if (length < 0) {
				IOLog("warning: vsnprintf in dumpMsg failed\n");
				break;
			}
			if (!strncmp(newMsg, lastMsg, length)) {
				lastMsgRepeat++;
				break;
			}
			strncpy(lastMsg, newMsg, length);	//save newMsg to lastMsg
			if (lockExists && mMsgBufferEnabled) {
				if (lastMsgRepeat) {
					if (mMsgBufferPos < (mMsgBufferSize - 1)) {
						if (mMsgBufferPos != (mMsgBufferSize - 2)) {
							length = snprintf(&mMsgBuffer[mMsgBufferPos], mMsgBufferSize - mMsgBufferPos,
											   "Last message repeated %d times.\n", lastMsgRepeat);
							if (length > 0)
								DumpMsg.mMsgBufferPos += length;
							else if (length < 0)
								IOLog("warning: snprintf in dumpMsg failed\n");
						}
					}
					lastMsgRepeat = 0;
				}
				mMsgBufferPos = DumpMsg.mMsgBufferPos;
				if (mMsgBufferPos < (mMsgBufferSize - 1)) {
					if (mMsgBufferPos != (mMsgBufferSize - 2)) {
						va_start(args, format);
						length = vsnprintf(&mMsgBuffer[mMsgBufferPos], mMsgBufferSize - mMsgBufferPos,
										   format, args);
						va_end(args);
						if (length > 0)
							DumpMsg.mMsgBufferPos += length;
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

void enableMsgBuffer(bool isEnabled)
{
	if (DumpMsg.mMsgBufferEnabled == isEnabled) {
		LOGE("warning: enableMsgBuffer(%d) has no effect\n", isEnabled);
		return;
	}
	
	lockMsgBuffer();
	DumpMsg.mMsgBufferEnabled = isEnabled;
	if (isEnabled) {
		bzero(DumpMsg.mMsgBuffer, DumpMsg.mMsgBufferSize);
		DumpMsg.mMsgBufferPos = 0;
	}
	unlockMsgBuffer();
}
#endif
