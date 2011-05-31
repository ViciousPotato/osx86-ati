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
#include <IOKit/IODeviceTreeSupport.h>
#include "RadeonController.h"
#include "RadeonHD.h"
#include "rhd/rhd.h"
#include "rhd/rhd_crtc.h"
#include "rhd/rhd_pm.h"
#include "OS_Version.h"

extern "C" Bool RadeonHDPreInit(ScrnInfoPtr pScrn);
extern "C" void RadeonHDFreeScrn(ScrnInfoPtr pScrn);
extern "C" Bool RadeonHDSetMode(ScrnInfoPtr pScrn, UInt16 depth);
extern "C" Bool RadeonHDDoCommunication(VDCommunicationRec *info);
extern "C" unsigned char * RHDGetEDIDRawData(int index);
extern "C" void HALGrayPage(UInt16 depth, int index);
extern "C" void CreateLinearGamma(GammaTbl *gTable);
extern "C" void RadeonHDSetGamma(GammaTbl *gTable, GammaTbl *gTableNew);
extern "C" void RadeonHDSetEntries(GammaTbl *gTable, ColorSpec *cTable, SInt16 offset, UInt16 length);
extern "C" Bool RadeonHDSetHardwareCursor(void *cursorRef, GammaTbl *gTable);
extern "C" Bool RadeonHDDrawHardwareCursor(SInt32 x, SInt32 y, Bool visible, int index);
extern "C" void RadeonHDGetHardwareCursorState(SInt32 *x, SInt32 *y, UInt32 *set, UInt32 *visible, int index);
extern "C" Bool RadeonHDDisplayPowerManagementSet(int PowerManagementMode, int flags);
extern "C" int RadeonHDGetConnectionCount(void);
extern "C" rhdOutput* RadeonHDGetOutput(unsigned int index);
extern "C" Bool RadeonHDSave();
extern "C" Bool RadeonHDRestore();
extern "C" Bool RHDIsInternalDisplay(int index);
extern "C" Bool RadeonHDSetMirror(Bool value);

#ifdef MACOSX_10_5
enum {
    kIONDRVOpenCommand                = 128 + 0,
    kIONDRVCloseCommand               = 128 + 1,
    kIONDRVReadCommand                = 128 + 2,
    kIONDRVWriteCommand               = 128 + 3,
    kIONDRVControlCommand             = 128 + 4,
    kIONDRVStatusCommand              = 128 + 5,
    kIONDRVKillIOCommand              = 128 + 6,
    kIONDRVInitializeCommand          = 128 + 7,		/* init driver and device*/
    kIONDRVFinalizeCommand            = 128 + 8,		/* shutdown driver and device*/
    kIONDRVReplaceCommand             = 128 + 9,		/* replace an old driver*/
    kIONDRVSupersededCommand          = 128 + 10		/* prepare to be replaced by a new driver*/
};
enum {
    kIONDRVSynchronousIOCommandKind   = 0x00000001,
    kIONDRVAsynchronousIOCommandKind  = 0x00000002,
    kIONDRVImmediateIOCommandKind     = 0x00000004
};

struct IONDRVControlParameters {
    UInt8	__reservedA[0x1a];
    UInt16	code;
    void *	params;
    UInt8	__reservedB[0x12];
};

enum {
    kConnectionCheckEnable              = 'cena',
};

#endif

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

static RadeonController * findController(IOService *provider) {
	RadeonController * ret = NULL;
	IOService * item;
	OSIterator * iter = provider->getProvider()->getClientIterator();
	if (iter == NULL) return NULL;
	do {
		item = OSDynamicCast(IOService, iter->getNextObject());
		if (item == NULL) break;
		ret = OSDynamicCast(RadeonController, item);
		if (ret != NULL) break;
	} while (true);
	iter->release();
	return ret;
}

