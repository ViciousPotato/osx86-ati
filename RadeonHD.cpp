/*
 *  RadeonHD.cpp
 *  RadeonHD
 *
 *  Created by Dong Luo on 5/19/08.
 *  Copyright 2008. All rights reserved.
 *
 */

#include <IOKit/IOLib.h>
#include <IOKit/platform/ApplePlatformExpert.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "RadeonHD.h"

#define RHD_VBIOS_BASE 0xC0000
#define RHD_VBIOS_SIZE 0x10000

extern "C" Bool RadeonHDPreInit(ScrnInfoPtr pScrn, RegEntryIDPtr pciTag, RHDMemoryMap *pMemory, pciVideoPtr PciInfo, UserOptions *options);
extern "C" void RadeonHDFreeScrn(ScrnInfoPtr pScrn);
extern "C" Bool RadeonHDSetMode(ScrnInfoPtr pScrn, UInt32 modeID, UInt16 depth);
extern "C" Bool RadeonHDGetSetBKSV(UInt32 *value, Bool set);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#undef super
#define super IONDRV
OSDefineMetaClassAndStructors(NDRVHD, IONDRV)

bool NDRVHD::getUInt32Property( IORegistryEntry * regEntry, const char * name,
							   UInt32 * result )
{
    OSData * data;
	
    data = OSDynamicCast(OSData, regEntry->getProperty(name));
    if (data)
        *result = *((UInt32 *) data->getBytesNoCopy());
	
    return (data != 0);
}

