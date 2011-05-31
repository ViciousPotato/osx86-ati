/*
 *  RadeonDumpClient.cpp
 *  RadeonHD
 *
 *  Created by Dong Luo on 2/2/11.
 *  Copyright 2011 Boston University. All rights reserved.
 *
 */

#include <IOKit/IOBufferMemoryDescriptor.h>
#include "RadeonDumpClient.h"
#include "Shared.h"
#include "logMsg.h"

#define super IOUserClient
OSDefineMetaClassAndStructors(RadeonDumpClient, IOUserClient);

IOReturn RadeonDumpClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
											   IOMemoryDescriptor **memory)
{
	IOReturn result;
	*options = 0;
	*memory = NULL;
	
#ifndef DEBUG
	result = kIOReturnUnsupported;
#else
	IOBufferMemoryDescriptor *memDesc;
	char *msgBuffer;
	
	switch (type) {
		case kRadeonDumpMemoryMessageBuffer:
			
			lockMsgBuffer();
			if (!DumpMsg.mMsgBufferSize) {
				unlockMsgBuffer();
				result = kIOReturnUnsupported;
				break;
			}
			memDesc = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared,
															DumpMsg.mMsgBufferSize);
			if (!memDesc) {
				unlockMsgBuffer();
				result = kIOReturnVMError;
				break;
			}
			msgBuffer = (char *) memDesc->getBytesNoCopy();
			bcopy(DumpMsg.mMsgBuffer, msgBuffer, DumpMsg.mMsgBufferSize);
			unlockMsgBuffer();
			
			*options |= kIOMapReadOnly;
			*memory = memDesc; // automatically released after memory is mapped into task
			result = kIOReturnSuccess;
			break;
		default:
			result = kIOReturnBadArgument;
			break;
	}
#endif
	return result;
}