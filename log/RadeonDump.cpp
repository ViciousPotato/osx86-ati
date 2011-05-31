/*
 *  RadeonDump.cpp
 *  RadeonHD
 *
 *  Created by Dong Luo on 10/19/09.
 *  Copyright 2009 Boston University. All rights reserved.
 *
 */

#include "RadeonDump.h"

#define super IOService
OSDefineMetaClassAndStructors( RadeonDump, IOService );

bool RadeonDump::start( IOService * provider )
{
    if( !super::start( provider ))
        return( false );
	
    registerService();
	
    return( true );
}

