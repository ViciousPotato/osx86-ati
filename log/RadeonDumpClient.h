/*
 *  RadeonDumpClient.h
 *  RadeonHD
 *
 *  Created by Dong Luo on 2/2/11.
 *  Copyright 2011 Boston University. All rights reserved.
 *
 */

#ifndef _RADEONDUMPCLIENT_H
#define _RADEONDUMPCLIENT_H

#include <IOKit/IOUserClient.h>

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

class RadeonDumpClient : public IOUserClient
{
    OSDeclareDefaultStructors(RadeonDumpClient)
	
public:
    virtual IOReturn clientMemoryForType( UInt32 type,
										 IOOptionBits * options, IOMemoryDescriptor ** memory );
};

#endif /* ! _RADEONDUMPCLIENT_H */