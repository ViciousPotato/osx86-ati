/*
 * Copyright 2007-2009  Luc Verhaegen <libv@exsuse.de>
 * Copyright 2007-2009  Matthias Hopf <mhopf@novell.com>
 * Copyright 2007-2009  Egbert Eich   <eich@novell.com>
 * Copyright 2007-2009  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef ATOM_BIOS_PARSER
# define ATOM_ASIC_INIT
#endif

#include "xf86.h"

/* For HW cursor */
//#include "xf86Cursor.h"

/* mi colormap manipulation */
//#include "micmap.h"

//#include "xf86cmap.h"
/*
#ifdef HAVE_XEXTPROTO_71
#include "X11/extensions/dpmsconst.h"
#else
#define DPMS_SERVER
#include "X11/extensions/dpms.h"
#endif
*/

/* Needed for Device Data Channel (DDC) support */
#include "xf86DDC.h"

#include "rhd.h"
#include "rhd_regs.h"
#include "rhd_cursor.h"
#ifdef ATOM_BIOS
#include "rhd_atombios.h"
#endif
#include "rhd_connector.h"
#include "rhd_output.h"
#ifdef ATOM_BIOS
#include "rhd_biosscratch.h"
#endif
#include "rhd_pll.h"
#include "rhd_vga.h"
#include "rhd_mc.h"
#include "rhd_monitor.h"
#include "rhd_crtc.h"
#include "rhd_modes.h"
#include "rhd_lut.h"
#include "rhd_i2c.h"
#include "rhd_card.h"
//#include "rhd_audio.h"
#include "rhd_pm.h"

/* Mandatory functions */
static Bool     RHDPreInit(ScrnInfoPtr pScrn);
static Bool     RHDScreenInit(ScrnInfoPtr pScrn, DisplayModePtr mode);
static Bool     RHDEnterVT(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void     RHDLeaveVT(ScrnInfoPtr pScrn);
static Bool     RHDCloseScreen(int scrnIndex);
static void     RHDFreeScreen(int scrnIndex, int flags);
static Bool     RHDSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void     RHDAdjustFrame(ScrnInfoPtr pScrn, int x, int y);
static void     RHDDisplayPowerManagementSet(ScrnInfoPtr pScrn,
                                             int PowerManagementMode,
                                             int flags);
static void     RHDLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
                               LOCO *colors);
static Bool     RHDSaveScreen(ScrnInfoPtr pScrn, Bool unblank);

static void     rhdSave(RHDPtr rhdPtr);
static void     rhdRestore(RHDPtr rhdPtr);
static Bool     rhdModeLayoutSelect(RHDPtr rhdPtr);
static void     rhdModeLayoutPrint(RHDPtr rhdPtr);
static void     rhdModeDPISet(ScrnInfoPtr pScrn);
static Bool     rhdAllIdle(RHDPtr rhdPtr);
static void     rhdModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void	rhdSetMode(ScrnInfoPtr pScrn, DisplayModePtr mode);
static Bool     rhdMapFB(RHDPtr rhdPtr);
static void     rhdUnmapFB(RHDPtr rhdPtr);
static CARD32   rhdGetVideoRamSize(RHDPtr rhdPtr);
//static void	rhdGetIGPNorthBridgeInfo(RHDPtr rhdPtr);
static enum rhdCardType rhdGetCardType(RHDPtr rhdPtr);

/* rhd_id.c */
extern SymTabRec RHDChipsets[];
extern PciChipsets RHDPCIchipsets[];
extern void RHDIdentify(int flags);
extern struct rhdCard *RHDCardIdentify(ScrnInfoPtr pScrn);

static Bool
RHDGetRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate != NULL)
	return TRUE;

    pScrn->driverPrivate = (pointer)IOMalloc(sizeof(RHDRec));
    if (pScrn->driverPrivate == NULL) return FALSE;
	bzero(pScrn->driverPrivate, sizeof(RHDRec));

    RHDPTR(pScrn)->scrnIndex = pScrn->scrnIndex;

    return TRUE;
}

static void
RHDFreeRec(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr;

    if (pScrn->driverPrivate == NULL)
	return;

    rhdPtr = RHDPTR(pScrn);

    RHDMCDestroy(rhdPtr);
    RHDVGADestroy(rhdPtr);
    RHDPLLsDestroy(rhdPtr);
    //RHDAudioDestroy(rhdPtr);
    RHDLUTsDestroy(rhdPtr);
    RHDOutputsDestroy(rhdPtr);
    RHDConnectorsDestroy(rhdPtr);
    RHDCursorsDestroy(rhdPtr);
    RHDCrtcsDestroy(rhdPtr);
    RHDI2CFunc(pScrn->scrnIndex, rhdPtr->I2C, RHD_I2C_TEARDOWN, NULL);
#ifdef ATOM_BIOS
    RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS,
		    ATOM_TEARDOWN, NULL);
#endif
    if (rhdPtr->CursorInfo)
        IODelete(rhdPtr->CursorInfo, xf86CursorInfoRec, 1);
	
	if (rhdPtr->BIOSCopy) IOFree(rhdPtr->BIOSCopy, rhdPtr->BIOSSize);
		
    IOFree(pScrn->driverPrivate, sizeof(RHDRec));	/* == rhdPtr */
    pScrn->driverPrivate = NULL;
}

/*
 *
 */
static Bool
RHDPreInit(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr;
    Bool ret = FALSE;
    RHDI2CDataArg i2cArg;
    DisplayModePtr Modes;		/* Non-RandR-case only */

    /*
     * Allocate the RhdRec driverPrivate
     * (for the PCIACCESS case this is done in Probe already)
     */
    if (!RHDGetRec(pScrn)) {
	return FALSE;
    }

    rhdPtr = RHDPTR(pScrn);

    /* Get server verbosity level */
    rhdPtr->verbosity = pScrn->options->verbosity;

    pScrn->videoRam = 0;

	int i = 0;
	while ( RHDPCIchipsets[i].numChipset > 0 ) {
		if (pScrn->PciInfo->chipType == RHDPCIchipsets[i].PCIid) {
			rhdPtr->ChipSet = RHDPCIchipsets[i].numChipset;
			break;
		}
		i++;
	}
	if ( RHDPCIchipsets[i].numChipset <= 0 ) return FALSE;
	
	rhdPtr->PciInfo = pScrn->PciInfo;
	rhdPtr->PciTag = pScrn->PciTag;
	/*
	rhdPtr->NBPciTag = rhdPtr->PciTag;	//the IGP now is handled gracefully, no need for this
	
    if (RHDIsIGP(rhdPtr->ChipSet))
	rhdGetIGPNorthBridgeInfo(rhdPtr); */

    pScrn->chipset = (char *)xf86TokenToString(RHDChipsets, rhdPtr->ChipSet);

	rhdPtr->BIOSCopy = pScrn->memoryMap->BIOSCopy;
	rhdPtr->BIOSSize = pScrn->memoryMap->BIOSLength;
	pScrn->memoryMap->BIOSCopy = NULL;

    /* Now check whether we know this card */
    rhdPtr->Card = RHDCardIdentify(pScrn);
    if (rhdPtr->Card)
	LOG("Detected an %s on a %s\n",
		   pScrn->chipset, rhdPtr->Card->name);
    else
	LOG("Detected an %s on an "
		   "unidentified card\n", pScrn->chipset);
	rhdPtr->hpdUsage = RHD_HPD_USAGE_AUTO;	//we initialize it here, may later set by user option
    if (rhdPtr->Card && rhdPtr->Card->flags & RHD_CARD_FLAG_HPDSWAP &&
	rhdPtr->hpdUsage == RHD_HPD_USAGE_AUTO)
	rhdPtr->hpdUsage = RHD_HPD_USAGE_AUTO_SWAP;
    if (rhdPtr->Card && rhdPtr->Card->flags & RHD_CARD_FLAG_HPDOFF &&
	rhdPtr->hpdUsage == RHD_HPD_USAGE_AUTO)
	rhdPtr->hpdUsage = RHD_HPD_USAGE_AUTO_OFF;

    /* We need access to IO space already */
	rhdPtr->MMIOBase = pScrn->memoryMap->MMIOBase;
	if (!rhdPtr->MMIOBase) {
	LOG("Failed to map MMIO.\n");
	goto error0;
    }

    rhdPtr->cardType = rhdGetCardType(rhdPtr);

#ifdef ATOM_BIOS
    {
	AtomBiosArgRec atomBiosArg;

	if (RHDAtomBiosFunc(pScrn->scrnIndex, NULL, ATOM_INIT, &atomBiosArg)
	    == ATOM_SUCCESS) {
	    rhdPtr->atomBIOS = atomBiosArg.atomhandle;
	} else {
	    if (RHDUseAtom(rhdPtr,  NULL, atomUsageAny)) {
		LOG("No AtomBIOS image found but required for AtomBIOS based mode setting\n");
		goto error0; /* @@@ No blacklist handling. So far no blacklists are used for any subsystem */
	    }
	}
    }
#else
	/*
    LOG("**************************************************\n");
    LOG("** Code has been built without AtomBIOS support **\n");
    LOG("** this may seriously affect the functionality ***\n");
    LOG("**              of this driver                 ***\n");
    LOG("**************************************************\n");
    if (RHDUseAtom(rhdPtr,  NULL, atomUsageAny)) {
	LOG("No AtomBIOS support compiled in but required for this chipset/ current settings\n");
	goto error0;
    } */
#endif
    rhdPtr->tvMode = RHD_TV_NONE;
    {
#ifdef ATOM_BIOS
		const struct { char *name; enum RHD_TV_MODE mode; }
		rhdTVModeMapName[] = {
			{"NTSC", RHD_TV_NTSC},
			{"NTSCJ", RHD_TV_NTSCJ},
			{"PAL", RHD_TV_PAL},
			{"PALM", RHD_TV_PALM},
			{"PALCN", RHD_TV_PALCN},
			{"PALN", RHD_TV_PALN},
			{"PAL60", RHD_TV_PAL60},
			{"SECAM", RHD_TV_SECAM},
			{"CV", RHD_TV_CV},
			{NULL, RHD_TV_NONE}
		};
		
		if (rhdPtr->tvMode == RHD_TV_NONE) {
			AtomBiosArgRec atomBiosArg;
			
			int i = 0;
			
			if (RHDAtomBiosFunc(rhdPtr->scrnIndex, rhdPtr->atomBIOS,
								ATOM_ANALOG_TV_DEFAULT_MODE, &atomBiosArg)
				== ATOM_SUCCESS) {
				rhdPtr->tvMode = atomBiosArg.tvMode;
				while (rhdTVModeMapName[i].name) {
					if (rhdTVModeMapName[i].mode == rhdPtr->tvMode) {
						LOG("Found default TV Mode %s\n",rhdTVModeMapName[i].name);
						break;
					}
					i++;
				}
			}
		}
#endif
    }
    /* We can use a register which is programmed by the BIOS to find out the
       size of our framebuffer */
    if (!pScrn->videoRam) {
		pScrn->videoRam = rhdGetVideoRamSize(rhdPtr);
		if (!pScrn->videoRam) {
			LOG("No Video RAM detected.\n");
			goto error1;
		}
    }
    LOG("VideoRAM: %d kByte\n",
		pScrn->videoRam);
	
    rhdPtr->FbFreeStart = 0;
    rhdPtr->FbFreeSize = pScrn->videoRam * 1024;
	
#ifdef ATOM_BIOS
    if (rhdPtr->atomBIOS) { 	/* for testing functions */
		
        AtomBiosArgRec atomBiosArg;
		
        atomBiosArg.fb.start = rhdPtr->FbFreeStart;
        atomBiosArg.fb.size = rhdPtr->FbFreeSize;
        if (RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS, ATOM_ALLOCATE_FB_SCRATCH,
							&atomBiosArg) == ATOM_SUCCESS) {
			rhdPtr->FbFreeStart = atomBiosArg.fb.start;
			rhdPtr->FbFreeSize = atomBiosArg.fb.size;
		}
		
		RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS, ATOM_GET_DEFAULT_ENGINE_CLOCK,
						&atomBiosArg);
		RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS, ATOM_GET_DEFAULT_MEMORY_CLOCK,
						&atomBiosArg);
		RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS,
						ATOM_GET_MAX_PIXEL_CLOCK_PLL_OUTPUT, &atomBiosArg);
		RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS,
						ATOM_GET_MIN_PIXEL_CLOCK_PLL_OUTPUT, &atomBiosArg);
		RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS,
						ATOM_GET_MAX_PIXEL_CLOCK_PLL_INPUT, &atomBiosArg);
		RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS,
						ATOM_GET_MIN_PIXEL_CLOCK_PLL_INPUT, &atomBiosArg);
		RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS,
						ATOM_GET_MAX_PIXEL_CLK, &atomBiosArg);
		RHDAtomBiosFunc(pScrn->scrnIndex, rhdPtr->atomBIOS,
						ATOM_GET_REF_CLOCK, &atomBiosArg);
    }
