#ifndef _IOKIT_RADEONHDUSERClient_H
#define _IOKIT_RADEONHDUSERClient_H

#include <IOKit/IOUserClient.h>

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

class RadeonHDUserClient : public IOUserClient
{
    OSDeclareDefaultStructors(RadeonHDUserClient)
	
public:
    virtual IOReturn clientMemoryForType( UInt32 type,
										 IOOptionBits * options, IOMemoryDescriptor ** memory );
};

#endif /* ! _IOKIT_RADEONHDUSERClient_H */