#define kHardwareReady "hardwareInitialized"
IONDRV * NDRVHD::fromRegistryEntry( IOService * nub )
{
    NDRVHD *		inst;
	
	inst = new NDRVHD;
	if (!inst || !inst->init()) return NULL;
	
	RadeonController * controller = findController(nub);
	if (controller == NULL) return NULL;
	inst->options = controller->getUserOptions();

	inst->nubIndex = 0;
	OSNumber * num = OSDynamicCast(OSNumber, nub->getProperty(kIOFBDependentIndexKey));
	if (num) inst->nubIndex = num->unsigned32BitValue();
	LOG("initialize NDRVHD for nub %d\n", inst->nubIndex);
	
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
	if (inst->options->debugMode) {
		inst->fBitsPerComponent = 8;
		inst->fDepth = kDepthMode1;
	}
	inst->fMode = kIOBootNDRVDisplayMode;
	inst->fRefreshRate = 0 << 16;
	
	// initialization of rhd structs
	inst->RHDReady = false;
	ScrnInfoPtr pScrn = xf86Screens[0];
	if (!controller->getProperty(kHardwareReady)) {
		LOG("initialize hardware with nub %d\n", inst->nubIndex);
		controller->setProperty(kHardwareReady, kOSBooleanTrue);
		if (!pScrn || !RadeonHDPreInit(pScrn)) controller->removeProperty(kHardwareReady);
	} else {
		// 2nd nub
		inst->fMode = 0x3000;
		inst->fRefreshRate = 0 << 16;
		inst->fDepth = kDepthMode3;
		inst->fWidth = 0;
		inst->fHeight = 0;
		LOG("nub %d default to offline mode\n", inst->nubIndex);
	}
	if (!controller->getProperty(kHardwareReady) || !pScrn) {
		LOG("nub %d can't initialize from hardware\n", inst->nubIndex);
		if (inst->options->debugMode) return inst;
		else return NULL;
	}
	
	struct rhdCrtc *Crtc = RHDPTR(pScrn)->Crtc[inst->nubIndex];
	DisplayModePtr mode;
	inst->modeCount = 0;
	mode = Crtc->Modes;
	LOG("Display resolutions detected for nub %d: \n", inst->nubIndex);
	while (mode)	//no longer a circular list, so won't be trapped here
	{
		LOG("%d X %d @ %dHz\n", mode->HDisplay, mode->VDisplay, (int) mode->VRefresh);
		
		inst->modeCount++;
		mode = mode->next;
	}
	
	if (inst->options->debugMode) return inst;
	
	//initialize mode structs
	do {
		//get current configuration
		if (!Crtc->CurrentMode) break;
		if (!inst->modeCount) break;
		inst->fWidth = Crtc->Width;
		inst->fHeight = Crtc->Height;
		inst->fBitsPerPixel = Crtc->bpp;
		inst->fBitsPerComponent = pScrn->bitsPerComponent;
		inst->fDepth = kDepthMode3;
		inst->fRowBytes = Crtc->Pitch;
		inst->fAddress = (Ptr) (Crtc->FBPhyAddress);
				
		inst->modeTimings = IONew(IODetailedTimingInformationV2, inst->modeCount);
		inst->modeIDs = IONew(IODisplayModeID, inst->modeCount);
		inst->refreshRates = IONew(Fixed, inst->modeCount);
		if (!inst->modeTimings || !inst->modeIDs || !inst->refreshRates) break;
		IODetailedTimingInformationV2 *dtInfo;
		unsigned int i;
		for (i = 0;i < inst->modeCount;i++) inst->modeIDs[i] = 1 + i;	//let's start with 1
		mode = Crtc->Modes;
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
			
			//modeID and refreshRade for current configuration
			if (mode == Crtc->CurrentMode) {
				inst->fMode = mode->modeID;
				inst->fRefreshRate = inst->refreshRates[i];
			}
			
			i++;
			
			mode = mode->next;
		}
		
		inst->fLastPowerState = kAVPowerOn;
		
		inst->RHDReady = true;
	} while (false);
	
    return (inst);
}