#endif
	
	if (RHDI2CFunc(pScrn->scrnIndex, NULL, RHD_I2C_INIT, &i2cArg) == RHD_I2C_SUCCESS)
		rhdPtr->I2C = i2cArg.I2CBusList;
	else {
		LOG("I2C init failed\n");
		goto error1;
	}

    /* Init modesetting structures */
    RHDVGAInit(rhdPtr);
    RHDMCInit(rhdPtr);
    if (!RHDCrtcsInit(rhdPtr))
#ifdef ATOM_BIOS
		RHDAtomCrtcsInit(rhdPtr);
#else
	goto error1;
#endif
    if (!RHDPLLsInit(rhdPtr))
#ifdef ATOM_BIOS
		RHDAtomPLLsInit(rhdPtr);
#else
	goto error1;
#endif
    //RHDAudioInit(rhdPtr);
    RHDLUTsInit(rhdPtr);
    if (pScrn->options->HWCursorSupport) {
		//RHDCursorsInit(rhdPtr); // moved to RHDxf86InitCursor
		if (!RHDxf86InitCursor(pScrn)) pScrn->options->HWCursorSupport = FALSE;	//allocate cursor image space failed
	}
    if (pScrn->options->lowPowerMode) RHDPmInit(rhdPtr);
	
    if (!RHDConnectorsInit(rhdPtr, rhdPtr->Card)) {
		LOG("Card information has invalid connector information\n");
		goto error1;
    }
	
#ifdef ATOM_BIOS
	struct rhdAtomOutputDeviceList *OutputDeviceList = NULL;
	int k = 0;
	
	if (rhdPtr->Card && (rhdPtr->Card->ConnectorInfo[0].Type != RHD_CONNECTOR_NONE)
		&& (rhdPtr->Card->DeviceInfo[0][0] != atomNone || rhdPtr->Card->DeviceInfo[0][1] != atomNone)) {
		int i;
		
		for (i = 0; i < RHD_CONNECTORS_MAX; i++) {
			int j;
			if (rhdPtr->Card->ConnectorInfo[i].Type == RHD_CONNECTOR_NONE) break;
			for (j = 0; j < MAX_OUTPUTS_PER_CONNECTOR; j++) {
				if (rhdPtr->Card->ConnectorInfo[i].Output[j] != RHD_OUTPUT_NONE) {
					/* if (!(OutputDeviceList = (struct rhdAtomOutputDeviceList *)xrealloc(
					 OutputDeviceList, sizeof (struct rhdAtomOutputDeviceList) * (k + 1)))) */
					struct rhdAtomOutputDeviceList *temp = (struct rhdAtomOutputDeviceList *)IOMalloc(sizeof (struct rhdAtomOutputDeviceList) * (k + 1));
					if (temp == NULL) break;
					bzero(temp, sizeof (struct rhdAtomOutputDeviceList) * (k + 1));
					bcopy(OutputDeviceList, temp, sizeof (struct rhdAtomOutputDeviceList) * k);
					IOFree(OutputDeviceList, sizeof (struct rhdAtomOutputDeviceList) * k);
					OutputDeviceList = temp;
					
					OutputDeviceList[k].ConnectorType = rhdPtr->Card->ConnectorInfo[i].Type;
					OutputDeviceList[k].DeviceId = rhdPtr->Card->DeviceInfo[i][j];
					OutputDeviceList[k].OutputType = rhdPtr->Card->ConnectorInfo[i].Output[j];
					LOG("OutputDevice: C: 0x%2.2x O: 0x%2.2x DevID: 0x%2.2x\n",
						OutputDeviceList[k].ConnectorType, OutputDeviceList[k].OutputType,
						OutputDeviceList[k].DeviceId);
					k++;
				}
			}
		}
	} else {
		AtomBiosArgRec data;
		
		data.chipset = rhdPtr->ChipSet;
		if (RHDAtomBiosFunc(rhdPtr->scrnIndex, rhdPtr->atomBIOS,
							ATOM_GET_OUTPUT_DEVICE_LIST, &data) == ATOM_SUCCESS) {
			OutputDeviceList = data.OutputDeviceList;
			k = data.deviceListCount;
		}
	}
	
	if (OutputDeviceList) {
		struct rhdOutput *Output;
		
		for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
			RHDAtomSetupOutputDriverPrivate(OutputDeviceList, Output);
		IOFree(OutputDeviceList, sizeof (struct rhdAtomOutputDeviceList) * k);
	}
#endif
	
    /*
     * Set this here as we might need it for the validation of a fixed mode in
     * rhdModeLayoutSelect(). Later it is used for Virtual selection and mode
     * pool creation.
     * For Virtual selection, the scanout area is all the free space we have.
     */
    rhdPtr->FbScanoutStart = rhdPtr->FbFreeStart;
    rhdPtr->FbScanoutSize = rhdPtr->FbFreeSize;
	pScrn->fbOffset = rhdPtr->FbFreeStart;
	
	/* Pick anything for now */	//here initialize ATIPanel
	if (!rhdModeLayoutSelect(rhdPtr)) {
		LOG("Failed to detect a connected monitor\n");
		goto error1;
	}
	
	rhdModeLayoutPrint(rhdPtr);
	/*	
	 if (pScrn->depth > 1) {
	 Gamma zeros = {0.0, 0.0, 0.0};
	 
	 if (!xf86SetGamma(pScrn, zeros)) {
	 goto error1;
	 }
	 }
	 */	
	
	Modes = RHDModesPoolCreate(pScrn, FALSE);
	if (!Modes) {
		LOG("No valid modes found\n");
		goto error1;
	}
	
	if (!pScrn->virtualX || !pScrn->virtualY)
		RHDGetVirtualFromModesAndFilter(pScrn, Modes, FALSE);
	
	RHDModesAttach(pScrn, Modes);
	
	rhdModeDPISet(pScrn);
 	
	LOG("Using %dx%d Framebuffer with %d pitch\n", pScrn->virtualX,
		pScrn->virtualY, pScrn->displayWidth);
	
	//this is for accelaration, no need for osx
	/* grab the real scanout area and adjust the free space */
	/*
	 rhdPtr->FbScanoutSize = RHD_FB_CHUNK(pScrn->displayWidth * pScrn->bitsPerPixel *
	 pScrn->virtualY / 8);
	 rhdPtr->FbScanoutStart = RHDAllocFb(rhdPtr, rhdPtr->FbScanoutSize,
	 "ScanoutBuffer");
	 //ASSERT(rhdPtr->FbScanoutStart != (unsigned)-1);
	 */
	
	LOG("Free FB offset 0x%08X (size = 0x%08X)\n",
		rhdPtr->FbFreeStart, rhdPtr->FbFreeSize);
	
    ret = TRUE;
	