IONDRV * NDRVHD::fromRegistryEntry( IORegistryEntry * regEntry, IOService * provider )
{
    NDRVHD *		inst;
	
	inst = new NDRVHD;
	if (!inst || !inst->init()) return NULL;
	
 	inst->fNub = provider;
	inst->IOMap = NULL;
	inst->FBMap = NULL;

	IOPCIDevice * pciDevice = OSDynamicCast(IOPCIDevice, regEntry);
	if (!pciDevice) return NULL;
	MAKE_REG_ENTRY(&inst->pciTag, pciDevice);

	//get user options
	OSDictionary *dict = OSDynamicCast(OSDictionary, provider->getProperty("UserOptions"));
	
	bzero(&inst->options, sizeof(UserOptions));
	//inst->options.HWCursorSupport = false;
	//inst->options.modeNameByUser[0] = 0;

	inst->debugMode = false;
	inst->options.enableBacklight = true;
	if (dict) {
		OSBoolean *prop = OSDynamicCast(OSBoolean, dict->getObject("debugMode"));
		if (prop) inst->debugMode = prop->getValue();
		prop = OSDynamicCast(OSBoolean, dict->getObject("enableBacklight"));
		if (prop) inst->options.enableBacklight = prop->getValue();
	}
#ifndef USEIOLOG && defined DEBUG
	xf86Msg.mVerbose = 1;
	xf86Msg.mMsgBufferSize = 65535;
	if (dict) {
		OSNumber *optionNum = OSDynamicCast(OSNumber, dict->getObject("verboseLevel"));
		if (optionNum) xf86Msg.mVerbose = optionNum->unsigned32BitValue();
		optionNum = OSDynamicCast(OSNumber, dict->getObject("MsgBufferSize"));
		if (optionNum) xf86Msg.mMsgBufferSize = optionNum->unsigned32BitValue();
	}	
	inst->options.verbosity = xf86Msg.mVerbose;
	xf86Msg.mMsgBufferEnabled = false;
	xf86Msg.mMsgBufferSize = max(65535, xf86Msg.mMsgBufferSize);
	xf86Msg.mMsgBufferPos = 0;
	xf86Msg.mMessageLock = IOLockAlloc();
	xf86Msg.mMsgBuffer = (char *) IOMalloc(xf86Msg.mMsgBufferSize);
	if (!xf86Msg.mMsgBuffer) {
		errorMsg("error: couldn't allocate message buffer (%ld bytes)\n", xf86Msg.mMsgBufferSize);
		return false;
	}
	enableMsgBuffer(true);
#endif
	inst->memoryMap.EDID_Block = NULL;
	inst->memoryMap.EDID_Length = 0;
	if (dict) {
		OSData *edidData = OSDynamicCast(OSData, dict->getObject("EDID"));
		if (edidData) {
			inst->memoryMap.EDID_Block = (unsigned char *)xalloc(edidData->getLength());
			if (inst->memoryMap.EDID_Block) {
				bcopy(edidData->getBytesNoCopy(), inst->memoryMap.EDID_Block, edidData->getLength());
				inst->memoryMap.EDID_Length = edidData->getLength();
			}
		}
	}
	
	//get other pciDevice info
	pciDevice->setIOEnable(true);
	pciDevice->setMemoryEnable(true);
	
	// map IO memory
	inst->IOMap = pciDevice->mapDeviceMemoryWithRegister( kIOPCIConfigBaseAddress2 );
	inst->memoryMap.MMIOBase = (pointer) inst->IOMap->getVirtualAddress();
	inst->memoryMap.MMIOMapSize = inst->IOMap->getLength();
	LOG("Mapped IO at %p (size 0x%08X)\n", inst->memoryMap.MMIOBase, inst->memoryMap.MMIOMapSize);
	// map FB memory may should use the one mapped by IONDRVSupport
	inst->FBMap = pciDevice->mapDeviceMemoryWithRegister( kIOPCIConfigBaseAddress0 );
	inst->memoryMap.FbBase = (pointer) inst->FBMap->getVirtualAddress();
	inst->memoryMap.FbMapSize = inst->FBMap->getLength();
	LOG("FB at %p (size 0x%08X) mapped to %p\n", (pointer) inst->FBMap->getPhysicalAddress(), inst->memoryMap.FbMapSize, inst->memoryMap.FbBase);
	// backup BIOS
	inst->memoryMap.BIOSCopy = NULL;
	inst->memoryMap.BIOSLength = 0;
	
	IOMemoryDescriptor * mem;
	mem = IOMemoryDescriptor::withPhysicalAddress((IOPhysicalAddress) RHD_VBIOS_BASE, RHD_VBIOS_SIZE, kIODirectionOut);
	if (mem) {
		inst->memoryMap.BIOSCopy = (unsigned char *)xalloc(RHD_VBIOS_SIZE);
		if (inst->memoryMap.BIOSCopy) {
			mem->prepare(kIODirectionOut);
			if (!(inst->memoryMap.BIOSLength = mem->readBytes(0, inst->memoryMap.BIOSCopy, RHD_VBIOS_SIZE))) {
				LOG("Cannot read BIOS image\n");
				inst->memoryMap.BIOSLength = 0;
			}
			if ((unsigned int)inst->memoryMap.BIOSLength != RHD_VBIOS_SIZE)
				LOG("Read only %d of %d bytes of BIOS image\n", inst->memoryMap.BIOSLength, RHD_VBIOS_SIZE);
			mem->complete(kIODirectionOut);
		}
	}
	
	inst->PciInfo.chipType = pciDevice->configRead16(kIOPCIConfigDeviceID);
	inst->PciInfo.subsysVendor = pciDevice->configRead16(kIOPCIConfigSubSystemVendorID);
	inst->PciInfo.subsysCard = pciDevice->configRead16(kIOPCIConfigSubSystemID);
	inst->PciInfo.biosSize = 16;	//RHD_VBIOS_SIZE = 1 << 16
	
	//if (inst->debugMode) {	//in debugMode, VESA mode is used
        PE_Video	bootDisplay;
        UInt32		bpp;
		
        IOService::getPlatform()->getConsoleInfo( &bootDisplay);
	    inst->fAddress	    = (void *) bootDisplay.v_baseAddr;
	    inst->fRowBytes	    = bootDisplay.v_rowBytes;
	    inst->fWidth	    = bootDisplay.v_width;
	    inst->fHeight	    = bootDisplay.v_height;
	    bpp = bootDisplay.v_depth;
	    if (bpp == 15)
			bpp = 16;
	    else if (bpp == 24)
			bpp = 32;
		
	    inst->fBitsPerPixel = bpp;
	/*
	} else {
		inst->fAddress = (void *)inst->FBMap->getPhysicalAddress();
		inst->fBitsPerPixel = 32;
	} */
	if (inst->fAddress != (void *)inst->FBMap->getPhysicalAddress()) {
		LOG("Different FBPhyAddr detected: 0x%p (VESA), 0x%p\n", inst->fAddress, (void *)inst->FBMap->getPhysicalAddress());
		inst->fAddress = (void *)inst->FBMap->getPhysicalAddress();
	}
	inst->fMode = kIOBootNDRVDisplayMode;
	inst->fRefreshRate = 0 << 16;
	inst->memoryMap.FbPhysBase = (unsigned long)inst->fAddress;
	inst->memoryMap.bitsPerPixel = inst->fBitsPerPixel;
	
	//now initialize RHD
	inst->RHDReady = false;
	xf86Screens[0] = IONew(ScrnInfoRec, 1);	//using global variable, will change it later
	ScrnInfoPtr pScrn = xf86Screens[0];
	if (pScrn) {
		bzero(pScrn, sizeof(ScrnInfoRec));
		pScrn->scrnIndex = 0;
		do {
			if (!RadeonHDPreInit(pScrn, &inst->pciTag, &inst->memoryMap, &inst->PciInfo, &inst->options )) break;
			inst->modeCount = 0;
			DisplayModePtr BigestMode = NULL;
			DisplayModePtr mode = pScrn->modes;
			if (mode) LOG("Display resolutions detected: \n");
			while (mode)	//no longer a circular list, so won't be trapped here
			{
				if ((mode->HDisplay == inst->fWidth) && (mode->VDisplay == inst->fHeight))
					inst->startMode = mode;
				
				if (pScrn->NativeMode && (mode->HDisplay == pScrn->NativeMode->HDisplay)
					&& (mode->VDisplay == pScrn->NativeMode->VDisplay)) {
					pScrn->NativeMode = mode;	//refered back to mode with modeID
					LOG("%d X %d @ %dHz (Native mode)\n", mode->HDisplay, mode->VDisplay, (int) mode->VRefresh);
				}
				LOG("%d X %d @ %dHz\n", mode->HDisplay, mode->VDisplay, (int) mode->VRefresh);
				
				if (!BigestMode || ((BigestMode->HDisplay < mode->HDisplay) && (BigestMode->VDisplay < mode->VDisplay)))
					BigestMode = mode;

				inst->modeCount++;
				mode = mode->next;
			}
			if (!inst->startMode) inst->startMode = pScrn->NativeMode;
			if (!inst->startMode) inst->startMode = BigestMode;
			
			inst->modeTimings = IONew(IODetailedTimingInformationV2, inst->modeCount);
			inst->modeIDs = IONew(IODisplayModeID, inst->modeCount);
			inst->refreshRates = IONew(Fixed, inst->modeCount);
			if (!inst->modeTimings || !inst->modeIDs || !inst->refreshRates) break;
			IODetailedTimingInformationV2 *dtInfo;
			unsigned int i;
			for (i = 0;i < inst->modeCount;i++) inst->modeIDs[i] = 1 + i;	//let's start with 1
			mode = pScrn->modes;
			i = 0;
			while (mode && (i < inst->modeCount)) {
				dtInfo = &inst->modeTimings[i];
				bzero(dtInfo, sizeof(IODetailedTimingInformationV2));
				dtInfo->pixelClock = mode->Clock * 1000;
				dtInfo->minPixelClock = dtInfo->pixelClock;
				dtInfo->maxPixelClock = dtInfo->pixelClock;
				dtInfo->horizontalActive = mode->HDisplay;
				dtInfo->horizontalBlanking = mode->HTotal - mode->HDisplay;
				dtInfo->horizontalSyncOffset = mode->HSyncStart - mode->HDisplay;
				dtInfo->horizontalSyncPulseWidth = mode->HSyncEnd - mode->HSyncStart;
				dtInfo->horizontalSyncConfig = (mode->Flags & V_NHSYNC)?0:1;
				dtInfo->verticalActive = mode->VDisplay;
				dtInfo->verticalBlanking = mode->VTotal - mode->VDisplay;
				dtInfo->verticalSyncOffset = mode->VSyncStart - mode->VDisplay;
				dtInfo->verticalSyncPulseWidth = mode->VSyncEnd - mode->VSyncStart;
				dtInfo->verticalSyncConfig = (mode->Flags & V_NVSYNC)?0:1;
				dtInfo->numLinks = (pScrn->dualLink)?2:1;
				inst->refreshRates[i] = ((Fixed) mode->VRefresh) << 16;
				mode->modeID = inst->modeIDs[i];
				/*
				if ((mode->HDisplay == inst->fWidth) && (mode->VDisplay == inst->fHeight)) {
					inst->fMode = inst->modeIDs[i];
					inst->fRefreshRate = inst->refreshRates[i];
				} */
				
				i++;
				
				mode = mode->next;
			}
			inst->RHDReady = true;
		} while (false);
	}
	
	inst->setModel(pciDevice);
    return (inst);
}