void NDRVHD::free( void )
{
	if (xf86Screens[0]) RadeonHDFreeScrn(xf86Screens[0]);
	if (modeTimings) IODelete(modeTimings, IODetailedTimingInformationV2, modeCount);
	if (modeIDs) IODelete(modeIDs, IODisplayModeID, modeCount);
	if (refreshRates) IODelete(refreshRates, Fixed, modeCount);
	
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

    switch (commandCode)
    {
        case kIONDRVInitializeCommand:
			ret = kIOReturnSuccess;
			break;
        case kIONDRVOpenCommand:
			if (!options->debugMode && RHDReady) {
				//initialize gamma
				if (options->enableGammaTable) {
					gTable = (GammaTbl *)IOMalloc(0x60C);
					if (gTable) CreateLinearGamma(gTable);
				}
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

#define REG_ENTRY_TO_OBJ_RET(regEntryID,obj,ret)                        \
if( (uintptr_t)((obj = ((IORegistryEntry **)regEntryID)[ 0 ]))  \
!= ~((uintptr_t *)regEntryID)[ 1 ] )                           \
return( ret);
IOReturn NDRVHD::doControl( UInt32 code, void * params )
{
    IOReturn		ret = kIOReturnUnsupported;
	
    switch (code)
    {
        case cscSetEntries:
			if (RHDReady && options->enableGammaTable)
		{
			VDSetEntryRecord *info = (VDSetEntryRecord *)params;
			RadeonHDSetEntries(gTable, info->csTable, info->csStart, info->csCount + 1);
		}
			ret = kIOReturnSuccess;
			break;
        case cscSetGamma:
			if (RHDReady && options->enableGammaTable)
		{
			VDGammaRecord *info = (VDGammaRecord *)params;
			GammaTbl *gTableNew = (GammaTbl *)info->csGTable;
			RadeonHDSetGamma(gTable, gTableNew);
		}
			ret = kIOReturnSuccess;
            break;
		case cscDoCommunication:
			if (RHDReady && options->enableOSXI2C)
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
			if (fMode == 0x3000) break;
			HALGrayPage(fDepth, nubIndex);
			ret = kIOReturnSuccess;
		}
			break;
		case cscSetGray:
			LOG("cscSetGray\n");
			break;
		case cscSetClutBehavior:
			if (RHDReady && options->enableGammaTable)
		{
			VDClutBehavior *info = (VDClutBehavior *)params;
			if ((*info == kSetClutAtSetEntries) || (*info == kSetClutAtVBL))
				options->setCLUTAtSetEntries = (*info == kSetClutAtSetEntries);
				ret = kIOReturnSuccess;
		}
			break;
		case cscProbeConnection:
			break;
		case cscSetHardwareCursor:
			if (RHDReady && options->HWCursorSupport)
		{
			VDSetHardwareCursorRec *info = (VDSetHardwareCursorRec *)params;
			if (RadeonHDSetHardwareCursor(info->csCursorRef, gTable))
				ret = kIOReturnSuccess;
		}
			break;
		case cscDrawHardwareCursor:
			if (RHDReady && options->HWCursorSupport)
		{
			VDHardwareCursorDrawStateRec *info = (VDHardwareCursorDrawStateRec *) params;
			if (RadeonHDDrawHardwareCursor(info->csCursorX, info->csCursorY, info->csCursorVisible, nubIndex))
				ret = kIOReturnSuccess;
		}
			break;
		case cscSetDetailedTiming:
		case cscSetScaler:
			break;
		case cscSwitchMode:
		if (!options->debugMode && RHDReady)	
		{
			VDSwitchInfoRec *info = (VDSwitchInfoRec *)params;
			
			if (info->csData == 0x3000) return kIOReturnSuccess;
			if ((info->csMode < kDepthMode1) || (info->csMode > kDepthMode6)) break;
			
			ScrnInfoPtr pScrn = xf86Screens[0];
			struct rhdCrtc *Crtc = RHDPTR(pScrn)->Crtc[nubIndex];
			DisplayModePtr mode = Crtc->Modes;
			while (mode) {
				if (mode->modeID == info->csData) break;
				mode = mode->next;
			}
			if (!mode) break;
			DisplayModePtr modeBackup = Crtc->CurrentMode;
			Crtc->CurrentMode = mode;
			pScrn->bitsPerPixel = HALPixelSize(info->csMode);
			pScrn->depth = pScrn->bitsPerPixel;
			pScrn->bitsPerComponent = HALColorBits(info->csMode);
			if (!RadeonHDSetMode(pScrn, info->csMode)) {
				Crtc->CurrentMode = modeBackup;
				pScrn->bitsPerPixel = fBitsPerPixel;
				pScrn->depth = pScrn->bitsPerPixel;
				pScrn->bitsPerComponent = fBitsPerComponent;
				break;
			}
			
			fWidth = Crtc->Width;
			fHeight = Crtc->Height;
			fBitsPerPixel = Crtc->bpp;
			fBitsPerComponent = pScrn->bitsPerComponent;
			fDepth = info->csMode;
			fRowBytes = Crtc->Pitch;
			fMode = info->csData;
			fRefreshRate = ((Fixed) mode->VRefresh) << 16;;
			fAddress = (Ptr) (Crtc->FBPhyAddress);

			ret = kIOReturnSuccess;
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
			LOG("cscSetMirror\n");/*
		{
			VDMirrorRec *mirror = (VDMirrorRec *)params;
			if (!mirror) break;
			IORegistryEntry *obj;
			REG_ENTRY_TO_OBJ_RET(((RegEntryID *)&mirror->csMirrorRequestID), obj, ret);
			if (RadeonHDSetMirror((obj == NULL)?FALSE:TRUE)) ret = kIOReturnSuccess;
		}*/
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
				if (fMode == 0x3000) {
					info->csDisplayModeID = fMode;
				}
				else if (RHDReady && modeTimings && modeIDs)
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
			VDTimingInfoRec * timingInfo = (VDTimingInfoRec *) params;
			if (options->debugMode) {
				{
					if (kIOBootNDRVDisplayMode != timingInfo->csTimingMode)
					{
						ret = kIOReturnBadArgument;
						break;
					}
					timingInfo->csTimingFormat = kDeclROMtables;
					timingInfo->csTimingFlags  = kDisplayModeValidFlag | kDisplayModeSafeFlag;
					ret = kIOReturnSuccess;
				}
				break;
			}
			
			UInt32 i;
			
			timingInfo->csTimingFormat = kDeclROMtables;
			timingInfo->csTimingFlags  = kDisplayModeValidFlag | kDisplayModeSafeFlag;
			if (modeTimings && modeIDs)
			{
				for (i = 0;i < modeCount;i++)
					if (modeIDs[i] == timingInfo->csTimingMode) break;
				if (i == modeCount) {
					ret = kIOReturnBadArgument;
					break;
				}
				timingInfo->csTimingFormat = kDetailedTimingFormat;	//all handled by detailed timing
				if (modeTimings[i].scalerFlags & kIOScaleStretchToFit)
					timingInfo->csTimingFlags |= (1 << kModeStretched);
			}
			ret = kIOReturnSuccess;
		}
            break;
			
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
			if (RHDReady && options->enableOSXI2C)
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
			if (RHDReady && options->enableGammaTable)
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
		{
			if (options->debugMode || fMode == 0x3000) {
				ret = kIOReturnUnsupported;
				break;
			}
			DisplayModePtr mode = RHDPTR(xf86Screens[0])->Crtc[nubIndex]->ScaledToMode;
			if (!mode) break;
			VDScalerRec *scaler = (VDScalerRec *) params;
			if (scaler->csDisplayModeID == fMode) {
				if (fMode != mode->modeID) scaler->csScalerFlags |= kIOScaleStretchToFit;
				scaler->csHorizontalPixels = fWidth;
				scaler->csVerticalPixels = fHeight;
				ret = kIOReturnSuccess;
			}
		}
			break;
		case cscSupportsHardwareCursor:
			if (RHDReady && options->HWCursorSupport)
		{
			VDSupportsHardwareCursorRec *info = (VDSupportsHardwareCursorRec *)params;
			info->csSupportsHardwareCursor = true;
			ret = kIOReturnSuccess;
		}
			break;
		case cscGetHardwareCursorDrawState:
			if (RHDReady && options->HWCursorSupport)
		{
			VDHardwareCursorDrawStateRec *info = (VDHardwareCursorDrawStateRec *)params;
			RadeonHDGetHardwareCursorState(&info->csCursorX, &info->csCursorY, &info->csCursorSet, &info->csCursorVisible, nubIndex);
			ret = kIOReturnSuccess;
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
			break;
		case cscGetConnection:
			LOG("cscGetConnection...\n");
			break;
		case cscProbeConnection:
			LOG("cscProbeConnection\n");
			break;
		case cscGetPreferredConfiguration:
			LOG("cscGetPreferredConfiguration\n");
			break;
		case cscGetMirror:
			LOG("cscGetMirror\n");
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
/*
bool NDRVHD::hasDDCConnect( void ) {
	if (fMode == 0x3000) return false;
	if (RHDGetEDIDRawData(nubIndex) == NULL) return false;
	return true;
}

UInt8 * NDRVHD::getDDCBlock( void ) {
	if (fMode == 0x3000) return NULL;
	return (UInt8 *)RHDGetEDIDRawData(nubIndex);
}

bool NDRVHD::isInternalDisplay( void ) {
	if (fMode == 0x3000) return false;
	return RHDIsInternalDisplay(nubIndex);
}
*/
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
            ndrv = NDRVHD::fromRegistryEntry( nub );
            if (ndrv) setName( ndrv->driverName());
			else LOG("NDRVHD failed to instance\n");
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
	
    switch (attribute)
    {
        default:
            err = super::setAttribute( attribute, _value );
    }
	
    return (err);
}

IOReturn RadeonHD::getAttribute( IOSelect attribute, uintptr_t * value )
{
    IOReturn			err = kIOReturnSuccess;
	
    switch (attribute)
    {/*
        case kIOMirrorAttribute:
			value[0] = 0x9F | kIOMirrorHWClipped;
			if (mirrorPrimary) value[0] |= kIOMirrorIsPrimary;
			err = kIOReturnSuccess;
            break;
		*/	
        default:
            err = super::getAttribute( attribute, value );
    }
	
    return (err);
}

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
			ret = super::setAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	return ret;
}

IOReturn RadeonHD::getAttributeForConnection( IOIndex connectIndex,
											 IOSelect attribute, uintptr_t  * value ) {
	IOReturn ret = kIOReturnUnsupported;
    IONDRVControlParameters     pb;

	switch (attribute) {
        case kConnectionCheckEnable:
		{
			VDSwitchInfoRec info;
			pb.code = cscGetCurMode;
			pb.params = &info;
			ret = doDriverIO( /*ID*/ 1, &pb, kIONDRVStatusCommand, kIONDRVImmediateIOCommandKind );
			if ((ret != kIOReturnSuccess) || (info.csData == 0x3000)) online = false;
			else online = true;
		}
            /* fall thru */
			
        case kConnectionEnable:
			
            *value = online;
            ret = kIOReturnSuccess;
            break;
		/*	
        case kConnectionFlags:
		{
			NDRVHD *dev = OSDynamicCast(NDRVHD, ndrv);
			if (dev) {
				if (dev->isInternalDisplay()) *value |= 1<<kBuiltInConnection;
				if (dev->hasDDCConnect()) *value |= 1<<kHasDDCConnection;
				VDSwitchInfoRec info;
				pb.code = cscGetCurMode;
				pb.params = &info;
				ret = doDriverIO( 1, &pb, kIONDRVStatusCommand, kIONDRVImmediateIOCommandKind );
				if ((ret != kIOReturnSuccess) || (info.csData == 0x3000)) *value |= 1<<kConnectionInactive;
				ret = kIOReturnSuccess;
			} else {
				*value = 0;
				ret = kIOReturnUnsupported;
			}
		}
			break;
			*/
			/*
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
			ret = super::getAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	return ret;
}

IOReturn RadeonHD::setDisplayMode( IODisplayModeID displayMode, IOIndex depth )
{
	if (!online || (displayMode == 0x3000)) return kIOReturnSuccess;
	return super::setDisplayMode(displayMode, depth);
}
/*
bool RadeonHD::hasDDCConnect( IOIndex  connectIndex )
{
	NDRVHD *dev = OSDynamicCast(NDRVHD, ndrv);
	if (dev) return dev->hasDDCConnect();
	return false;
}

IOReturn RadeonHD::getDDCBlock( IOIndex,	// connectIndex 
							   UInt32 blockNumber,
							   IOSelect blockType,
							   IOOptionBits options,
							   UInt8 * data, IOByteCount * length )

{
	UInt8 *rawData = NULL;
	
	NDRVHD *dev = OSDynamicCast(NDRVHD, ndrv);
	if (dev) rawData = dev->getDDCBlock();
	if (!rawData) return kIOReturnError;
	bcopy(rawData, data, 128);
	*length = 128;
	return kIOReturnSuccess;
}
*/

IOService * RadeonHD::probe( IOService * provider, SInt32 * score )
{
	return this;	//overwrite super method, otherwise won't get match because kIONDRVIgnoreKey is set
}