error1:
error0:
    if (!ret)
		RHDFreeRec(pScrn);
	
    return ret;
}

/* Mandatory */
static Bool
RHDScreenInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    RHDFUNC(pScrn);

    if (!rhdMapFB(rhdPtr)) {
	LOG("Failed to map FB.\n");
	return FALSE;
    }

    /* save previous mode */
    //rhdSave(rhdPtr);	//we not going to restore yet (Dong)

    /* disable all memory accesses for MC setup */
    RHDVGADisable(rhdPtr);

    if (!rhdAllIdle(rhdPtr)) return FALSE;

    /* now set up the MC - has to be done after AllIdle and before DRI init */
    if (!RHDMCSetupFBLocation(rhdPtr, rhdPtr->FbIntAddress, rhdPtr->FbIntSize))
	return FALSE;

    /* Static power management */
    if (rhdPtr->Pm)
	rhdPtr->Pm->SelectState (rhdPtr, RHD_PM_IDLE);

    RHDPrepareMode(rhdPtr);
    /* now init the new mode */
	rhdModeInit(pScrn, mode);
	
    /* fix viewport */
    RHDAdjustFrame(pScrn, pScrn->frameX0, pScrn->frameY0);

    /* enable/disable audio */
    //RHDAudioSetEnable(rhdPtr, rhdPtr->audio);
/* moved to preInit
	if (pScrn->options->HWCursorSupport) {
		// Inititalize HW cursor
		Bool ret;
		ret = RHDxf86InitCursor(pScrn);
		if (!ret)
			LOG("Hardware cursor initialization failed\n");
	} */
    /* fixme */
    /* Support 10-bits of precision in LUT */ /*
    if (!xf86HandleColormaps(pScreen, 256, 10,
                         RHDLoadPalette, NULL,
                         CMAP_PALETTED_TRUECOLOR | CMAP_RELOAD_ON_MODE_SWITCH))
	return FALSE; */

    /* Function to unblank, so that we don't show an uninitialised FB */
    //pScreen->SaveScreen = RHDSaveScreen;
	RHDSaveScreen(pScrn, TRUE);	//unblank the screens

    /* Setup DPMS mode */

    //xf86DPMSInit(pScreen, (DPMSSetProcPtr)RHDDisplayPowerManagementSet,0);

    return TRUE;
}

static Bool
rhdAllIdle(RHDPtr rhdPtr)
{
    int i;

    /* Make sure that VGA has been disabled before calling AllIdle() */
    //ASSERT(RHD_CHECKDEBUGFLAG(rhdPtr, VGA_SETUP));

    /* stop scanout */
    for (i = 0; i < 2; i++)
	if (!rhdPtr->Crtc[i]->Power(rhdPtr->Crtc[i], RHD_POWER_RESET)) {
	    LOG("%s: unable to stop CRTC: cannot idle MC\n",
		       __func__);
	    return FALSE;
	}

    if (!RHDMCIdleWait(rhdPtr, 1000)) {
	LOG("MC not idle\n");
	return FALSE;
    }
    return TRUE;
}

/* Mandatory */
static Bool
RHDCloseScreen(int scrnIndex)	//not going to use it yet (Dong)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    RHDPtr rhdPtr = RHDPTR(pScrn);
    Bool Idle = TRUE; /* yes, this is correct! */
    
    if (pScrn->vtSema)
	Idle = rhdAllIdle(rhdPtr);

    if (pScrn->vtSema)
	rhdRestore(rhdPtr);

    rhdUnmapFB(rhdPtr);

    pScrn->vtSema = FALSE;
    return TRUE;
}

/* Optional */
static void
RHDFreeScreen(int scrnIndex, int flags)
{
    RHDFreeRec(xf86Screens[scrnIndex]);
}

/* Mandatory */
static Bool
RHDEnterVT(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);

    RHDFUNC(rhdPtr);

    //rhdSave(rhdPtr);

    /* disable all memory accesses for MC setup */
    RHDVGADisable(rhdPtr);

    if (!rhdAllIdle(rhdPtr))
	return FALSE;

    /* now set up the MC - has to be done before DRI init */
    RHDMCSetupFBLocation(rhdPtr, rhdPtr->FbIntAddress, rhdPtr->FbIntSize);

#ifdef ATOM_BIOS
    /* Set accelerator mode in the BIOSScratch registers */
    //RHDAtomBIOSScratchSetAccelratorMode(rhdPtr, TRUE);
#endif

	rhdModeInit(pScrn, mode);

    /* @@@ video overlays can be initialized here */

    if (rhdPtr->CursorInfo) rhdReloadCursor(pScrn);	//cursorImage should be set before this (Dong)
    /* rhdShowCursor() done by AdjustFrame */

    RHDAdjustFrame(pScrn, pScrn->frameX0, pScrn->frameY0);

    /* enable/disable audio */
    //RHDAudioSetEnable(rhdPtr, rhdPtr->audio);

    /* Static power management */
    if (rhdPtr->Pm)
	rhdPtr->Pm->SelectState (rhdPtr, RHD_PM_IDLE);

    return TRUE;
}

/* Mandatory */
static void
RHDLeaveVT(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);

    RHDFUNC(rhdPtr);

    rhdAllIdle(rhdPtr);

    rhdRestore(rhdPtr);
}

static Bool
RHDSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
	
    RHDFUNC(rhdPtr);
	
    /* disable all memory accesses for MC setup */
    RHDVGADisable(rhdPtr);

    if (!rhdAllIdle(rhdPtr)) return FALSE;

    /* now set up the MC - has to be done before DRI init */
    RHDMCSetupFBLocation(rhdPtr, rhdPtr->FbIntAddress, rhdPtr->FbIntSize);
	
	rhdSetMode(pScrn, mode);
	
    return TRUE;
}

/*
 * High level bit banging functions
 */

static void
RHDAdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    struct rhdCrtc *Crtc;
	
	Crtc = rhdPtr->Crtc[0];
	if (Crtc->Active)
		Crtc->FrameSet(Crtc, x, y);
	
	Crtc = rhdPtr->Crtc[1];
	if (Crtc->Active)
		Crtc->FrameSet(Crtc, x, y);
	
    if (rhdPtr->CursorInfo)
		rhdShowCursor(pScrn);
}

static void
RHDDisplayPowerManagementSet(ScrnInfoPtr pScrn,
                             int PowerManagementMode,
			     int flags)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    struct rhdOutput *Output;
    struct rhdCrtc *Crtc1, *Crtc2;

    RHDFUNC(rhdPtr);

    if (!pScrn->vtSema)
	return;

    Crtc1 = rhdPtr->Crtc[0];
    Crtc2 = rhdPtr->Crtc[1];

    switch (PowerManagementMode) {
    case DPMSModeOn:
	if (Crtc1->Active) {
	    Crtc1->Power(Crtc1, RHD_POWER_ON);

	    for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
		if (Output->Power && Output->Active && (Output->Crtc == Crtc1)) {
		    Output->Power(Output, RHD_POWER_ON);
#ifdef ATOM_BIOS
		    RHDAtomBIOSScratchPMState(rhdPtr, Output, PowerManagementMode);
#endif
		}

	    Crtc1->Blank(Crtc1, FALSE);
	}

	if (Crtc2->Active) {
	    Crtc2->Power(Crtc2, RHD_POWER_ON);

	    for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
		if (Output->Power && Output->Active && (Output->Crtc == Crtc2)) {
		    Output->Power(Output, RHD_POWER_ON);
#ifdef ATOM_BIOS
		    RHDAtomBIOSScratchPMState(rhdPtr, Output, PowerManagementMode);
#endif
		}
	    Crtc2->Blank(Crtc2, FALSE);
	}
	break;
    case DPMSModeStandby:
    case DPMSModeSuspend:
    case DPMSModeOff:
	if (Crtc1->Active) {
	    Crtc1->Blank(Crtc1, TRUE);

	    for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
		if (Output->Power && Output->Active && (Output->Crtc == Crtc1)) {
		    Output->Power(Output, RHD_POWER_RESET);
#ifdef ATOM_BIOS
		    RHDAtomBIOSScratchPMState(rhdPtr, Output, PowerManagementMode);
#endif
		}

	    Crtc1->Power(Crtc1, RHD_POWER_RESET);
	}

	if (Crtc2->Active) {
	    Crtc2->Blank(Crtc2, TRUE);

	    for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
		if (Output->Power && Output->Active && (Output->Crtc == Crtc2)) {
		    Output->Power(Output, RHD_POWER_RESET);
#ifdef ATOM_BIOS
		    RHDAtomBIOSScratchPMState(rhdPtr, Output, PowerManagementMode);
#endif
		}

	    Crtc2->Power(Crtc2, RHD_POWER_RESET);
	}
	break;
    }
}