void NDRVHD::free( void )
{
	if (xf86Screens[0]) {
		RadeonHDFreeScrn(xf86Screens[0]);
		xf86Screens[0] = NULL;
	}
#ifndef USEIOLOG && defined DEBUG	
	xf86Msg.mMsgBufferEnabled = false;
	if (xf86Msg.mMsgBuffer) {
		IOFree(xf86Msg.mMsgBuffer, xf86Msg.mMsgBufferSize);
		xf86Msg.mMsgBuffer = NULL;
	}
	if (xf86Msg.mMessageLock) {
		IOLockLock(xf86Msg.mMessageLock);
		IOLockFree(xf86Msg.mMessageLock);
		xf86Msg.mMessageLock = NULL;
	}
#endif
	if (memoryMap.BIOSCopy) {
		xfree(memoryMap.BIOSCopy);
		memoryMap.BIOSCopy = NULL;
	}
	if (memoryMap.EDID_Block) {
		xfree(memoryMap.EDID_Block);
		memoryMap.EDID_Block = NULL;
	}
	if (modeTimings) IODelete(modeTimings, IODetailedTimingInformationV2, modeCount);
	if (modeIDs) IODelete(modeIDs, IODisplayModeID, modeCount);
	if (refreshRates) IODelete(refreshRates, Fixed, modeCount);
	if (IOMap) IOMap->release();
	if (FBMap) FBMap->release();
    super::free();
}

