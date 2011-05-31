/*
 *  RadeonController.h
 *  RadeonHD
 *
 *  Created by Dong Luo on 5/16/11.
 *  Copyright 2011 Boston University. All rights reserved.
 *
 */
#ifndef _RadeonController_H
#define _RadeonController_H

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IORegistryEntry.h>
#include "xf86str.h"

#ifndef _IOKIT_IOMACOSTYPES_H
struct RegEntryID
{
	void * opaque[4];
};
typedef struct RegEntryID RegEntryID;
typedef RegEntryID *                    RegEntryIDPtr;
#endif

class RadeonController:public IOService {
	OSDeclareDefaultStructors(RadeonController)
	
public:
	
	IOPCIDevice * device;
	RegEntryID	nub;
    pciVideoRec pciRec;
	UserOptions options;
	
	IOMemoryMap * IOMap;
	IOMemoryMap * FBMap;
	RHDMemoryMap	memoryMap;

public:
	
    virtual IOService * probe(  IOService *     provider,
							  SInt32 *        score );
    virtual bool start( IOService * provider );
    virtual void stop( IOService * provider );
	
    virtual void free( void );
	
public:
	
	UserOptions * getUserOptions(void);
	
};

#endif