/*
 *
 */
static void
RHDLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices, LOCO *colors)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    struct rhdCrtc *Crtc;

    CARD16 red[256], green[256], blue[256];
    int i, index, j, n;
    Bool partial_table = FALSE;

    switch (pScrn->depth) {
    case 8:
    case 24:
    case 32:
        if (numColors < 256) {
            partial_table = TRUE;
            break;
        }
        for (i = 0; i < numColors; i++) {
            index = indices[i];
            red[index] = colors[index].red << 6;
            green[index] = colors[index].green << 6;
            blue[index] = colors[index].blue << 6;
        }
        break;
    case 16:
        if (numColors < 64) {
            partial_table = TRUE;
            break;
        }
        /* 6 bits of green, 5 bits of red and blue each */
        for (i = 0; i < numColors; i++) {
            index = indices[i];
            n = index * 4;
            for (j = 0; j < 4; j++) {
                red[n + j] = colors[index/2].red << 6;
                green[n + j] = colors[index].green << 6;
                blue[n + j] = colors[index/2].blue << 6;
            }
        }
        break;
    case 15:
        if (numColors < 32) {
            partial_table = TRUE;
            break;
        }
        /* 5 bits each */
        for (i = 0; i < numColors; i++) {
            int j, n;

            index = indices[i];
            n = index * 8;
            for (j = 0; j < 8; j++) {
                red[n + j] = colors[index].red << 6;
                green[n + j] = colors[index].green << 6;
                blue[n+ j] = colors[index].blue << 6;
            }
        }
        break;
    }

    for (i = 0; i < 2; i++) {
        Crtc = rhdPtr->Crtc[i];
        if ((pScrn->scrnIndex == Crtc->scrnIndex) && Crtc->Active) {
            if (!partial_table)
                Crtc->LUT->Set(Crtc->LUT, red, green, blue);
            else
                Crtc->LUT->SetRows(Crtc->LUT, numColors, indices, colors);
        }
    }
}

/*
 *
 */
static Bool
RHDSaveScreen(ScrnInfoPtr pScrn, Bool unblank)
{
    RHDPtr rhdPtr;
    struct rhdCrtc *Crtc;

    if (pScrn == NULL)
	return TRUE;

    RHDFUNC(pScrn);

    rhdPtr = RHDPTR(pScrn);

    if (!pScrn->vtSema)
	return TRUE;

    Crtc = rhdPtr->Crtc[0];
 	Crtc->Blank(Crtc, !unblank);

    Crtc = rhdPtr->Crtc[1];
 	Crtc->Blank(Crtc, !unblank);

    return TRUE;
}

/*
 *
 */
static Bool
RHDScalePolicy(struct rhdMonitor *Monitor, struct rhdConnector *Connector)
{
    if (!Monitor || !Monitor->UseFixedModes || !Monitor->NativeMode)
	return FALSE;

    if (Connector->Type != RHD_CONNECTOR_PANEL)
	return FALSE;

    return TRUE;
}

/*
 *
 */
static CARD32
rhdGetVideoRamSize(RHDPtr rhdPtr)
{
    CARD32 RamSize, BARSize;

    RHDFUNC(rhdPtr);

    if (rhdPtr->ChipSet < RHD_R600)
	RamSize = (RHDRegRead(rhdPtr, R5XX_CONFIG_MEMSIZE)) >> 10;
    else
	RamSize = (RHDRegRead(rhdPtr, R6XX_CONFIG_MEMSIZE)) >> 10;
    BARSize = xf86Screens[rhdPtr->scrnIndex]->memoryMap->FbMapSize >> 10;	//what os tells you

    if (RamSize > BARSize) {
	LOG("The detected amount of videoram"
		   " exceeds the PCI BAR aperture.\n");
	LOG("Using only %dkB of the total "
		   "%dkB.\n", (int) BARSize, (int) RamSize);
	return BARSize;
    } else
	return RamSize;
}

/*
 *
 */
static Bool
rhdMapFB(RHDPtr rhdPtr)
{
    ScrnInfoPtr pScrn = xf86Screens[rhdPtr->scrnIndex];
    RHDFUNC(rhdPtr);

    rhdPtr->FbBase = NULL;

    //rhdPtr->FbPCIAddress = rhdPtr->PciInfo->memBase[RHD_FB_BAR];
    //rhdPtr->FbMapSize = 1 << rhdPtr->PciInfo->size[RHD_FB_BAR];
	rhdPtr->FbBase = pScrn->memoryMap->FbBase;
	rhdPtr->FbMapSize = pScrn->videoRam * 1024;

    /* some IGPs are special cases */
    switch (rhdPtr->ChipSet) {
   	case RHD_RS690:
	case RHD_RS740:
	    rhdPtr->FbPhysAddress = RHDReadMC(rhdPtr, RS69_K8_FB_LOCATION);
	    break;
	case RHD_RS780:
	    rhdPtr->FbPhysAddress = RHDReadMC(rhdPtr, RS78_K8_FB_LOCATION);
	    break;
	default:
	    rhdPtr->FbPhysAddress = 0;
	    break;
    }

    if (rhdPtr->FbPhysAddress) {
		Bool SetIGPMemory = TRUE;
		CARD32 option = X_DEFAULT;
		
		if (rhdPtr->SetIGPMemory) {
			option = X_CONFIG;
			SetIGPMemory = rhdPtr->SetIGPMemory;
		}
		if (SetIGPMemory && ! RHD_MC_IGP_SideportMemoryPresent(rhdPtr)) {
			SetIGPMemory = FALSE;
			option = X_DEFAULT;
		}
		if (SetIGPMemory) {
			CARD32 tmp = 0xfffffc00;
			CARD32 s = pScrn->videoRam;
			while (! (s & 0x1)) {
				s >>= 1;
				tmp <<= 1;
			}
			if (rhdPtr->FbPhysAddress & ~tmp) {
				LOG("IGP memory base 0x%8.8x seems to be bogus.\n", rhdPtr->FbPhysAddress);
				SetIGPMemory = FALSE;
				option = X_DEFAULT;
			}
		}
		if (SetIGPMemory) LOG("IGP memory @ 0x%8.8x is not used (Dong)\n",rhdPtr->FbPhysAddress);	//let's see where it is
			/*
		{
			CARD32 FbMapSizePCI =  rhdPtr->FbMapSize;
			
			LOG("Mapping IGP memory @ 0x%8.8x\n",rhdPtr->FbPhysAddress);
			
			rhdPtr->FbMapSize = pScrn->videoRam * 1024;
			rhdPtr->FbBase =
			xf86MapPciMem(rhdPtr->scrnIndex, VIDMEM_FRAMEBUFFER,
						  rhdPtr->PciTag,
						  rhdPtr->FbPhysAddress,
						  rhdPtr->FbMapSize);
			
			if (!rhdPtr->FbBase)
				rhdPtr->FbMapSize = FbMapSizePCI;
		} else {
			LOG("Not Mapping IGP memory\n");
		} */
    }
	
    /* go through the BAR */ /*
    if (!rhdPtr->FbBase) {
		rhdPtr->FbPhysAddress = rhdPtr->FbPCIAddress;
		
		if (rhdPtr->FbMapSize > (unsigned) pScrn->videoRam * 1024)
			rhdPtr->FbMapSize = pScrn->videoRam * 1024;
		
		rhdPtr->FbBase =
	    xf86MapPciMem(rhdPtr->scrnIndex, VIDMEM_FRAMEBUFFER, rhdPtr->PciTag,
					  rhdPtr->FbPhysAddress, rhdPtr->FbMapSize);
    }
	
    LOG("Physical FB Address: 0x%08X (PCI BAR: 0x%08X)\n",
	     rhdPtr->FbPhysAddress, rhdPtr->FbPCIAddress);
	*/
    if (!rhdPtr->FbBase)
        return FALSE;
	/*
    LOG("Mapped FB @ 0x%x to %p (size 0x%08X)\n",
	       rhdPtr->FbPhysAddress, rhdPtr->FbBase, rhdPtr->FbMapSize); */
    return TRUE;
}

/*
 *
 */
static void
rhdUnmapFB(RHDPtr rhdPtr)
{
    RHDFUNC(rhdPtr);

    if (!rhdPtr->FbBase)
	return;

    rhdPtr->FbBase = NULL;
}

/*
 *
 */
static void
rhdOutputConnectorCheck(struct rhdConnector *Connector)
{
    struct rhdOutput *Output;
    int i;

    /* First, try to sense */
    for (i = 0; i < 2; i++) {
		Output = Connector->Output[i];
		if (Output && Output->Sense) {
			/*
			 * This is ugly and needs to change when the TV support patches are in.
			 * The problem here is that the Output struct can be used for two connectors
			 * and thus two different devices
			 */
			if (Output->SensedType == RHD_SENSED_NONE) {
				/* Do this before sensing as AtomBIOS sense needs this info */
				Output->SensedType = Output->Sense(Output, Connector);
				if (Output->SensedType != RHD_SENSED_NONE) {
					RHDOutputPrintSensedType(Output);
					RHDOutputAttachConnector(Output, Connector);
					break;
				}
			}
		}
    }
	
    if (i == 2) {
		/* now just enable the ones without sensing */
		for (i = 0; i < 2; i++) {
			Output = Connector->Output[i];
			if (Output && !Output->Sense) {
				RHDOutputAttachConnector(Output, Connector);
				break;
			}
		}
    }
}

/*
 *
 */
