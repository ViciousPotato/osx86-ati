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
#include "rhd/rhd.h"
#include "rhd/rhd_pm.h"

#define RHD_VBIOS_BASE 0xC0000
#define RHD_VBIOS_SIZE 0x10000

extern "C" Bool RadeonHDPreInit(ScrnInfoPtr pScrn, RegEntryIDPtr pciTag, RHDMemoryMap *pMemory, pciVideoPtr PciInfo, UserOptions *options);
extern "C" void RadeonHDFreeScrn(ScrnInfoPtr pScrn);
extern "C" Bool RadeonHDSetMode(ScrnInfoPtr pScrn, UInt32 modeID, UInt16 depth);
extern "C" Bool RadeonHDGetSetBKSV(UInt32 *value, Bool set);
extern "C" Bool RadeonHDDoCommunication(VDCommunicationRec *info);
extern "C" void HALGrayPage(UInt16 depth);
extern "C" void CreateLinearGamma(GammaTbl *gTable);
extern "C" void RadeonHDSetGamma(GammaTbl *gTable, GammaTbl *gTableNew);
extern "C" void RadeonHDSetEntries(GammaTbl *gTable, ColorSpec *cTable, SInt16 offset, UInt16 length);
extern "C" Bool RadeonHDSetHardwareCursor(void *cursorRef, GammaTbl *gTable);
extern "C" Bool RadeonHDDrawHardwareCursor(SInt32 x, SInt32 y, Bool visible);
extern "C" void RadeonHDGetHardwareCursorState(SInt32 *x, SInt32 *y, UInt32 *set, UInt32 *visible);
extern "C" Bool RadeonHDDisplayPowerManagementSet(int PowerManagementMode, int flags);
extern "C" int RadeonHDGetConnectionCount(void);
extern "C" rhdOutput* RadeonHDGetOutput(unsigned int index);
extern "C" Bool RadeonHDSave();
extern "C" Bool RadeonHDRestore();

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
	
	inst->fLastPowerState = kAVPowerOn;

	IOPCIDevice * pciDevice = OSDynamicCast(IOPCIDevice, regEntry);
	if (!pciDevice) return NULL;
	MAKE_REG_ENTRY(&inst->pciTag, pciDevice);

	//get user options
	OSBoolean *prop;
	OSNumber *optionNum;
	
	OSDictionary *dict = OSDynamicCast(OSDictionary, provider->getProperty("UserOptions"));
	
	bzero(&inst->options, sizeof(UserOptions));
	inst->options.HWCursorSupport = FALSE;
	inst->options.enableGammaTable = FALSE;
	inst->options.enableOSXI2C = FALSE;

	inst->debugMode = false;
	inst->options.BackLightLevel = 255;
	inst->options.lowPowerMode = FALSE;
	if (dict) {
		prop = OSDynamicCast(OSBoolean, dict->getObject("debugMode"));
		if (prop) inst->debugMode = prop->getValue();
		optionNum = OSDynamicCast(OSNumber, dict->getObject("BackLightLevel"));
		if (optionNum) inst->options.BackLightLevel = optionNum->unsigned32BitValue();
		prop = OSDynamicCast(OSBoolean, dict->getObject("enableHWCursor"));
		if (prop) inst->options.HWCursorSupport = prop->getValue();
		prop = OSDynamicCast(OSBoolean, dict->getObject("enableGammaTable"));
		if (prop) inst->options.enableGammaTable = prop->getValue();
		prop = OSDynamicCast(OSBoolean, dict->getObject("lowPowerMode"));
		if (prop) inst->options.lowPowerMode = prop->getValue();
	}
#ifndef USEIOLOG && defined DEBUG
	xf86Msg.mVerbose = 1;
	xf86Msg.mMsgBufferSize = 65535;
	if (dict) {
		optionNum = OSDynamicCast(OSNumber, dict->getObject("verboseLevel"));
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
		IOLog("error: couldn't allocate message buffer (%ld bytes)\n", xf86Msg.mMsgBufferSize);
		return false;
	}
	enableMsgBuffer(true);
