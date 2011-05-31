/*
 *  RadeonDump.h
 *  RadeonHD
 *
 *  Created by Dong Luo on 10/19/09.
 *  Copyright 2009 Boston University. All rights reserved.
 *
 */

#ifndef _RADEONDUMP_H
#define _RADEONDUMP_H
#include <IOKit/IOService.h>

class RadeonDump : public IOService {

    OSDeclareDefaultStructors( RadeonDump );
	
public:
	
    virtual bool start( IOService * provider );
};

#endif