static Bool
rhdModeLayoutSelect(RHDPtr rhdPtr)
{
    struct rhdOutput *Output;
    struct rhdConnector *Connector;
    Bool Found = FALSE;
    Bool ConnectorIsDMS59 = FALSE;
    int i = 0;

    RHDFUNC(rhdPtr);

    /* housekeeping */
    rhdPtr->Crtc[0]->PLL = rhdPtr->PLLs[0];
    rhdPtr->Crtc[0]->LUT = rhdPtr->LUT[0];

    rhdPtr->Crtc[1]->PLL = rhdPtr->PLLs[1];
    rhdPtr->Crtc[1]->LUT = rhdPtr->LUT[1];

    /* start layout afresh */
    for (Output = rhdPtr->Outputs; Output; Output = Output->Next) {
	Output->Active = FALSE;
	Output->Crtc = NULL;
	Output->Connector = NULL;
    }

    /* handle cards with DMS-59 connectors appropriately. The DMS-59 to VGA
       adapter does not raise HPD at all, so we need a fallback there. */
    if (rhdPtr->Card) {
	ConnectorIsDMS59 = rhdPtr->Card->flags & RHD_CARD_FLAG_DMS59;
	if (ConnectorIsDMS59)
	    LOG("Card %s has a DMS-59"
		       " connector.\n", rhdPtr->Card->name);
    }

    /* Check on the basis of Connector->HPD */
    for (i = 0; i < RHD_CONNECTORS_MAX; i++) {
		Connector = rhdPtr->Connector[i];
		
		if (!Connector)
			continue;
		
		if (Connector->HPDCheck) {
			if (Connector->HPDCheck(Connector)) {
				Connector->HPDAttached = TRUE;
				
				rhdOutputConnectorCheck(Connector);
			} else {
				Connector->HPDAttached = FALSE;
				if (ConnectorIsDMS59)
					rhdOutputConnectorCheck(Connector);
			}
		} else
			rhdOutputConnectorCheck(Connector);
    }
	
    i = 0; /* counter for CRTCs */
    for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
	if (Output->Connector) {
	    struct rhdMonitor *Monitor = NULL;

	    Connector = Output->Connector;

	    Monitor = RHDMonitorInit(Connector);

	    if (!Monitor && (Connector->Type == RHD_CONNECTOR_PANEL)) {
		LOG("Unable to attach a"
			   " monitor to connector \"%s\"\n", Connector->Name);
		Output->Active = FALSE;
	    } else if (!Output->AllocFree || Output->AllocFree(Output, RHD_OUTPUT_ALLOC)){
		Connector->Monitor = Monitor;

		Output->Active = TRUE;

		Output->Crtc = rhdPtr->Crtc[i & 1]; /* ;) */	//assign crtc here for ATIConnections
		i++;

		Output->Crtc->Active = TRUE;

		if (RHDScalePolicy(Monitor, Connector)) {
		    Output->Crtc->ScaledToMode = RHDModeCopy(Monitor->NativeMode);
		    LOG("Crtc[%d]: found native mode from Monitor[%s]: \n",
			       Output->Crtc->Id, Monitor->Name);
		    RHDPrintModeline(Output->Crtc->ScaledToMode);
		}

		Found = TRUE;

		if (Monitor) {
		    /* If this is a DVI attached monitor, enable reduced blanking.
		     * TODO: iiyama vm pro 453: CRT with DVI-D == No reduced.
		     */
		    if ((Output->Id == RHD_OUTPUT_TMDSA) ||
			(Output->Id == RHD_OUTPUT_LVTMA) ||
			(Output->Id == RHD_OUTPUT_KLDSKP_LVTMA) ||
			(Output->Id == RHD_OUTPUT_UNIPHYA) ||
			(Output->Id == RHD_OUTPUT_UNIPHYB))
			Monitor->ReducedAllowed = TRUE;

		    LOG("Connector \"%s\" uses Monitor \"%s\":\n",
			       Connector->Name, Monitor->Name);
		    //RHDMonitorPrint(Monitor);
		} else
		    LOG("Connector \"%s\": Failed to retrieve Monitor"
			       " information.\n", Connector->Name);
	    }
	}

    /* Now validate the scaled modes attached to crtcs */
    for (i = 0; i < 2; i++) {
		struct rhdCrtc *crtc = rhdPtr->Crtc[i];
		if (crtc->ScaledToMode && (RHDValidateScaledToMode(crtc, crtc->ScaledToMode) != MODE_OK)) {
			LOG("Crtc[%d]: scaled mode invalid.\n", crtc->Id);
			DisplayModePtr mode = crtc->ScaledToMode;
			LOG("mode: %d %d %d %d\n", mode->HDisplay, mode->HSyncStart, mode->HSyncEnd, mode->HTotal);
			LOG("mode: %d %d %d %d\n", mode->VDisplay, mode->VSyncStart, mode->VSyncEnd, mode->VTotal);
			LOG("Crtc: %d %d %d %d %d %d\n", mode->CrtcHDisplay, mode->CrtcHBlankStart, mode->CrtcHSyncStart, mode->CrtcHSyncEnd, mode->CrtcHBlankEnd, mode->CrtcHTotal);
			LOG("Crtc: %d %d %d %d %d %d\n", mode->CrtcVDisplay, mode->CrtcVBlankStart, mode->CrtcVSyncStart, mode->CrtcVSyncEnd, mode->CrtcHBlankEnd, mode->CrtcVTotal);
			IODelete(crtc->ScaledToMode, DisplayModeRec, 1);
			crtc->ScaledToMode = NULL;
		}
    }
	
    return Found;
}

/*
 * Calculating DPI will never be good. But here we attempt to make it work,
 * somewhat, with multiple monitors.
 *
 * The real solution for the DPI problem cannot be something statically,
 * as DPI varies with resolutions chosen and with displays attached/detached.
 */
static void
rhdModeDPISet(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
	
	/* go over the monitors */
	struct rhdCrtc *Crtc;
	struct rhdOutput *Output;
	struct rhdMonitor *Monitor;
	/* we need to use split counters, x or y might fail separately */
	int i, xcount, ycount;
	
	pScrn->xDpi = 0;
	pScrn->yDpi = 0;
	xcount = 0;
	ycount = 0;
	
	for (i = 0; i < 2; i++) {
		Crtc = rhdPtr->Crtc[i];
		if (Crtc->Active) {
			for (Output = rhdPtr->Outputs; Output; Output = Output->Next) {
				if (Output->Active && (Output->Crtc == Crtc)) {
					if (Output->Connector && Output->Connector->Monitor) {
						Monitor = Output->Connector->Monitor;
						
						if (Monitor->xDpi) {
							pScrn->xDpi += (Monitor->xDpi - pScrn->xDpi) / (xcount + 1);
							xcount++;
						}
						
						if (Monitor->yDpi) {
							pScrn->yDpi += (Monitor->yDpi - pScrn->yDpi) / (ycount + 1);
							ycount++;
						}
					}
				}
			}
		}
	}
	
	/* make sure that we have at least some value */
	if (!pScrn->xDpi || !pScrn->yDpi) {
		if (pScrn->xDpi)
			pScrn->yDpi = pScrn->xDpi;
		else if (pScrn->yDpi)
			pScrn->xDpi = pScrn->yDpi;
		else {
			pScrn->xDpi = 96;
			pScrn->yDpi = 96;
		}
	}
	
#ifndef MMPERINCH
#define MMPERINCH 25.4
#endif
    pScrn->widthmm = pScrn->virtualX * MMPERINCH / pScrn->xDpi;
    pScrn->heightmm = pScrn->virtualY * MMPERINCH / pScrn->yDpi;
	
    LOG("Using %dx%d DPI.\n",
			   pScrn->xDpi, pScrn->yDpi);
}


/*
 *
 */
static void
rhdModeLayoutPrint(RHDPtr rhdPtr)
{
    struct rhdCrtc *Crtc;
    struct rhdOutput *Output;
    Bool Found;
	
    LOG("Listing modesetting layout:\n");
	
    /* CRTC 1 */
    Crtc = rhdPtr->Crtc[0];
    if (Crtc->Active) {
		LOG("%s: tied to %s and %s:\n",
			Crtc->Name, Crtc->PLL->Name, Crtc->LUT->Name);
		
		Found = FALSE;
		for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
			if (Output->Active && (Output->Crtc == Crtc)) {
				if (!Found) {
					LOG("\tOutputs: %s (%s)",
						Output->Name, Output->Connector->Name);
					Found = TRUE;
				} else
					LOG(", %s (%s)", Output->Name, Output->Connector->Name);
			}
		
		if (!Found)
			LOG("%s is active without outputs\n", Crtc->Name);
		else
			LOG("\n");
    } else
		LOG("%s: unused\n", Crtc->Name);
	
    /* CRTC 2 */
    Crtc = rhdPtr->Crtc[1];
    if (Crtc->Active) {
		LOG("%s: tied to %s and %s:\n",
			Crtc->Name, Crtc->PLL->Name, Crtc->LUT->Name);
		
		Found = FALSE;
		for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
			if (Output->Active && (Output->Crtc == Crtc)) {
				if (!Found) {
					LOG("\tOutputs: %s (%s)",
						Output->Name, Output->Connector->Name);
					Found = TRUE;
				} else
					LOG(", %s (%s)", Output->Name, Output->Connector->Name);
			}
		
		if (!Found)
			LOG("%s is active without outputs\n", Crtc->Name);
		else
			LOG("\n");
    } else
		LOG("%s: unused\n", Crtc->Name);
	
    /* Print out unused Outputs */
    Found = FALSE;
    for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
		if (!Output->Active) {
			if (!Found) {
				LOG("\tUnused Outputs: %s", Output->Name);
				Found = TRUE;
			} else
				LOG(", %s", Output->Name);
		}
	
    if (Found)
		LOG("\n");
}