#endif
	inst->memoryMap.EDID_Block = NULL;
	inst->memoryMap.EDID_Length = 0;
	if (dict) {
		OSData *edidData = OSDynamicCast(OSData, dict->getObject("EDID"));
		if (edidData) {
			inst->memoryMap.EDID_Block = (unsigned char *)IOMalloc(edidData->getLength());
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
		inst->memoryMap.BIOSCopy = (unsigned char *)IOMalloc(RHD_VBIOS_SIZE);
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
	
	switch (bpp) {
		case 8:
			inst->fBitsPerComponent = 8;
			inst->fDepth = kDepthMode1;
			break;
		case 16:
			inst->fBitsPerComponent = 5;
			inst->fDepth = kDepthMode2;
			break;
		case 32:
			inst->fBitsPerComponent = 8;
			inst->fDepth = kDepthMode3;
			break;
		default:
			inst->fBitsPerComponent = 0;
			inst->fDepth = kDepthMode1;
			break;
	}
	if (inst->debugMode) {	//in debugMode, VESA mode is used
		inst->fBitsPerComponent = 8;
		inst->fDepth = kDepthMode1;
	}
	if (inst->fAddress != (void *)inst->FBMap->getPhysicalAddress()) {
		LOG("Different FBPhyAddr detected: 0x%p (VESA), 0x%p\n", inst->fAddress, (void *)inst->FBMap->getPhysicalAddress());
		inst->fAddress = (void *)inst->FBMap->getPhysicalAddress();
	}
	inst->fMode = kIOBootNDRVDisplayMode;
	inst->fRefreshRate = 0 << 16;
	inst->memoryMap.FbPhysBase = (unsigned long)inst->fAddress;
	inst->memoryMap.bitsPerPixel = inst->fBitsPerPixel;
	inst->memoryMap.bitsPerComponent = inst->fBitsPerComponent;
	inst->memoryMap.colorFormat = 0;	//0 for non-64 bit
	
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
					inst->startMode = mode;	//using the vesa mode as start mode will avoid display of garbage
				
				LOG("%d X %d @ %dHz\n", mode->HDisplay, mode->VDisplay, (int) mode->VRefresh);
				
				if (!BigestMode || ((BigestMode->HDisplay < mode->HDisplay) && (BigestMode->VDisplay < mode->VDisplay)))
					BigestMode = mode;

				inst->modeCount++;
				mode = mode->next;
			}
			//if (!inst->startMode)	//use native mode better choice?
				inst->startMode = BigestMode;
			
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
				dtInfo->scalerFlags |= (mode->Flags & V_STRETCH)?kIOScaleStretchToFit:0;
				dtInfo->numLinks = (pScrn->dualLink)?2:1;
				inst->refreshRates[i] = ((Fixed) mode->VRefresh) << 16;
				mode->modeID = inst->modeIDs[i];
				
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
		IOFree(memoryMap.BIOSCopy, memoryMap.BIOSLength);
		memoryMap.BIOSCopy = NULL;
	}
	if (memoryMap.EDID_Block) {
		IOFree(memoryMap.EDID_Block, memoryMap.EDID_Length);
		memoryMap.EDID_Block = NULL;
	}
	if (modeTimings) IODelete(modeTimings, IODetailedTimingInformationV2, modeCount);
	if (modeIDs) IODelete(modeIDs, IODisplayModeID, modeCount);
	if (refreshRates) IODelete(refreshRates, Fixed, modeCount);
	if (IOMap) IOMap->release();
	if (FBMap) FBMap->release();
	
	if (gTable) IOFree(gTable, 0x60C);
	
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
/*	
	static UInt16 commandCodeCopy = 0;
	static UInt16 pbCodeCopy = 0;
	static UInt16 codeCount = 0;
	
	if ((commandCode == commandCodeCopy) && (pb->code == pbCodeCopy)) codeCount++;
	else {
		if (codeCount) LOG("cmd:%d->%d(%d)\n", commandCodeCopy, pbCodeCopy, codeCount + 1);
		else if (commandCodeCopy) LOG("cmd:%d->%d\n", commandCodeCopy, pbCodeCopy);
		commandCodeCopy = commandCode;
		pbCodeCopy = pb->code;
		codeCount = 0;
	}
*/	
    switch (commandCode)
    {
        case kIONDRVInitializeCommand:
			ret = kIOReturnSuccess;
			break;
        case kIONDRVOpenCommand:
			if (!debugMode && RHDReady) {
				//initialize gamma
				if (options.enableGammaTable) {
					gTable = (GammaTbl *)IOMalloc(0x60C);
					if (gTable) CreateLinearGamma(gTable);
				}
				//initialize mode
				VDSwitchInfoRec info;
				if (startMode)
					info.csData = startMode->modeID;
				else return kIOReturnSuccess;
				info.csMode = fDepth;
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

IOReturn NDRVHD::doControl( UInt32 code, void * params )
{
    IOReturn		ret = kIOReturnUnsupported;
	
    switch (code)
    {
        case cscSetEntries:
			if (RHDReady && options.enableGammaTable)
		{
			VDSetEntryRecord *info = (VDSetEntryRecord *)params;
			RadeonHDSetEntries(gTable, info->csTable, info->csStart, info->csCount + 1);
		}
			ret = kIOReturnSuccess;
			break;
        case cscSetGamma:
			if (RHDReady && options.enableGammaTable)
		{
			VDGammaRecord *info = (VDGammaRecord *)params;
			GammaTbl *gTableNew = (GammaTbl *)info->csGTable;
			RadeonHDSetGamma(gTable, gTableNew);
		}
			ret = kIOReturnSuccess;
            break;
		case cscDoCommunication:
			if (RHDReady && options.enableOSXI2C)
		{
			VDCommunicationRec *info = (VDCommunicationRec *) params;
			if (RadeonHDDoCommunication(info))
				ret = kIOReturnSuccess;
		}
			break;
		case cscGrayPage:
			if (RHDReady)
		{
			VDPageInfo *info = (VDPageInfo *) params;
			if (info->csPage != 0) break;
			HALGrayPage(fDepth);
			ret = kIOReturnSuccess;
		}
			break;
		case cscSetGray:
			LOG("cscSetGray\n");
			break;
		case cscSetClutBehavior:
			if (RHDReady && options.enableGammaTable)
		{
			VDClutBehavior *info = (VDClutBehavior *)params;
			if ((*info == kSetClutAtSetEntries) || (*info == kSetClutAtVBL))
				options.setCLUTAtSetEntries = (*info == kSetClutAtSetEntries);
				ret = kIOReturnSuccess;
		}
			break;
		case cscProbeConnection:
			break;
		case cscSetHardwareCursor:
			if (RHDReady && options.HWCursorSupport)
		{
			VDSetHardwareCursorRec *info = (VDSetHardwareCursorRec *)params;
			if (RadeonHDSetHardwareCursor(info->csCursorRef, gTable))
				ret = kIOReturnSuccess;
		}
			break;
		case cscDrawHardwareCursor:
			if (RHDReady && options.HWCursorSupport)
		{
			VDHardwareCursorDrawStateRec *info = (VDHardwareCursorDrawStateRec *) params;
			if (RadeonHDDrawHardwareCursor(info->csCursorX, info->csCursorY, info->csCursorVisible))
				ret = kIOReturnSuccess;
		}
			break;
		case cscSetDetailedTiming:
		case cscSetScaler:
			break;
		case cscSwitchMode:
		if (!debugMode && RHDReady)	
		{
			VDSwitchInfoRec *info = (VDSwitchInfoRec *)params;
			
			if ((info->csMode < kDepthMode1) || (info->csMode > kDepthMode6)) break;
			
			UInt32 i;
			for (i = 0;i < modeCount;i++) if (info->csData == modeIDs[i]) break;
			if (i == modeCount) break;
			
			UInt32 newWidth, newHeight, newRowBytes, newBitsPerPixel, newBitsPerComponent;
			if ((fMode != modeIDs[i]) || (fDepth != info->csMode)) {
				if (modeTimings[i].horizontalScaled && modeTimings[i].verticalScaled) {
					newWidth = modeTimings[i].horizontalScaled;
					newHeight = modeTimings[i].verticalScaled;
				} else {
					newWidth = modeTimings[i].horizontalActive;
					newHeight = modeTimings[i].verticalActive;
				}
				newBitsPerPixel = HALPixelSize(info->csMode);
				newBitsPerComponent = HALColorBits(info->csMode);
				newRowBytes = getPitch(newWidth, newBitsPerPixel / 8);
				ScrnInfoPtr pScrn = xf86Screens[0];
				pScrn->bitsPerPixel = newBitsPerPixel;
				pScrn->depth = newBitsPerPixel;
				pScrn->bitsPerComponent = newBitsPerComponent;
				pScrn->displayWidth = newRowBytes * 8 / newBitsPerPixel;
				pScrn->virtualX = newWidth;
				pScrn->virtualY = newHeight;
				if (RadeonHDSetMode(pScrn, info->csData, info->csMode)) {
					fWidth = newWidth;
					fHeight = newHeight;
					fBitsPerPixel = newBitsPerPixel;
					fBitsPerComponent = newBitsPerComponent;
					fDepth = info->csMode;
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
			if (RHDReady && options.BackLightLevel)
		{
			if (RadeonHDGetSetBKSV((UInt32 *)params, 1)) ret = kIOReturnSuccess;
		}
			break;
		case cscSetPowerState:
			if (RHDReady) 
			{
				VDPowerStateRec* vdPowerState = (VDPowerStateRec*)params;
				switch (vdPowerState->powerState) {
					case kAVPowerOn:
						LOG("kAVPowerOn: Trying to select state DPMSModeOn\n");
						RadeonHDDisplayPowerManagementSet(DPMSModeOn, 0);
						fLastPowerState = kAVPowerOn;
						ret = kIOReturnSuccess;
						break;
					case kAVPowerOff:
						LOG("kAVPowerOff: Trying to select state kAVPowerOff\n");
						RadeonHDDisplayPowerManagementSet(DPMSModeOff, 0);
						fLastPowerState = kAVPowerOff;
						ret = kIOReturnSuccess;
						break;
					case kAVPowerStandby:
						LOG("kAVPowerStandby: Trying to select state DPMSModeStandby\n");
						RadeonHDDisplayPowerManagementSet(DPMSModeStandby, 0);
						fLastPowerState = kAVPowerStandby;
						ret = kIOReturnSuccess;
						break;
					case kAVPowerSuspend:
						LOG("kAVPowerSuspend: Trying to select state DPMSModeSuspend\n");
						RadeonHDDisplayPowerManagementSet(DPMSModeSuspend, 0);
						fLastPowerState = kAVPowerSuspend;
						ret = kIOReturnSuccess;
						break;
					case kHardwareSleep:
						LOG("HW SLEEP: Trying to select state DPMSModeOff\n");
						RadeonHDDisplayPowerManagementSet(DPMSModeOff, 0);
						RadeonHDSave();
						fLastPowerState = kHardwareSleep;
						ret = kIOReturnSuccess;
						break;
					case kHardwareWake:
						LOG("HW WAKE: Trying to select state DPMSModeOn\n");
						RadeonHDRestore();
						RadeonHDDisplayPowerManagementSet(DPMSModeOn, 0);
						fLastPowerState = kHardwareWake;
						ret = kIOReturnSuccess;
						break;
					default:
						LOG("PowerManagemet: unhandled event %u\n", (unsigned int)vdPowerState->powerState);
						break;
				}
			}
            break;
		case cscSetMode:
			LOG("cscSetMode\n");
			break;
		case cscSavePreferredConfiguration:
			LOG("cscSavePreferredConfiguration\n");
			break;
		case cscSetMirror:
			LOG("cscSetMirror\n");
			break;
		case cscSetFeatureConfiguration:
			LOG("cscSetFeatureConfiguration\n");
			break;
		case cscSetSync:
			LOG("cscSetSync\n");
			break;
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
		case cscGetMode:
		{
			VDSwitchInfoRec* info = (VDSwitchInfoRec*) params;
			info->csPage = 1;
			info->csMode = fDepth;
			info->csBaseAddr = (Ptr) (1 | (unsigned long) fAddress);
			ret = kIOReturnSuccess;
		}
			break;
			
		case cscGetPages:
		{
			VDPageInfo *info = (VDPageInfo *)params;
			info->csPage = 1;
			ret = kIOReturnSuccess;
		}
			break;
			
		case cscGetBaseAddr:
		{
			VDPageInfo *info = (VDPageInfo *)params;
			info->csBaseAddr = (Ptr) (1 | (unsigned long) fAddress);
			ret = kIOReturnSuccess;
		}
			break;
			
        case cscGetCurMode:
		{
			VDSwitchInfoRec * info = (VDSwitchInfoRec *) params;
			
			info->csData     = fMode;
			info->csMode     = fDepth;
			info->csPage     = 1;
			info->csBaseAddr = (Ptr) (1 | (unsigned long) fAddress);
			/*
			if (RHDReady) {
				info->csPage = 0;
				info->csBaseAddr = (Ptr) fAddress;
				info->csReserved = 0;
			} */
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
					info->csMaxDepthMode		= kDepthMode6;
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
				info->csMaxDepthMode		= kDepthMode6;
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
					info->csMaxDepthMode		= kDepthMode6;
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
			//LOG("cscGetVideoParameters: modeID %d, depth %d\n", pixelParams->csDisplayModeID, pixelParams->csDepthMode);
			
			IODisplayModeID modeID = pixelParams->csDisplayModeID;
			if (modeID == 0) modeID = fMode;
			
			if ((kIOBootNDRVDisplayMode == modeID) && (fDepth == pixelParams->csDepthMode))
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
					if (modeIDs[i] == modeID) break;
				if (i < modeCount) {
					dtInfo = &modeTimings[i];
					pixelParams->csPageCount = 1;
					bzero(info, sizeof(VPBlock));
					if (dtInfo->horizontalScaled && dtInfo->verticalScaled) {
						info->vpBounds.right	= dtInfo->horizontalScaled;
						info->vpBounds.bottom	= dtInfo->verticalScaled;
					} else {
						info->vpBounds.right	= dtInfo->horizontalActive;
						info->vpBounds.bottom	= dtInfo->verticalActive;
					}
					info->vpCmpSize		= HALColorBits(pixelParams->csDepthMode);
					info->vpPixelSize	= HALPixelSize(pixelParams->csDepthMode);
					info->vpRowBytes	= getPitch(info->vpBounds.right, info->vpPixelSize / 8);
					//info->vpHRes = 0x480055;
					//info->vpVRes = 0x480044;
					if (info->vpPixelSize == 8) {
						info->vpPixelType = kIOCLUTPixels;
						pixelParams->csDeviceType = kIOCLUTPixels;
						info->vpCmpCount      = 1;
						info->vpBounds.left = 0;
					} else {
						info->vpPixelType     = kIORGBDirectPixels;
						pixelParams->csDeviceType = kIORGBDirectPixels;
						info->vpCmpCount      = 3;
						//info->vpBounds.left = 2;
				}
					ret = kIOReturnSuccess;
				}
			}
		}
            break;
			
		case cscGetGammaInfoList:
			break;
			
        case cscGetModeTiming:
		{
			VDTimingInfoRec * info = (VDTimingInfoRec *) params;
			UInt32 i;
			
			info->csTimingFormat = kDeclROMtables;
			info->csTimingFlags  = kDisplayModeValidFlag | kDisplayModeSafeFlag;
			if (kIOBootNDRVDisplayMode != info->csTimingMode)
			{
				for (i = 0;i < modeCount;i++)
					if (modeIDs[i] == info->csTimingMode) break;
				if (i == modeCount) {
					ret = kIOReturnBadArgument;
					break;
				}
				info->csTimingFormat = kDetailedTimingFormat;	//all handled by detailed timing
				if (modeTimings[i].scalerFlags & kIOScaleStretchToFit)
					info->csTimingFlags |= (1 << kModeStretched);
			}
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
				//bzero(&info, info->csTimingSize);
				//info->csDisplayModeID = modeIDs[i];
				//info->csTimingSize = sizeof(VDDetailedTimingRec);
				info->csPixelClock = dtInfo->pixelClock;
				info->csMinPixelClock = info->csPixelClock;
				info->csMaxPixelClock = info->csPixelClock;
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
				ret = kIOReturnSuccess;
			}
		}
			break;
			
		case cscGetCommunicationInfo:	//code reverse enigeered from 10.5 driver
			if (RHDReady && options.enableOSXI2C)
		{
			VDCommunicationInfoRec * info = (VDCommunicationInfoRec *) params;
			info->csBusID = 0;
			info->csBusType = 1;
			info->csMinBus = 0;
			info->csMaxBus = 0;
			info->csSupportedTypes = kVideoNoTransactionTypeMask | kVideoSimpleI2CTypeMask | kVideoDDCciReplyTypeMask | kVideoCombinedI2CTypeMask;
			info->csSupportedCommFlags = kVideoUsageAddrSubAddrMask;
			ret = kIOReturnSuccess;
		}
			break;
			
		case cscGetGamma:
			if (RHDReady && options.enableGammaTable)
		{
			VDGammaRecord *info = (VDGammaRecord *)params;
			if (gTable) {
				info->csGTable = (Ptr)gTable;
				ret = kIOReturnSuccess;
			}
		}
			break;
		case cscGetScalerInfo:
			LOG("cscGetScalerInfo\n");
			/*
			if (RHDReady)
		{
			VDScalerInfoRec *info = (VDScalerInfoRec *)params;
			info->csScalerFeatures = kScaleCanSupportInsetMask | kScaleCanScaleInterlacedMask | kScaleCanUpSamplePixelsMask | kScaleCanDownSamplePixelsMask;
			//info->csScalerFeatures |= kScaleCanRotateMask;
			info->csMaxHorizontalPixels = 4096;
			info->csMaxVerticalPixels = 4096;
			info->csReserved1 = 0;
			info->csReserved2 = 0;
			info->csReserved3 = 0;
			ret = kIOReturnSuccess;
		} */
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
				info->csScalerFlags = dtInfo->scalerFlags;
				info->csHorizontalInset = dtInfo->horizontalScaledInset;
				info->csVerticalInset = dtInfo->verticalScaledInset;
				info->csHorizontalPixels = dtInfo->horizontalActive;
				info->csVerticalPixels = dtInfo->verticalActive;
				if (dtInfo->horizontalScaled && dtInfo->verticalScaled) {
					info->csHorizontalPixels = dtInfo->horizontalScaled;
					info->csVerticalPixels = dtInfo->verticalScaled;
				}
				ret = kIOReturnSuccess;
			}
		}
			break;
		case cscSupportsHardwareCursor:
			if (RHDReady && options.HWCursorSupport)
		{
			VDSupportsHardwareCursorRec *info = (VDSupportsHardwareCursorRec *)params;
			info->csSupportsHardwareCursor = true;
			ret = kIOReturnSuccess;
		}
			break;
		case cscGetHardwareCursorDrawState:
			if (RHDReady && options.HWCursorSupport)
		{
			VDHardwareCursorDrawStateRec *info = (VDHardwareCursorDrawStateRec *)params;
			RadeonHDGetHardwareCursorState(&info->csCursorX, &info->csCursorY, &info->csCursorSet, &info->csCursorVisible);
			ret = kIOReturnSuccess;
		}
			break;
		case cscGetBackLightLevel:
			if (RHDReady && options.BackLightLevel)
		{
			if (RadeonHDGetSetBKSV((UInt32 *)params, 0)) ret = kIOReturnSuccess;
		}
			break;
			
		case cscGetPowerState:
			if (RHDReady)
			{
				VDPowerStateRec* vdPowerState = (VDPowerStateRec*)params;
				vdPowerState->powerState = fLastPowerState;
				vdPowerState->powerFlags = kPowerStateSleepCanPowerOffMask;
				ret = kIOReturnSuccess;
			}
			break;
		case cscGetMultiConnect:
			LOG("cscGetMultiConnect...\n");
			if (RHDReady) {
				VDMultiConnectInfoPtr vdMultiConnectInfo = (VDMultiConnectInfoPtr)params;
				switch (vdMultiConnectInfo->csDisplayCountOrNumber) {
					case kGetConnectionCount:
						LOG("cscGetMultiConnect query for connections count\n");
						vdMultiConnectInfo->csDisplayCountOrNumber = RadeonHDGetConnectionCount();
						LOG("cscGetMultiConnect query for connections count returning %u\n", (unsigned int)vdMultiConnectInfo->csDisplayCountOrNumber);
						ret = kIOReturnSuccess;
						break;
					default:
						//TODO: return connectin info for connection at csDisplayCountOrNumber position
						break;
				}
			}
			break;
		case cscGetConnection:
			LOG("cscGetConnection...\n");
			if (RHDReady) {
				LOG("cscGetConnection query for connection 0\n");
				VDDisplayConnectInfoPtr vdDisplayConnectInfo = (VDDisplayConnectInfoPtr)params;
				//struct rhdOutput* output = RadeonHDGetOutput(0);
				vdDisplayConnectInfo->csDisplayType = kUnknownConnect; //Really??
				vdDisplayConnectInfo->csConnectFlags = kAllModesSafe; //Really??
				ret = kIOReturnSuccess;
			}
			break;
		case cscProbeConnection:
			LOG("cscProbeConnection\n");
			break;
		case cscGetPreferredConfiguration:
			LOG("cscGetPreferredConfiguration\n");
			break;
		case cscGetMirror:
		{
			LOG("cscGetMirror\n");
			VDMirrorRec* vdMirror = (VDMirrorRec*)params;
			vdMirror->csMirrorFeatures = kMirrorSameDepthOnlyMirrorMask;
			vdMirror->csMirrorSupportedFlags = kMirrorCanMirrorMask;
			vdMirror->csMirrorFlags = kMirrorCanMirrorMask;
			ret = kIOReturnSuccess;
		}
			break;
		case cscGetFeatureConfiguration:
			LOG("cscGetFeatureConfiguration\n");
			break;
		case cscGetSync:
			LOG("cscGetSync\n");
			break;
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
		case kIOMirrorAttribute:
			LOG("kIOMirrorAttribute requested\n");
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

IOReturn RadeonHD::setAttributeForConnection( IOIndex connectIndex, IOSelect attribute, uintptr_t value ) {
	IOReturn ret = kIOReturnUnsupported;
	
	switch (attribute) {
			/*
		case kConnectionFlags:
			crtc.displayConnectFlags |= value;
			theProvider->setProperty("display-connect-flags", &crtc.displayConnectFlags, 4);
			ret = kIOReturnSuccess;
			break;
		case 'tbri':
		case 'tsat':
		case kConnectionVideoBest:
		case 'wasr':
		case 'thue':
		case kConnectionOverscan:
			ret = IOFramebuffer::setAttributeForConnection(connectIndex, attribute, value);
			//if (ret == kIOReturnUnsupported) ret = setTvParameters(attribute, value);
			break;
		case kConnectionPower:
			ret = kIOReturnSuccess;
			break;
		case kConnectionProbe:
			//ret = probeAction(value);
			break;
		case kConnectionSyncEnable:
			if (Connector != NULL) {
				Connector->getActiveConnection()->setSync(value & 0xFF);
				ret = kIOReturnSuccess;
			}
			break;
		case 'auw ':
		case 'aur ':
			//ret = setHdcpAttribute(attribute, value);
			break;
		case 228:
			//ret = setPrivateAttribute(value);
			break;
		case 227:
			//ret = setDisplayParameters((ATIDisplayParameters *)value);
			break; */
		case 'bklt':	//IOKit/graphics/IOGraphicsTypesPrivate.h
		{
			IONDRVControlParameters pb;
			pb.code = cscSetBackLightLevel;
			pb.params = &value;
			ret = doDriverIO(1, &pb, kIONDRVControlCommand, kIONDRVImmediateIOCommandKind);
		}
			break;
			/*
		case 'mvn ':
			if ((value <= 3) && (value != crtc.u4)) {
				crtc.u4 = value;
				Controller->setMacrovisionMode(&crtc);
			}
			break;
		case 'dith':
			if (aConnection != NULL) {
				UInt32 data = 0;
				ret = aConnection->getAttribute('dith', &data);
				if ((ret == kIOReturnSuccess) && (data != value)) ret = aConnection->setAttribute('dith', value);
			}
			break;
		case kConnectionHandleDisplayPortEvent:
			if ((value != 0) && (value != 3) && (aConnection != NULL) && aConnection->isDisplayPort()) {
				DisplayPortUtilities *dp = Connector->getDpServices();
				if (dp->isLinkTrained()) {
					ret = kIOReturnSuccess;
					break;
				}
				ret = dp->trainLink();
			}
			break; */
		default:
			break;
	}
	if (ret != kIOReturnSuccess) ret = super::setAttributeForConnection(connectIndex, attribute, value);
	return ret;
}

IOReturn RadeonHD::getAttributeForConnection( IOIndex connectIndex,
											 IOSelect attribute, uintptr_t  * value ) {
	IOReturn ret = kIOReturnUnsupported;
	switch (attribute) {
			/*
		case 'enab':
			if (value != NULL) {
				if ((crtc.flags & crtcOnline) && (Connector != NULL) && Connector->isConnected()) *value = 1;
				else *value = 0;
			}
			ret = kIOReturnSuccess;
			break;
		case 'aums':
		case 'auph':
		case 'aupp':
		case 'aupc':
		case 'auw ':
		case 'aur ':
			//ret = getHdcpAttribute(attribute, *value);
			break; 
		case kConnectionFlags:
			if (value != NULL) {
				
				if (Connector != NULL) {
					UInt32 var_38[2];
					Connector->getSenseInfo(var_38);
					*value = var_38[1];
				} else *value = 0;
			}
			ret = kIOReturnSuccess;
			break;
			 */
		case 'bklt':
		{
			UInt32 bklt = 0;
			IONDRVControlParameters pb;
			pb.code = cscGetBackLightLevel;
			pb.params = &bklt;
				ret = doDriverIO(1, &pb, kIONDRVStatusCommand, kIONDRVImmediateIOCommandKind);
				if ((value != NULL) && (ret == kIOReturnSuccess)) {
					*value = bklt;		//current
					*(value + 4) = 30;	//minimus?
					*(value + 8) = 255;	//maximum?
				}
		}
			break;
			/*
		case 'dith':
		{
			UInt32 data = 0;
			ret = ndrv->getAttribute('dith', &data);
			if ((value != NULL) && (ret == kIOReturnSuccess)) {
				*value = data;
				*(value + 4) = 0;
				*(value + 8) = 1;
			}
		}
			break;
		case 'mvn ':
			
			*value = crtc.u4;
			*(value + 4) = 0;
			*(value + 8) = 3;
			ret = kIOReturnUnsupported;
			break;
		case 227:
			//ret = getDisplayParameters((ATIDisplayParameters *)value);
			break;
		case 228:
			//ret = getPrivateAttribute(value);
			break;
		case 'asns':
		case 'hddc':
		case 'lddc':
		case 'pwak':
			ret = kIOReturnSuccess;
			break;
		case 'sycf':
			if (value != NULL)
				*value = ndrv->getSync();
			ret = kIOReturnSuccess;
			break;
		case 'thue':
		case kConnectionVideoBest:
		case 'wasr':
		case 'tbri':
		case 'tsat':
		case kConnectionOverscan:
			ret = IOFramebuffer::getAttributeForConnection(connectIndex, attribute, value);
			//if (ret == kIOReturnUnsupported) ret = getTvParameters(attribute, value);
			break;
		case 'thrm':
			ret = Controller->readAsicTemperature(&var_20, &var_24, &var_28);
			*value = var_20;
			*(value + 4) = var_24;
			*(value + 8) = var_28;
			break;
		case kConnectionSyncEnable:
			if (value != NULL) *value = 0x87;
			ret = kIOReturnSuccess;
			break;
		case kConnectionFlags:
			if (value != NULL) {
				if (Connector != NULL) {
					UInt32 var_38[2];
					Connector->getSenseInfo(var_38);
					*value = var_38[1];
				} else *value = 0;
			}
			ret = kIOReturnSuccess;
			break;
		case 'pcnt':
			ret = IOFramebuffer::getAttributeForConnection(connectIndex, 'pcnt', value);
			if (ret != kIOReturnSuccess) *value = 9;
			else *value += 9;
			ret = kIOReturnSuccess;
			break;
		case 'parm':
		{
			UInt32  featureList[9] = {'htid', 'sgd ', 'nvmn', 'csot', 'sbvr', 'sawt', 'lkbm', 'rht\0', '\0\0\0\0'};
			bcopy(featureList, value, 36);
			IOFramebuffer::getAttributeForConnection(connectIndex, 'parm', (value + 36));
			ret = kIOReturnSuccess;
		}
			break; */
		default:
			break;
	}
	if (ret != kIOReturnSuccess) ret = super::getAttributeForConnection(connectIndex, attribute, value);
	return ret;
}