IOReturn NDRVHD::getSymbol( const char * symbolName,
						   IOLogicalAddress * address )
{
    return (kIOReturnUnsupported);
}

const char * NDRVHD::driverName( void )
{
    return ("RadeonHD");
}

IOReturn NDRVHD::doDriverIO( UInt32 commandID, void * contents,
							UInt32 commandCode, UInt32 commandKind )
{
    IONDRVControlParameters * pb = (IONDRVControlParameters *) contents;
    IOReturn	ret;
	
    switch (commandCode)
    {
        case kIONDRVInitializeCommand:
			ret = kIOReturnSuccess;
			break;
        case kIONDRVOpenCommand:
			if (!debugMode && RHDReady) {
				VDSwitchInfoRec info;
				if (startMode)
					info.csData = startMode->modeID;
				else return kIOReturnSuccess;
				info.csMode = 32;
				doControl(cscSwitchMode, &info);
			}
            ret = kIOReturnSuccess;
            break;
			
        case kIONDRVControlCommand:
            ret = doControl( pb->code, pb->params );
            break;
        case kIONDRVStatusCommand:
            ret = doStatus( pb->code, pb->params );
            break;
			
		case kIONDRVCloseCommand:
		case kIONDRVFinalizeCommand:
        default:
            ret = kIOReturnUnsupported;
            break;
    }
	
    return (ret);
}

static UInt32 getPitch(UInt32 width, UInt32 bytesPerPixel) {
	if (bytesPerPixel == 0) bytesPerPixel = 1;
	return ((width * bytesPerPixel + 0xFF) & (~ 0xFF));
}

IOReturn NDRVHD::doControl( UInt32 code, void * params )
{
    IOReturn		ret = kIOReturnUnsupported;
	
    switch (code)
    {
        case cscSetEntries:
        case cscSetGamma:
            ret = kIOReturnSuccess;
            break;
			// to do list
		case cscDoCommunication:
		case cscGrayPage:
		case cscSetGray:
		case cscSetClutBehavior:
		case cscProbeConnection:
		case cscSetHardwareCursor:
		case cscDrawHardwareCursor:
		case cscSetDetailedTiming:
		case cscSetScaler:
			break;
		case cscSwitchMode:
		if (!debugMode && RHDReady)	
		{
			VDSwitchInfoRec *info = (VDSwitchInfoRec *)params;
			UInt32 i;
			for (i = 0;i < modeCount;i++) if (info->csData == modeIDs[i]) break;
			if (i == modeCount) break;
			UInt32 newWidth, newHeight, newRowBytes;
			if (fMode != modeIDs[i]) {
				if (modeTimings[i].horizontalScaled && modeTimings[i].verticalScaled) {
					newWidth = modeTimings[i].horizontalScaled;
					newHeight = modeTimings[i].verticalScaled;
				} else {
					newWidth = modeTimings[i].horizontalActive;
					newHeight = modeTimings[i].verticalActive;
				}
				newRowBytes = getPitch(newWidth, fBitsPerPixel / 8);
				ScrnInfoPtr pScrn = xf86Screens[0];
				pScrn->displayWidth = newRowBytes * 8 / fBitsPerPixel;
				pScrn->virtualX = newWidth;
				pScrn->virtualY = newHeight;
				if (RadeonHDSetMode(pScrn, info->csData, info->csMode)) {
					fWidth = newWidth;
					fHeight = newHeight;
					fRowBytes = newRowBytes;
					fMode = modeIDs[i];
					fRefreshRate = refreshRates[i];
					fAddress = (Ptr) (pScrn->memPhysBase + pScrn->fbOffset);
				}
			}
			ret = kIOReturnSuccess;
		}	
			break;
		case cscSetBackLightLevel:
		{
			if (RHDReady && RadeonHDGetSetBKSV((UInt32 *)params, 1)) ret = kIOReturnSuccess;
		}
			break;
		case cscSetMode:
		case cscSavePreferredConfiguration:
		case cscSetMirror:
		case cscSetFeatureConfiguration:
		case cscSetSync:
		case cscSetPowerState:
        default:
            break;
    }
	
    return (ret);
}