/*
 *
 */
void
RHDPrepareMode(RHDPtr rhdPtr)
{
    RHDFUNC(rhdPtr);

    /* Stop crap from being shown: gets reenabled through SaveScreen */
    rhdPtr->Crtc[0]->Blank(rhdPtr->Crtc[0], TRUE);
    rhdPtr->Crtc[1]->Blank(rhdPtr->Crtc[1], TRUE);
    /* no active outputs == no mess */
    RHDOutputsPower(rhdPtr, RHD_POWER_RESET);
}

/*
 *
 */
static void
rhdModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    RHDFUNC(pScrn);
    pScrn->vtSema = TRUE;

    rhdSetMode(pScrn, mode);
}

/*
 *
 */
static void
rhdSetMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    RHDFUNC(rhdPtr);

    LOG("Setting up \"%s\" (%dx%d@%dHz)\n",
	       mode->name, mode->CrtcHDisplay, mode->CrtcVDisplay,
	       (int)mode->VRefresh);

    /* Set up D1/D2 and appendages */
    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc;

	Crtc = rhdPtr->Crtc[i];
	if (Crtc->Active) {
	    Crtc->FBSet(Crtc, pScrn->displayWidth, pScrn->virtualX, pScrn->virtualY,
			pScrn->depth, rhdPtr->FbScanoutStart);
	    if (Crtc->ScaledToMode) {
		Crtc->ModeSet(Crtc, Crtc->ScaledToMode);
		if (Crtc->ScaleSet)
		    Crtc->ScaleSet(Crtc, Crtc->ScaleType, mode, Crtc->ScaledToMode);
	    } else {
		Crtc->ModeSet(Crtc, mode);
		if (Crtc->ScaleSet)
		    Crtc->ScaleSet(Crtc, RHD_CRTC_SCALE_TYPE_NONE, mode, NULL);
	    }
	    RHDPLLSet(Crtc->PLL, mode->Clock);
	    Crtc->LUTSelect(Crtc, Crtc->LUT);
	    RHDOutputsMode(rhdPtr, Crtc, Crtc->ScaledToMode
			   ? Crtc->ScaledToMode : mode);
	}
    }

	/* shut down that what we don't use */
	RHDPLLsShutdownInactive(rhdPtr);
	RHDOutputsShutdownInactive(rhdPtr);
	
	if (rhdPtr->Crtc[0]->Active)
		rhdPtr->Crtc[0]->Power(rhdPtr->Crtc[0], RHD_POWER_ON);
	else
		rhdPtr->Crtc[0]->Power(rhdPtr->Crtc[0], RHD_POWER_SHUTDOWN);
	
	if (rhdPtr->Crtc[1]->Active)
		rhdPtr->Crtc[1]->Power(rhdPtr->Crtc[1], RHD_POWER_ON);
	else
		rhdPtr->Crtc[1]->Power(rhdPtr->Crtc[1], RHD_POWER_SHUTDOWN);
	
	RHDOutputsPower(rhdPtr, RHD_POWER_ON);
}

/*
 *
 */
static void
rhdSave(RHDPtr rhdPtr)
{
    ScrnInfoPtr pScrn = xf86Screens[rhdPtr->scrnIndex];

    RHDFUNC(rhdPtr);

    RHDMCSave(rhdPtr);

    RHDVGASave(rhdPtr);

    RHDOutputsSave(rhdPtr);
#ifdef ATOM_BIOS
    rhdPtr->BIOSScratch = RHDSaveBiosScratchRegisters(rhdPtr);
#endif
    RHDPLLsSave(rhdPtr);
    //RHDAudioSave(rhdPtr);
    RHDLUTsSave(rhdPtr);

    RHDCrtcSave(rhdPtr->Crtc[0]);
    RHDCrtcSave(rhdPtr->Crtc[1]);
    rhdSaveCursor(pScrn);

    RHDPmSave(rhdPtr);
}

/*
 *
 */
static void
rhdRestore(RHDPtr rhdPtr)
{
    ScrnInfoPtr pScrn = xf86Screens[rhdPtr->scrnIndex];

    RHDFUNC(rhdPtr);

    RHDMCRestore(rhdPtr);

    rhdRestoreCursor(pScrn);

    RHDPLLsRestore(rhdPtr);
    //RHDAudioRestore(rhdPtr);
    RHDLUTsRestore(rhdPtr);

    RHDVGARestore(rhdPtr);

    /* restore after restoring CRTCs - check rhd_crtc.c for why */
    RHDCrtcRestore(rhdPtr->Crtc[0]);
    RHDCrtcRestore(rhdPtr->Crtc[1]);

    RHDPmRestore(rhdPtr);

    RHDOutputsRestore(rhdPtr);
#ifdef ATOM_BIOS
    RHDRestoreBiosScratchRegisters(rhdPtr, rhdPtr->BIOSScratch);
#endif
}

CARD32
myRegRead(pointer MMIOBase, CARD16 offset)
{
	return *(volatile CARD32 *)((CARD8 *) (MMIOBase) + offset);
}

void
myRegWrite(pointer MMIOBase, CARD16 offset, CARD32 value)
{
	*(volatile CARD32 *)((CARD8 *) (MMIOBase) + offset) = value;
}

#ifdef RHD_DEBUG
/*
 *
 */
CARD32
_RHDRegReadD(int scrnIndex, CARD16 offset)
{
    CARD32 tmp =  MMIO_IN32(RHDPTR(xf86Screens[scrnIndex])->MMIOBase, offset);
    LOG("RHDRegRead(0x%4.4x) = 0x%4.4x\n",offset,tmp);
    return tmp;
}

/*
 *
 */
void
_RHDRegWriteD(int scrnIndex, CARD16 offset, CARD32 value)
{
    LOG("RHDRegWrite(0x%4.4x,0x%4.4x)\n",offset,tmp);
    MMIO_OUT32(RHDPTR(xf86Screens[scrnIndex])->MMIOBase, offset, value);
}

/*
 *
 */
void
_RHDRegMaskD(int scrnIndex, CARD16 offset, CARD32 value, CARD32 mask)
{
    CARD32 tmp;

    tmp = _RHDRegReadD(scrnIndex, offset);
    tmp &= ~mask;
    tmp |= (value & mask);
    _RHDRegWriteD(scrnIndex, offset, tmp);
}
#endif /* RHD_DEBUG */

/* The following two are R5XX only. R6XX doesn't require these */
CARD32
_RHDReadMC(int scrnIndex, CARD32 addr)
{
    RHDPtr rhdPtr = RHDPTR(xf86Screens[scrnIndex]);
    CARD32 ret = 0;

    if (rhdPtr->ChipSet < RHD_RS600) {
	RHDRegWrite(rhdPtr, MC_IND_INDEX, addr);
	ret = RHDRegRead(rhdPtr, MC_IND_DATA);
    } else if (rhdPtr->ChipSet == RHD_RS600) {
	RHDRegWrite(rhdPtr, RS600_MC_INDEX, ((addr & RS600_MC_INDEX_ADDR_MASK) | RS600_MC_INDEX_CITF_ARB0));
	ret = RHDRegRead(rhdPtr, RS600_MC_DATA);
    } else if (rhdPtr->ChipSet == RHD_RS690 || rhdPtr->ChipSet == RHD_RS740) {
	RHDRegWrite(rhdPtr, RS690_MC_INDEX, (addr & RS690_MC_INDEX_ADDR_MASK));
        ret = RHDRegRead(rhdPtr, RS690_MC_DATA);
    } else if (rhdPtr->ChipSet == RHD_RS780 || rhdPtr->ChipSet == RHD_RS880) {
	RHDRegWrite(rhdPtr, RS780_MC_INDEX, (addr & RS780_MC_INDEX_ADDR_MASK));
	ret = RHDRegRead(rhdPtr, RS780_MC_DATA);
    } else {
	LOG("%s: shouldn't be here\n", __func__);
    }
#ifdef RHD_DEBUG
    LOG("%s(0x%08X) = 0x%08X\n",__func__,(unsigned int)addr,
	     (unsigned int)ret);
#endif
    return ret;
}

void
_RHDWriteMC(int scrnIndex, CARD32 addr, CARD32 data)
{
    RHDPtr rhdPtr = RHDPTR(xf86Screens[scrnIndex]);

#ifdef RHD_DEBUG
    LOG("%s(0x%08X, 0x%08X)\n",__func__,(unsigned int)addr,
	     (unsigned int)data);
#endif

    if (rhdPtr->ChipSet < RHD_RS600) {
	RHDRegWrite(rhdPtr, MC_IND_INDEX, addr | MC_IND_WR_EN);
	RHDRegWrite(rhdPtr, MC_IND_DATA, data);
    } else if (rhdPtr->ChipSet == RHD_RS600) {
        RHDRegWrite(rhdPtr, RS600_MC_INDEX, ((addr & RS600_MC_INDEX_ADDR_MASK) | RS600_MC_INDEX_CITF_ARB0 | RS600_MC_INDEX_WR_EN));
        RHDRegWrite(rhdPtr, RS600_MC_DATA, data);
    } else if (rhdPtr->ChipSet == RHD_RS690 || rhdPtr->ChipSet == RHD_RS740) {
	RHDRegWrite(rhdPtr, RS690_MC_INDEX, ((addr & RS690_MC_INDEX_ADDR_MASK) | RS690_MC_INDEX_WR_EN));
        RHDRegWrite(rhdPtr, RS690_MC_DATA, data);
        RHDRegWrite(rhdPtr, RS690_MC_INDEX, RS690_MC_INDEX_WR_ACK);
    } else if (rhdPtr->ChipSet == RHD_RS780 || rhdPtr->ChipSet == RHD_RS880) {
	RHDRegWrite(rhdPtr, RS780_MC_INDEX, ((addr & RS780_MC_INDEX_ADDR_MASK) | RS780_MC_INDEX_WR_EN));
        RHDRegWrite(rhdPtr, RS780_MC_DATA, data);
    } else {
	LOG("%s: shouldn't be here\n", __func__);
    }
}

