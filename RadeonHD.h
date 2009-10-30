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
	UInt32	fBitsPerComponent;
	IOIndex	fDepth;
	
	IODisplayModeID	fMode;
	Fixed	fRefreshRate;
	
	GammaTbl		*gTable;
	
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
	
    // Controller attributes
	
    virtual IOReturn setAttribute( IOSelect attribute, uintptr_t value );
    virtual IOReturn getAttribute( IOSelect attribute, uintptr_t * value );
	
    virtual IOReturn doDriverIO( UInt32 commandID, void * contents,
								UInt32 commandCode, UInt32 commandKind );
	
	/*! @function setAttributeForConnection
	 @abstract Generic method to set some attribute of the framebuffer device, specific to one display connection.
	 @discussion IOFramebuffer subclasses may implement this method to allow arbitrary attribute/value pairs to be set, specific to one display connection. 
	 @param attribute Defines the attribute to be set. Some defined attributes are:<br> 
	 kIOCapturedAttribute If the device supports hotplugging displays, it should disable the generation of hot plug interrupts when the attribute kIOCapturedAttribute is set to true.
	 @param value The new value for the attribute.
	 @result an IOReturn code.
	 */
	
    virtual IOReturn setAttributeForConnection( IOIndex connectIndex,
											   IOSelect attribute, UInt32 value );

	/*! @function getAttributeForConnection
	 @abstract Generic method to retrieve some attribute of the framebuffer device, specific to one display connection.
	 @discussion IOFramebuffer subclasses may implement this method to allow arbitrary attribute/value pairs to be returned, specific to one display connection. 
	 @param attribute Defines the attribute to be returned. Some defined attributes are:<br> 
	 kConnectionSupportsHLDDCSense If the framebuffer supports the DDC methods hasDDCConnect() and getDDCBlock() it should return success (and no value) for this attribute.<br>
	 kConnectionSupportsLLDDCSense If the framebuffer wishes to make use of IOFramebuffer::doI2CRequest software implementation of I2C it should implement the I2C methods setDDCClock(), setDDCData(), readDDCClock(), readDDCData(), and it should return success (and no value) for this attribute.<br>
	 @param value Returns the value for the attribute.
	 @result an IOReturn code.
	 */
	
    virtual IOReturn getAttributeForConnection( IOIndex connectIndex,
											   IOSelect attribute, UInt32 * value );
};

#endif /* ! _RADEONHD_H */