IOReturn NDRVHD::doStatus( UInt32 code, void * params )
{
 	IOReturn ret = kIOReturnUnsupported;
	IODetailedTimingInformationV2 *dtInfo;
	
    switch (code)
    {
        case cscGetCurMode:
		{
			VDSwitchInfoRec * info = (VDSwitchInfoRec *) params;
			
			//info->csData     = kIOBootNDRVDisplayMode;
			info->csData     = fMode;
			info->csMode     = kDepthMode1;
			info->csPage     = 1;
			info->csBaseAddr = (Ptr) (1 | (unsigned long) fAddress);
			ret = kIOReturnSuccess;
		}
            break;
			
        case cscGetNextResolution:
		{
			VDResolutionInfoRec * info = (VDResolutionInfoRec *) params;
			
			if (kDisplayModeIDFindFirstResolution == (SInt32) info->csPreviousDisplayModeID)
			{
				if (RHDReady && modeTimings && modeIDs)
				{
					dtInfo = modeTimings;
					info->csDisplayModeID 	= modeIDs[0];
					info->csMaxDepthMode		= kDepthMode1;
					if (dtInfo->horizontalScaled && dtInfo->verticalScaled) {
						info->csHorizontalPixels	= dtInfo->horizontalScaled;
						info->csVerticalLines	= dtInfo->verticalScaled;
					} else {
						info->csHorizontalPixels	= dtInfo->horizontalActive;
						info->csVerticalLines	= dtInfo->verticalActive;
					}
					info->csRefreshRate		= refreshRates[0];
				}
				else
				{
					info->csDisplayModeID 	= kIOBootNDRVDisplayMode;
					info->csMaxDepthMode		= kDepthMode1;
					info->csHorizontalPixels	= fWidth;
					info->csVerticalLines	= fHeight;
					info->csRefreshRate		= fRefreshRate;
				}
				ret = kIOReturnSuccess;
			}
			else if (kDisplayModeIDFindFirstProgrammable == (SInt32) info->csPreviousDisplayModeID)
			{
				ret = kIOReturnUnsupported;	//not clear yet
			}
			else if (kDisplayModeIDCurrent == (SInt32) info->csPreviousDisplayModeID)
			{
				info->csDisplayModeID 	= fMode;
				info->csMaxDepthMode		= kDepthMode1;
				info->csHorizontalPixels	= fWidth;
				info->csVerticalLines	= fHeight;
				info->csRefreshRate		= fRefreshRate;
				ret = kIOReturnSuccess;
			}
			else if (kIOBootNDRVDisplayMode == info->csPreviousDisplayModeID)
			{
				info->csDisplayModeID = kDisplayModeIDNoMoreResolutions;
				ret = kIOReturnSuccess;
			}
			else if (RHDReady)
			{
				UInt32 i;
				for (i = 0;i < modeCount;i++)
					if (modeIDs[i] == info->csPreviousDisplayModeID) break;
				if (i == modeCount) {
					info->csDisplayModeID = kDisplayModeIDInvalid;
					ret = kIOReturnBadArgument;
				} else if (i == (modeCount - 1)) {
					info->csDisplayModeID = kDisplayModeIDNoMoreResolutions;
					ret = kIOReturnSuccess;
				} else {
					i++;
					dtInfo						= &modeTimings[i];
					info->csDisplayModeID 	= modeIDs[i];
					info->csMaxDepthMode		= kDepthMode1;
					if (dtInfo->horizontalScaled && dtInfo->verticalScaled) {
						info->csHorizontalPixels	= dtInfo->horizontalScaled;
						info->csVerticalLines	= dtInfo->verticalScaled;
					} else {
						info->csHorizontalPixels	= dtInfo->horizontalActive;
						info->csVerticalLines	= dtInfo->verticalActive;
					}
					info->csRefreshRate		= refreshRates[i];
					ret = kIOReturnSuccess;
				}
			}
		}
            break;
			
        case cscGetVideoParameters:
		{
			VDVideoParametersInfoRec * pixelParams = (VDVideoParametersInfoRec *) params;
			VPBlock *	info = pixelParams->csVPBlockPtr;
			
			if (((kIOBootNDRVDisplayMode == pixelParams->csDisplayModeID)
				 && (kDepthMode1 == pixelParams->csDepthMode)) || (fMode == pixelParams->csDisplayModeID))
			{
				bzero(info, sizeof(VPBlock));
				info->vpBounds.right	= fWidth;
				info->vpBounds.bottom	= fHeight;
				info->vpRowBytes	= fRowBytes;
				info->vpPixelSize	= fBitsPerPixel;
				info->vpPixelType     = kIORGBDirectPixels;
				info->vpCmpCount      = 3;
				info->vpCmpSize       = (fBitsPerPixel <= 16) ? 5 : 8;
				ret = kIOReturnSuccess;
			}
			else if (RHDReady)
			{
				UInt32 i;
				for (i = 0;i < modeCount;i++)
					if (modeIDs[i] == pixelParams->csDisplayModeID) break;
				if (i < modeCount) {
					dtInfo = &modeTimings[i];
					bzero(info, sizeof(VPBlock));
					info->vpBounds.left	= 0;
					info->vpBounds.top	= 0;
					if (dtInfo->horizontalScaled && dtInfo->verticalScaled) {
						info->vpBounds.right	= dtInfo->horizontalScaled;
						info->vpBounds.bottom	= dtInfo->verticalScaled;
					} else {
						info->vpBounds.right	= dtInfo->horizontalActive;
						info->vpBounds.bottom	= dtInfo->verticalActive;
					}
					info->vpRowBytes	= getPitch(info->vpBounds.right, fBitsPerPixel / 8);
					info->vpPlaneBytes	= 0;
					info->vpPixelSize	= fBitsPerPixel;
					info->vpPixelType     = kIORGBDirectPixels;
					info->vpCmpCount      = 3;
					info->vpCmpSize       = (fBitsPerPixel <= 16) ? 5 : 8;
					ret = kIOReturnSuccess;
				}
			}
		}
            break;
			
        case cscGetModeTiming:
		{
			VDTimingInfoRec * info = (VDTimingInfoRec *) params;
			UInt32 i;
			
			if (kIOBootNDRVDisplayMode != info->csTimingMode)
			{
				for (i = 0;i < modeCount;i++)
					if (modeIDs[i] == info->csTimingMode) break;
				if (i == modeCount) {
					ret = kIOReturnBadArgument;
					break;
				}
			}
			if (kIOBootNDRVDisplayMode == info->csTimingMode)
				info->csTimingFormat = kDeclROMtables;
			else info->csTimingFormat = kDetailedTimingFormat;	//all handled by detailed timing
			info->csTimingFlags  = kDisplayModeValidFlag | kDisplayModeSafeFlag;
			ret = kIOReturnSuccess;
		}
            break;
			
			/* case cscGetDDCBlock:	//return EDID, here we use injected data
			 {
			 VDDDCBlockRec *	ddcRec = (VDDDCBlockRec *) params;
			 ret = kIOReturnBadArgument;
			 OSData * data = OSDynamicCast(OSData, (IORegistryEntry *) fNub->getProperty(kEDIDDATA));
			 if (data) {
			 Byte * block = (Byte *) data->getBytesNoCopy();
			 for (int i=0; i<EDID1_LEN; i++) ddcRec->ddcBlockData[i] = block[i];
			 ret = kIOReturnSuccess;
			 }
			 }
			 break; */
			
		case cscGetDetailedTiming:
			if (RHDReady)
		{
			VDDetailedTimingRec * info = (VDDetailedTimingRec *) params;
			UInt32 i;
			for (i = 0;i < modeCount;i++)
				if (modeIDs[i] == info->csDisplayModeID) break;
			if (i < modeCount) {
				dtInfo = &modeTimings[i];
				info->csPixelClock = dtInfo->pixelClock;
				info->csHorizontalActive = dtInfo->horizontalActive;
				info->csHorizontalBlanking = dtInfo->horizontalBlanking;
				info->csHorizontalSyncOffset = dtInfo->horizontalSyncOffset;
				info->csHorizontalSyncPulseWidth = dtInfo->horizontalSyncPulseWidth;
				info->csHorizontalSyncConfig = dtInfo->horizontalSyncConfig;
				info->csVerticalActive = dtInfo->verticalActive;
				info->csVerticalBlanking = dtInfo->verticalBlanking;
				info->csVerticalSyncOffset = dtInfo->verticalSyncOffset;
				info->csVerticalSyncPulseWidth = dtInfo->verticalSyncPulseWidth;
				info->csVerticalSyncConfig = dtInfo->verticalSyncConfig;
				//info->csNumLinks = dtInfo->numLinks;
				//info->csHorizontalBorderLeft/csHorizontalBorderRight/csVerticalBorderTop/csVerticalBorderBottom
				ret = kIOReturnSuccess;
			}
		}
			break;
			
		case cscGetCommunicationInfo:	//used for I2C, not clear yet
		{
			ret = kIOReturnUnsupported;
			VDCommunicationInfoRec * info = (VDCommunicationInfoRec *) params;
			if (kVideoDefaultBus == info->csBusID)	//return BusType, MinBus, MaxBus, SupportedTypes, SupportedCommFlags
			{
			}
			else	//return information for specific busID
			{
			}
		}
			break;
			
		case cscGetGamma:
			break;
		case cscGetScaler:
			if (RHDReady)
		{
			VDScalerRec * info = (VDScalerRec *) params;
			UInt32 i;
			for (i = 0;i < modeCount;i++)
				if (modeIDs[i] == info->csDisplayModeID) break;
			if (i < modeCount) {
				dtInfo = &modeTimings[i];
				if (dtInfo->horizontalScaled && dtInfo->verticalScaled) {
					info->csScalerFlags = dtInfo->scalerFlags;
					info->csHorizontalPixels = dtInfo->horizontalScaled;
					info->csVerticalPixels = dtInfo->verticalScaled;
					info->csHorizontalInset = dtInfo->horizontalScaledInset;
					info->csVerticalInset = dtInfo->verticalScaledInset;
					ret = kIOReturnSuccess;
				}
			}
		}
			break;
		case cscSupportsHardwareCursor:
		{
			VDSupportsHardwareCursorRec *info = (VDSupportsHardwareCursorRec *)params;
			if (options.HWCursorSupport) {
				info->csSupportsHardwareCursor = true;
				ret = kIOReturnSuccess;
			}
		}
			break;
		case cscGetBackLightLevel:
		{
			if (RHDReady && RadeonHDGetSetBKSV((UInt32 *)params, 0)) ret = kIOReturnSuccess;
		}
			break;
		case cscProbeConnection:
		case cscGetPreferredConfiguration:
		case cscGetMirror:
		case cscGetMultiConnect:
		case cscGetFeatureConfiguration:
		case cscGetSync:
		case cscGetConnection:
			//case cscGetFeatureList:	//defined in IONDRVFramebufferPrivate.h
		case cscGetPowerState:
			//case cscSleepWake:	//defined in IONDRVFramebufferPrivate.h
        default:
            break;
    }
	
    return (ret);
}