CARD32
_RHDReadPLL(int scrnIndex, CARD16 offset)
{
    RHDPtr rhdPtr = RHDPTR(xf86Screens[scrnIndex]);
    RHDRegWrite(rhdPtr, CLOCK_CNTL_INDEX, (offset & PLL_ADDR));
    return RHDRegRead(rhdPtr, CLOCK_CNTL_DATA);
}

void
_RHDWritePLL(int scrnIndex, CARD16 offset, CARD32 data)
{
    RHDPtr rhdPtr = RHDPTR(xf86Screens[scrnIndex]);
    RHDRegWrite(rhdPtr, CLOCK_CNTL_INDEX, (offset & PLL_ADDR) | PLL_WR_EN);
    RHDRegWrite(rhdPtr, CLOCK_CNTL_DATA, data);
}

/*
 *  rhdDoReadPCIBios(): do the actual reading, return size and copy in ptr
 */
static unsigned int
rhdDoReadPCIBios(RHDPtr rhdPtr, unsigned char **ptr)
{
	return 0;
	/*
    unsigned int size = 1 << rhdPtr->PciInfo->biosSize;
    int read_len;

    if (!(*ptr = xcalloc(1, size))) {
	LOG("Cannot allocate %d bytes of memory "
		   "for BIOS image\n",size);
	return 0;
    }
    LOG("Getting BIOS copy from PCI ROM\n");

    if ((read_len =
	 xf86ReadPciBIOS(0, rhdPtr->PciTag, -1, *ptr, size)) <= 0) {
	LOG("Cannot read BIOS image\n");
	xfree(*ptr);
	return 0;
    } else if ((unsigned int)read_len != size) {
	LOG("Read only %d of %d bytes of BIOS image\n",
		   read_len, size);
	return (unsigned int)read_len;
    }

    return size;
	 */
}

#define regw32(addr, value) RHDRegWrite(rhdPtr, (addr), (value))
#define regr32(addr) RHDRegRead(rhdPtr, (addr))
typedef enum {
	ROM_ACCESS_ENABLE			= 0,
	ROM_ACCESS_DIABLE			= 1
} ROM_ACCESS_MODE;

static void accessROM(RHDPtr rhdPtr, ROM_ACCESS_MODE rMode) {
	//x2400 x2600 x3800 x4800
	if (rMode == ROM_ACCESS_ENABLE) {
		regw32(0x1600, 0x14030302);
		regw32(0x1798, 0x21);
		regw32(0x17A0, 0x21);
		regw32(0x179C, 0x80000);
		regw32(0x17A0, 0x80621);
		regw32(0x1798, 0x80721);
		regw32(0x1798, 0x21);
		regw32(0x17A0, 0x21);
		regw32(0x179C, 0);
		regw32(0x1604, 0x40E9FC);
		regw32(0x161C, 0);
		regw32(0x1620, 0x9F);
		regw32(0x1618, 0x40004);
		regw32(0x161C, 0);
		regw32(0x1604, 0xE9FC);
		regw32(0x179C, 0x80000);
		regw32(0x1798, 0x80721);
		regw32(0x17A0, 0x80621);
		regw32(0x1798, 0x21);
		regw32(0x17A0, 0x21);
		regw32(0x179C, 0);
	} else if (rMode == ROM_ACCESS_DIABLE) {
		regw32(0x179C, 0x80000);
		regw32(0x1798, 0x80721);
		regw32(0x17A0, 0x80621);
		regw32(0x1600, 0x14030300);
		regw32(0x1798, 0x21);
		regw32(0x17A0, 0x21);
		regw32(0x179C, 0);
		regw32(0x17A0, 0x21);
		regw32(0x1798, 0x21);
		regw32(0x1798, 0x21);
	}
}

static unsigned int readATOMBIOS(RHDPtr rhdPtr, unsigned char **ptr) {
	//x2400 x2600 x3800 x4800
	accessROM(rhdPtr, ROM_ACCESS_ENABLE);
	//regw32(0xA8, 0);	//check if the value is aa55
	//regr32(0xAC);
	//regw32(0xA8, 0);
	//regr32(0xAC);
	*ptr = (unsigned char *)IOMalloc(0x10000);
	if (*ptr == NULL) return 0;
	int i;
	for (i = 0;i < 0x10000;i += 4){
		regw32(0xA8, i);
		*(UInt32 *)(*ptr + i) = regr32(0xAC);
	}
	unsigned int ret = 0x10000;
	accessROM(rhdPtr, ROM_ACCESS_DIABLE);
	return ret;
}

/*
 * rhdR5XXDoReadPCIBios(): enables access to R5xx BIOS, wraps rhdDoReadPCIBios()
 */
unsigned int
RHDReadPCIBios(RHDPtr rhdPtr, unsigned char **ptr)
{
    unsigned int ret;
    if (rhdPtr->ChipSet >= RHD_R600) return readATOMBIOS(rhdPtr, ptr);
	else {
		CARD32 save_seprom_cntl1 = 0,
		save_gpiopad_a, save_gpiopad_en, save_gpiopad_mask,
		save_viph_cntl,
		save_bus_cntl,
		save_d1vga_control, save_d2vga_control, save_vga_render_control,
		save_rom_cntl = 0,
		save_gen_pwrmgt = 0,
		save_low_vid_lower_gpio_cntl = 0, save_med_vid_lower_gpio_cntl = 0,
		save_high_vid_lower_gpio_cntl = 0, save_ctxsw_vid_lower_gpio_cntl = 0,
		save_lower_gpio_en = 0;
		
		if (rhdPtr->ChipSet < RHD_R600)
			save_seprom_cntl1 = RHDRegRead(rhdPtr, SEPROM_CNTL1);
		save_gpiopad_en = RHDRegRead(rhdPtr, GPIOPAD_EN);
		save_gpiopad_a = RHDRegRead(rhdPtr, GPIOPAD_A);
		save_gpiopad_mask = RHDRegRead(rhdPtr, GPIOPAD_MASK);
		save_viph_cntl = RHDRegRead(rhdPtr, VIPH_CONTROL);
		save_bus_cntl = RHDRegRead(rhdPtr, BUS_CNTL);
		save_d1vga_control = RHDRegRead(rhdPtr, D1VGA_CONTROL);
		save_d2vga_control = RHDRegRead(rhdPtr, D2VGA_CONTROL);
		save_vga_render_control = RHDRegRead(rhdPtr, VGA_RENDER_CONTROL);
		if (rhdPtr->ChipSet >= RHD_R600) {
			save_rom_cntl                  = RHDRegRead(rhdPtr, ROM_CNTL);
			save_gen_pwrmgt                = RHDRegRead(rhdPtr, GENERAL_PWRMGT);
			save_low_vid_lower_gpio_cntl   = RHDRegRead(rhdPtr, LOW_VID_LOWER_GPIO_CNTL);
			save_med_vid_lower_gpio_cntl   = RHDRegRead(rhdPtr, MEDIUM_VID_LOWER_GPIO_CNTL);
			save_high_vid_lower_gpio_cntl  = RHDRegRead(rhdPtr, HIGH_VID_LOWER_GPIO_CNTL);
			save_ctxsw_vid_lower_gpio_cntl = RHDRegRead(rhdPtr, CTXSW_VID_LOWER_GPIO_CNTL);
			save_lower_gpio_en             = RHDRegRead(rhdPtr, LOWER_GPIO_ENABLE);
		}
		
		/* Set SPI ROM prescale value to change the SCK period */
		if (rhdPtr->ChipSet < RHD_R600)
			RHDRegMask(rhdPtr, SEPROM_CNTL1, 0x0C << 24, SCK_PRESCALE);
		/* Let chip control GPIO pads - this is the default state after power up */
		RHDRegWrite(rhdPtr, GPIOPAD_EN, 0);
		RHDRegWrite(rhdPtr, GPIOPAD_A, 0);
		/* Put GPIO pads in read mode */
		RHDRegWrite(rhdPtr, GPIOPAD_MASK, 0);
		/* Disable VIP Host port */
		RHDRegMask(rhdPtr, VIPH_CONTROL, 0, VIPH_EN);
		/* Enable BIOS ROM */
		RHDRegMask(rhdPtr, BUS_CNTL, 0, BIOS_ROM_DIS);
		/* Disable VGA and select extended timings */
		RHDRegMask(rhdPtr, D1VGA_CONTROL, 0,
				   D1VGA_MODE_ENABLE | D1VGA_TIMING_SELECT);
		RHDRegMask(rhdPtr, D2VGA_CONTROL, 0,
				   D2VGA_MODE_ENABLE | D2VGA_TIMING_SELECT);
		RHDRegMask(rhdPtr, VGA_RENDER_CONTROL, 0, VGA_VSTATUS_CNTL);
		if (rhdPtr->ChipSet >= RHD_R600) {
			RHDRegMask(rhdPtr, ROM_CNTL, SCK_OVERWRITE
					   | 1 << SCK_PRESCALE_CRYSTAL_CLK_SHIFT,
					   SCK_OVERWRITE
					   | 1 << SCK_PRESCALE_CRYSTAL_CLK_SHIFT);
			RHDRegMask(rhdPtr, GENERAL_PWRMGT, 0, OPEN_DRAIN_PADS);
			RHDRegMask(rhdPtr, LOW_VID_LOWER_GPIO_CNTL, 0, 0x400);
			RHDRegMask(rhdPtr, MEDIUM_VID_LOWER_GPIO_CNTL, 0, 0x400);
			RHDRegMask(rhdPtr, HIGH_VID_LOWER_GPIO_CNTL, 0, 0x400);
			RHDRegMask(rhdPtr, CTXSW_VID_LOWER_GPIO_CNTL, 0, 0x400);
			RHDRegMask(rhdPtr, LOWER_GPIO_ENABLE, 0x400, 0x400);
		}
		
		ret = rhdDoReadPCIBios(rhdPtr, ptr);
		
		if (rhdPtr->ChipSet < RHD_R600)
			RHDRegWrite(rhdPtr, SEPROM_CNTL1, save_seprom_cntl1);
		RHDRegWrite(rhdPtr, GPIOPAD_EN, save_gpiopad_en);
		RHDRegWrite(rhdPtr, GPIOPAD_A, save_gpiopad_a);
		RHDRegWrite(rhdPtr, GPIOPAD_MASK, save_gpiopad_mask);
		RHDRegWrite(rhdPtr, VIPH_CONTROL, save_viph_cntl);
		RHDRegWrite(rhdPtr, BUS_CNTL, save_bus_cntl);
		RHDRegWrite(rhdPtr, D1VGA_CONTROL, save_d1vga_control);
		RHDRegWrite(rhdPtr, D2VGA_CONTROL, save_d2vga_control);
		RHDRegWrite(rhdPtr, VGA_RENDER_CONTROL, save_vga_render_control);
		if (rhdPtr->ChipSet >= RHD_R600) {
			RHDRegWrite(rhdPtr, ROM_CNTL, save_rom_cntl);
			RHDRegWrite(rhdPtr, GENERAL_PWRMGT, save_gen_pwrmgt);
			RHDRegWrite(rhdPtr, LOW_VID_LOWER_GPIO_CNTL, save_low_vid_lower_gpio_cntl);
			RHDRegWrite(rhdPtr, MEDIUM_VID_LOWER_GPIO_CNTL, save_med_vid_lower_gpio_cntl);
			RHDRegWrite(rhdPtr, HIGH_VID_LOWER_GPIO_CNTL, save_high_vid_lower_gpio_cntl);
			RHDRegWrite(rhdPtr, CTXSW_VID_LOWER_GPIO_CNTL, save_ctxsw_vid_lower_gpio_cntl);
			RHDRegWrite(rhdPtr, LOWER_GPIO_ENABLE, save_lower_gpio_en);
		}
	}
    return ret;
}

