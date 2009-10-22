/*
 *  RadeonHD.h
 *  RadeonHD
 *
 *  Created by Dong Luo on 5/19/08.
 *  Copyright 2008. All rights reserved.
 *
 */

#ifndef _RADEONHD_H
#define _RADEONHD_H

#include <IOKit/ndrvsupport/IONDRVFramebuffer.h>
#include "IONDRV.h"
#include "xf86str.h"

class NDRVHD : public IONDRV
{
    OSDeclareAbstractStructors(NDRVHD)
	
private:
    enum { kIOBootNDRVDisplayMode = 100 };
	
	UInt32							modeCount;
	IODisplayModeID					*modeIDs;
	IODetailedTimingInformationV2	*modeTimings;
	Fixed							*refreshRates;
	DisplayModePtr					startMode;
	
    void *	fAddress;
    UInt32	fRowBytes;
    UInt32	fWidth;
    UInt32	fHeight;
    UInt32	fBitsPerPixel;
	
	IODisplayModeID	fMode;
	Fixed	fRefreshRate;
	
	IOService		*fNub;
    IOMemoryMap		*IOMap;
    IOMemoryMap		*FBMap;
	RegEntryID		pciTag;
	pciVideoRec		PciInfo;
	bool			RHDReady;
	UserOptions		options;
	RHDMemoryMap	memoryMap;
	bool			debugMode;
	
public:
	
    static IONDRV * fromRegistryEntry( IORegistryEntry * regEntry, IOService * provider );
	
    virtual void free( void );
	
    virtual IOReturn getSymbol( const char * symbolName,
							   IOLogicalAddress * address );
	
    virtual const char * driverName( void );
	
    virtual IOReturn doDriverIO( UInt32 commandID, void * contents,
								UInt32 commandCode, UInt32 commandKind );
	
private:
	
    static bool getUInt32Property( IORegistryEntry * regEntry, const char * name,
								  UInt32 * result );
    IOReturn doControl( UInt32 code, void * params );
    IOReturn doStatus( UInt32 code, void * params );
	void setModel(IORegistryEntry *device);
};


enum {
	cscSetBackLightLevel = 100
};

enum {
	cscGetBackLightLevel = 100
};

class RadeonHD : public IONDRVFramebuffer
{
    OSDeclareDefaultStructors(RadeonHD)
	
protected:

public:
	/*! @function setCursorImage
	 @abstract Set a new image for the hardware cursor.
	 @discussion IOFramebuffer subclasses may implement hardware cursor functionality, if so they should implement this method to change the hardware cursor image. The image should be passed to the convertCursorImage() method with each type of cursor format the hardware supports until success, if all fail the hardware cursor should be hidden and kIOReturnUnsupported returned.
	 @param cursorImage Opaque cursor description. This should be passed to the convertCursorImage() method to convert to a format specific to the hardware.
	 @result An IOReturn code.
	 */
	
    //virtual IOReturn setCursorImage( void * cursorImage );
	
	/*! @function setCursorState
	 @abstract Set a new position and visibility for the hardware cursor.
	 @discussion IOFramebuffer subclasses may implement hardware cursor functionality, if so they should implement this method to change the position and visibility of the cursor.
	 @param x Left coordinate of the cursor image. A signed value, will be negative if the cursor's hot spot and position place it partly offscreen.
	 @param y Top coordinate of the cursor image. A signed value, will be negative if the cursor's hot spot and position place it partly offscreen.
	 @param visible Visible state of the cursor.
	 @result An IOReturn code.
	 */
	
    //virtual IOReturn setCursorState( SInt32 x, SInt32 y, bool visible );
	
    //// Controller attributes
	
    virtual IOReturn setAttribute( IOSelect attribute, uintptr_t value );
    virtual IOReturn getAttribute( IOSelect attribute, uintptr_t * value );
	
    virtual IOReturn doDriverIO( UInt32 commandID, void * contents,
								UInt32 commandCode, UInt32 commandKind );
};

#endif /* ! _RADEONHD_H */