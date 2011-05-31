/*
 *  logMsg.h
 *  radeonFB
 *
 *  Created by Dong Luo on 2/2/11.
 *  Copyright 2011 Boston University. All rights reserved.
 *
 */

#ifndef _LOGMSG_H
#define _LOGMSG_H
#include <IOKit/IOLib.h>

#ifdef __cplusplus
extern "C" {
#endif
	
#define DEBUG

#ifdef DEBUG
enum {
	kRadeonDumpMessageTypeGeneral = 0x2000,
	kRadeonDumpMessageTypeError,
	kRadeonDumpMessageTypeDump
};

extern struct radeonDumpMsg {
	UInt32 mVerbose;
	UInt8 client;
	bool mMsgBufferEnabled;
	char *mMsgBuffer;
	size_t mMsgBufferSize;
	size_t mMsgBufferPos;
	IOLock *mMessageLock;
} DumpMsg;

extern void lockMsgBuffer(void);
extern void unlockMsgBuffer(void);
extern void enableMsgBuffer(bool isEnabled);
extern void logMsg(UInt32 type, const char *format, ...) __attribute__ ((format (printf, 2, 3)));
#endif

#ifdef DEBUG
#define LOG(fmt, args...) do { logMsg(kRadeonDumpMessageTypeGeneral, fmt, ## args); } while (0)
#define LOGE(fmt, args...) do { logMsg(kRadeonDumpMessageTypeError, fmt, ## args); } while (0)
#define LOGV(fmt, args...) do { logMsg(kRadeonDumpMessageTypeDump, fmt, ## args); } while (0)
#else
#define LOG(fmt, args...) do {} while (0)
#define LOGE(fmt, args...) do {} while (0)
#define LOGV(fmt, args...) do {} while (0)
#endif
	
#define FUNC LOG("%s\n", __func__);
	
#ifdef __cplusplus
}
#endif

#endif