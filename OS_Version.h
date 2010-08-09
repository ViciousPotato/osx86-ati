/*
 *  OS_Version.h
 *  RadeonHD
 *
 *  Created by Dong Luo on 8/6/10.
 *  Copyright 2010 Boston University. All rights reserved.
 *
 */
#ifndef _OS_VERSION_H
#define _OS_VERSION_H
#include "/usr/include/AvailabilityMacros.h"

#ifdef MAC_OS_X_VERSION_MAX_ALLOWED
#if MAC_OS_X_VERSION_MAX_ALLOWED <= MAC_OS_X_VERSION_10_5
#define MACOSX_10_5
#endif
#endif

#endif