#include <IOKit/IOBufferMemoryDescriptor.h>
#include "xf86.h"
#include "Shared.h"
#include "RadeonHDUserClient.h"

#define super IOUserClient
OSDefineMetaClassAndStructors(RadeonHDUserClient, IOUserClient);

IOReturn RadeonHDUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
												  IOMemoryDescriptor **memory)
{
	IOReturn result;
	IOBufferMemoryDescriptor *memDesc;
	char *msgBuffer;
	
	
	*options = 0;
	*memory = NULL;
	
	switch (type) {
		case kVoodooHDAMemoryMessageBuffer:
			lockMsgBuffer();
			if (!xf86Msg.mMsgBufferSize) {
				unlockMsgBuffer();
				result = kIOReturnUnsupported;
				break;
			}
			memDesc = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared,
															xf86Msg.mMsgBufferSize);
			if (!memDesc) {
				unlockMsgBuffer();
				result = kIOReturnVMError;
				break;
			}
			msgBuffer = (char *) memDesc->getBytesNoCopy();
			bcopy(xf86Msg.mMsgBuffer, msgBuffer, xf86Msg.mMsgBufferSize);
			unlockMsgBuffer();
			*options |= kIOMapReadOnly;
			*memory = memDesc; // automatically released after memory is mapped into task
			result = kIOReturnSuccess;
			break;
		default:
			result = kIOReturnBadArgument;
			break;
	}
	
	return result;
}