extern SymTabRec RHDModels[];
void NDRVHD::setModel(IORegistryEntry *device) {
	IOPCIDevice *pciDevice = OSDynamicCast(IOPCIDevice, device);
	if (!pciDevice) return;
	char *model = (char *)xf86TokenToString(RHDModels, pciDevice->configRead16(kIOPCIConfigDeviceID));
	if ((model == NULL) && (xf86Screens[0] != NULL)) model = xf86Screens[0]->chipset;
	if (model != NULL) pciDevice->setProperty("model", model);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#undef super
#define super IONDRVFramebuffer

OSDefineMetaClassAndStructors(RadeonHD, IONDRVFramebuffer)

IOReturn RadeonHD::doDriverIO( UInt32 commandID, void * contents,
							  UInt32 commandCode, UInt32 commandKind )
{
    IOReturn err;
	
    if (kIONDRVInitializeCommand == commandCode)
    {
        if (!ndrv)
        {
            ndrv = NDRVHD::fromRegistryEntry( nub, this );
            if (ndrv)
                setName( ndrv->driverName());
        }
    }
	
    if (ndrv)
    {
        OSIncrementAtomic( &ndrvEnter );
        err = ndrv->doDriverIO( commandID, contents, commandCode, commandKind );
        OSDecrementAtomic( &ndrvEnter );
    }
    else
        err = kIOReturnUnsupported;
	
    return (err);
}

IOReturn RadeonHD::setAttribute( IOSelect attribute, uintptr_t _value )
{
    IOReturn		err = kIOReturnSuccess;
    IONDRVControlParameters	pb;
	UInt32 value = _value;
	
    switch (attribute)
    {
        case 'bksv':
			pb.code = cscSetBackLightLevel;
			pb.params = &value;
			err = doDriverIO(1, &pb, kIONDRVControlCommand, kIONDRVImmediateIOCommandKind );
			break;
        default:
            err = super::setAttribute( attribute, _value );
    }
	
    return (err);
}

IOReturn RadeonHD::getAttribute( IOSelect attribute, uintptr_t * value )
{
    IOReturn			err = kIOReturnSuccess;
    IONDRVControlParameters	pb;
	
    switch (attribute)
    {
        case 'bksv':
			pb.code = cscGetBackLightLevel;
			pb.params = value;
			err = doDriverIO(1, &pb, kIONDRVStatusCommand, kIONDRVImmediateIOCommandKind );
			break;
        default:
            err = super::getAttribute( attribute, value );
    }
	
    return (err);
}


/*
static UInt32 cursorColorEncodings[2] = {0, 1};
static IOHardwareCursorDescriptor CURSORS[2] = {
	{kHardwareCursorDescriptorMajorVersion, kHardwareCursorDescriptorMinorVersion, 64, 64, 32, 0, 0, NULL, 0, kInvertingEncoding, 0,},
	{kHardwareCursorDescriptorMajorVersion, kHardwareCursorDescriptorMinorVersion, 64, 64, 2, 0, 2, cursorColorEncodings, 0, kInvertingEncoding | (1 << 2), 2, 3, 0,}
};

IOReturn RadeonHD::setCursorImage(void *cursorImage) {
	IOReturn ret;
	
	crtc.crsrInfo.crsrImageSet = false;
	crtc.crsrInfo.crsrBitDepth = 0;
	
	if (!(crtc.flags & crtcOnline)) return kIOReturnSuccess;
	IOHardwareCursorDescriptor aCrsrDscrpt;	//var_A8
	IOHardwareCursorInfo aCrsrInfo;	//var_44
	aCrsrInfo.majorVersion = 1;
	aCrsrInfo.minorVersion = 0;
	aCrsrInfo.colorMap = &crtc.crsrInfo.crsrColorMap;
	aCrsrInfo.hardwareCursorData = currentCrsrImage;
	int i;
	for (i = 0;i < Controller->crsrNum;i++) {
		memcpy(&aCrsrDscrpt, Controller->crsrDscrpt[i], sizeof(IOHardwareCursorDescriptor));
		ret = super::convertCursorImage(cursorImage, &aCrsrDscrpt, &aCrsrInfo);
		if (ret == kIOReturnSuccess) break;
	}
	if (i == Controller->crsrNum) return kIOReturnUnsupported;
	HWCrsrInfo.majorVersion = aCrsrInfo.majorVersion;
	HWCrsrInfo.minorVersion = aCrsrInfo.minorVersion;
	HWCrsrInfo.cursorHeight = aCrsrInfo.cursorHeight;
	HWCrsrInfo.cursorWidth = aCrsrInfo.cursorWidth;
	HWCrsrInfo.colorMap = aCrsrInfo.colorMap;
	HWCrsrInfo.hardwareCursorData = aCrsrInfo.hardwareCursorData;
	HWCrsrInfo.cursorHotSpotX = aCrsrInfo.cursorHotSpotX;
	HWCrsrInfo.cursorHotSpotY = aCrsrInfo.cursorHotSpotY;
	HWCrsrInfo.reserved[0] = aCrsrInfo.reserved[0];
	HWCrsrInfo.reserved[1] = aCrsrInfo.reserved[1];
	HWCrsrInfo.reserved[2] = aCrsrInfo.reserved[2];
	HWCrsrInfo.reserved[3] = aCrsrInfo.reserved[3];
	crtc.crsrInfo.crsrIndex = i;
	crtc.crsrInfo.crsrMode = Controller->getCursorMode(i);
	crtc.crsrInfo.crsrBitDepth = aCrsrDscrpt.bitDepth;
	ret = updateCursorImage();
	if (ret == kIOReturnSuccess) crtc.crsrInfo.crsrImageSet = true;
	else crtc.crsrInfo.crsrBitDepth = 0;
	
	return ret;
}

IOReturn RadeonHD::setCursorState( SInt32 x, SInt32 y, bool visible ) {
	crtc.crsrInfo.visible = false;
	Window aCrsrSize = Controller->getCursorSize();	//var_2A
	if ((crtc.crsrInfo.crsrBitDepth == 0) && visible) return kIOReturnError;
	if (!(crtc.flags & crtcOnline) || Controller->isInPowerPlay()) return kIOReturnSuccess;
	if (OSIncrementAtomic(&ndrvEnter) != 0) {	//serialize this call
		OSDecrementAtomic(&ndrvEnter);
		return kIOReturnBusy;
	}
	if ((crtc.flags & crtcSWScalerUsed)) {
		var_38 = crtc.scalerFlags;
		if (crtc.scalerFlags != kIOScaleRotate0) {
			if (crtc.scalerFlags & kIOScaleInvertX) x = crtc.fPixelInfo.activeWidth - x;
			if (crtc.scalerFlags & kIOScaleInvertY) y = crtc.fPixelInfo.activeHeight - y;
			if (crtc.scalerFlags & kIOScaleSwapAxes) swap(&x, &y);
		}
		x = crtc.softwareWindow.width * x / crtc.viewPort.width;
		y = crtc.softwareWindow.height * y / crtc.viewPort.height;
		if (crtc.scalerFlags != kIOScaleRotate0) {
			if (crtc.scalerFlags & kIOScaleInvertX) y = y - aCrsrSize.height;
			if (crtc.scalerFlags & kIOScaleInvertY) x = x - aCrsrSize.width;
		}
	}
	crtc.crsrInfo->x = x;
	crtc.crsrInfo->y = y;
	crtc.crsrInfo->visible = visible;
	notifySlaves(3, NULL);
	IOReturn ret = Controller->setCursorState(crtc.index, crtc.crsrInfo.crsrIndex, x, y, visible);
	OSDecrementAtomic(&ndrvEnter);
	return ret;
}

IOReturn RadeonHD::updateCursorState(CursorInfo  const *crsrInfo) {
	if (Controller->isInitialized()) {
		return Controller->setCursorState(crtc.index, crsrInfo->crsrIndex, crsrInfo->x, crsrInfo->y, crsrInfo->visible);
	} else {
		//Controller->getName(0);	//debug purpose
		return kIOReturnNotReady;
	}
}
*/