/*
 *
 */ /*
static void
rhdGetIGPNorthBridgeInfo(RHDPtr rhdPtr)
{
    switch (rhdPtr->ChipSet) {
	case RHD_RS600:
	    break;
	case RHD_RS690:
	case RHD_RS740:
	case RHD_RS780:
	    rhdPtr->NBPciTag = pciTag(0,0,0);

		break;
	default:
	    break;
    }
}
*/

/*
 *
 */
#define PCI_CMD_STAT_REG		0x04

static enum rhdCardType
rhdGetCardType(RHDPtr rhdPtr)
{
    uint32_t cmd_stat;

    if (rhdPtr->ChipSet == RHD_RS780)
	return RHD_CARD_PCIE;

    cmd_stat = pciReadLong(rhdPtr->PciTag, PCI_CMD_STAT_REG);

    if (cmd_stat & 0x100000) {
        uint32_t cap_ptr, cap_id;

	cap_ptr = pciReadLong(rhdPtr->PciTag, 0x34);

        cap_ptr &= 0xfc;

        while (cap_ptr) {

			cap_id = pciReadLong(rhdPtr->PciTag, cap_ptr);

			switch (cap_id & 0xff) {
	    case RHD_PCI_CAPID_AGP:
		LOG("AGP Card Detected\n");
		return RHD_CARD_AGP;
	    case RHD_PCI_CAPID_PCIE:
		LOG("PCIE Card Detected\n");
		return RHD_CARD_PCIE;
	    }
            cap_ptr = (cap_id >> 8) & 0xff;
        }
    }
    return RHD_CARD_NONE;
}

/* Allocate a chunk of the framebuffer. -1 on fail. So far no free()! */
unsigned int RHDAllocFb(RHDPtr rhdPtr, unsigned int size, const char *name)
{
    unsigned int chunk;
    size = RHD_FB_CHUNK(size);
    if (rhdPtr->FbFreeSize < size) {
	LOG("FB: Failed allocating %s (%d KB)\n", name, size/1024);
	return -1;
    }
    chunk = rhdPtr->FbFreeStart;
    rhdPtr->FbFreeStart += size;
    rhdPtr->FbFreeSize  -= size;
    LOG("FB: Allocated %s at offset 0x%08X (size = 0x%08X)\n",
	       name, chunk, size);
    return chunk;
}

//reverse engineered code
Bool isDisplayEnabled(RHDPtr rhdPtr, UInt8 index) {
	if (index == 0) return (RHDRegRead(rhdPtr, D1CRTC_CONTROL) & 1);
	if (index == 1) return (RHDRegRead(rhdPtr, D2CRTC_CONTROL) & 1);
	return TRUE;
}

void WaitForVBL(RHDPtr rhdPtr, UInt8 index, Bool noWait) {
	UInt32 crtcStatusReg;
	uint64_t deadline, now;
	
	if (index > 2) return;
	if (!isDisplayEnabled(rhdPtr, index)) return;
	crtcStatusReg = (index)?D2CRTC_STATUS:D1CRTC_STATUS;
	if (!noWait) {
		clock_interval_to_deadline(80, 1000000, &deadline);	//80ms
		while(RHDRegRead(rhdPtr, crtcStatusReg) & 1) {
			clock_get_uptime(&now);
			if (now > deadline) break;
		}
	}
	clock_interval_to_deadline(80, 1000000, &deadline);
	while(!(RHDRegRead(rhdPtr, crtcStatusReg) & 1)) {
		clock_get_uptime(&now);
		if (now > deadline) break;
	}
}

//Interface added by Dong

Bool RadeonHDPreInit(ScrnInfoPtr pScrn, RegEntryIDPtr pciTag, RHDMemoryMap *pMemory, pciVideoPtr PciInfo, UserOptions *options) {
	pScrn->PciTag = pciTag;
	pScrn->PciInfo = PciInfo;
	pScrn->options = options;
	pScrn->memPhysBase = pMemory->FbPhysBase;
	pScrn->fbOffset = 0;	//scanout offset
	pScrn->bitsPerPixel = pMemory->bitsPerPixel;
	pScrn->bitsPerComponent = pMemory->bitsPerComponent;
	pScrn->colorFormat = pMemory->colorFormat;
	pScrn->depth = pScrn->bitsPerPixel;
	pScrn->memoryMap = pMemory;
	
	if (pScrn->memoryMap->EDID_Block) {
		LOGV("EDID data (size %d) provided by user:\n", pScrn->memoryMap->EDID_Length);
		LOGV("%02x: 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F", 0);
		int i;
		for (i = 0; i < pScrn->memoryMap->EDID_Length; i++)
		{
			if (0 == (i & 15)) LOGV("\nRadeonHD: %02x: ", i);
			LOGV("%02x ", pScrn->memoryMap->EDID_Block[i]);
		}
		LOGV("\n");
	}
	
	return RHDPreInit(pScrn);
}

void RadeonHDFreeScrn(ScrnInfoPtr pScrn) {
	if (pScrn)
	{
		RHDFreeRec(pScrn);
		IODelete(pScrn, ScrnInfoRec, 1);
		pScrn = NULL;
	}
}

Bool RadeonHDSetMode(ScrnInfoPtr pScrn, UInt32 modeID, UInt16 depth) {
	DisplayModePtr mode = pScrn->modes;
	while (mode) {
		if (mode->modeID == modeID) break;
		mode = mode->next;
	}
	if (!mode) return FALSE;
	if (!pScrn->vtSema) return RHDScreenInit(pScrn, mode);	//first time
	else return RHDSwitchMode(pScrn, mode);
}

Bool RadeonHDGetSetBKSV(UInt32 *value, Bool set) {
	ScrnInfoPtr pScrn;
	RHDPtr rhdPtr;
	struct rhdOutput *Output;
	
	pScrn = xf86Screens[0];
	if (!pScrn) return FALSE;
	rhdPtr = RHDPTR(pScrn);
	
	Output = rhdPtr->Outputs;
    while (Output) {
		if (Output->Active && Output->Property) {
			if (set)
				Output->Property(Output, rhdPropertySet, RHD_OUTPUT_BACKLIGHT, (union rhdPropertyData *)value);
			else
				Output->Property(Output, rhdPropertyGet, RHD_OUTPUT_BACKLIGHT, (union rhdPropertyData *)value);
			return TRUE;
		}
		Output = Output->Next;
    }
	return FALSE;
}
