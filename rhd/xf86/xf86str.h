/* $XFree86: xc/programs/Xserver/hw/xfree86/common/xf86str.h,v 1.111 2007/02/13 18:30:09 tsi Exp $ */

/*
 * Copyright (c) 1997-2007 by The XFree86 Project, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 *   1.  Redistributions of source code must retain the above copyright
 *       notice, this list of conditions, and the following disclaimer.
 *
 *   2.  Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer
 *       in the documentation and/or other materials provided with the
 *       distribution, and in the same place and form as other copyright,
 *       license and disclaimer information.
 *
 *   3.  The end-user documentation included with the redistribution,
 *       if any, must include the following acknowledgment: "This product
 *       includes software developed by The XFree86 Project, Inc
 *       (http://www.xfree86.org/) and its contributors", in the same
 *       place and form as other third-party acknowledgments.  Alternately,
 *       this acknowledgment may appear in the software itself, in the
 *       same form and location as other such third-party acknowledgments.
 *
 *   4.  Except as contained in this notice, the name of The XFree86
 *       Project, Inc shall not be used in advertising or otherwise to
 *       promote the sale, use or other dealings in this Software without
 *       prior written authorization from The XFree86 Project, Inc.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE XFREE86 PROJECT, INC OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright � 2003, 2004, 2005 David H. Dawes.
 * Copyright � 2003, 2004, 2005 X-Oz Technologies.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions, and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 * 
 *  3. The end-user documentation included with the redistribution,
 *     if any, must include the following acknowledgment: "This product
 *     includes software developed by X-Oz Technologies
 *     (http://www.x-oz.com/)."  Alternately, this acknowledgment may
 *     appear in the software itself, if and wherever such third-party
 *     acknowledgments normally appear.
 *
 *  4. Except as contained in this notice, the name of X-Oz
 *     Technologies shall not be used in advertising or otherwise to
 *     promote the sale, use or other dealings in this Software without
 *     prior written authorization from X-Oz Technologies.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL X-OZ TECHNOLOGIES OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains definitions of the public XFree86 data structures/types.
 * Any data structures that video drivers need to access should go here.
 */

#ifndef _XF86STR_H
#define _XF86STR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "xf86.h"

	typedef enum {
		X_PROBED,                   /* Value was probed */
		X_CONFIG,                   /* Value was given in the config file */
		X_DEFAULT,                  /* Value is a default */
		X_CMDLINE,                  /* Value was given on the command line */
		X_NOTICE,                   /* Notice */
		X_ERROR,                    /* Error message */
		X_WARNING,                  /* Warning message */
		X_INFO,                     /* Informational message */
		X_NONE,                     /* No prefix */
		X_NOT_IMPLEMENTED           /* Not implemented */
	} MessageType;
	
	/*
 * memType is of the size of the addressable memory (machine size)
 * usually unsigned long.
 */
typedef unsigned long memType;

/* Video mode flags */

typedef enum {
    V_PHSYNC	= 0x0001,
    V_NHSYNC	= 0x0002,
    V_PVSYNC	= 0x0004,
    V_NVSYNC	= 0x0008,
    V_INTERLACE	= 0x0010,
    V_DBLSCAN	= 0x0020,
    V_CSYNC	= 0x0040,
    V_PCSYNC	= 0x0080,
    V_NCSYNC	= 0x0100,
    V_HSKEW	= 0x0200,	/* hskew provided */
    V_BCAST	= 0x0400,
    V_PIXMUX	= 0x1000,
    V_DBLCLK	= 0x2000,
    V_CLKDIV2	= 0x4000,
	V_STRETCH	= 0x8000
} ModeFlags;

typedef enum {
    INTERLACE_HALVE_V	= 0x0001	/* Halve V values for interlacing */
} CrtcAdjustFlags;

/* Flags passed to ChipValidMode() */
typedef enum {
    MODECHECK_INITIAL = 0,
    MODECHECK_FINAL   = 1
} ModeCheckFlags;

/* These are possible return values for xf86CheckMode() and ValidMode() */
typedef enum {
    MODE_OK	= 0,	/* Mode OK */
    MODE_HSYNC,		/* hsync out of range */
    MODE_VSYNC,		/* vsync out of range */
    MODE_H_ILLEGAL,	/* mode has illegal horizontal timings */
    MODE_V_ILLEGAL,	/* mode has illegal horizontal timings */
    MODE_BAD_WIDTH,	/* requires an unsupported linepitch */
    MODE_NOMODE,	/* no mode with a maching name */
    MODE_NO_INTERLACE,	/* interlaced mode not supported */
    MODE_NO_DBLESCAN,	/* doublescan mode not supported */
    MODE_NO_VSCAN,	/* multiscan mode not supported */
    MODE_MEM,		/* insufficient video memory */
    MODE_VIRTUAL_X,	/* mode width too large for specified virtual size */
    MODE_VIRTUAL_Y,	/* mode height too large for specified virtual size */
    MODE_MEM_VIRT,	/* insufficient video memory given virtual size */
    MODE_NOCLOCK,	/* no fixed clock available */
    MODE_CLOCK_HIGH,	/* clock required is too high */
    MODE_CLOCK_LOW,	/* clock required is too low */
    MODE_CLOCK_RANGE,	/* clock/mode isn't in a ClockRange */
    MODE_BAD_HVALUE,	/* horizontal timing was out of range */
    MODE_BAD_VVALUE,	/* vertical timing was out of range */
    MODE_BAD_VSCAN,	/* VScan value out of range */
    MODE_HSYNC_NARROW,	/* horizontal sync too narrow */
    MODE_HSYNC_WIDE,	/* horizontal sync too wide */
    MODE_HBLANK_NARROW,	/* horizontal blanking too narrow */
    MODE_HBLANK_WIDE,	/* horizontal blanking too wide */
    MODE_VSYNC_NARROW,	/* vertical sync too narrow */
    MODE_VSYNC_WIDE,	/* vertical sync too wide */
    MODE_VBLANK_NARROW,	/* vertical blanking too narrow */
    MODE_VBLANK_WIDE,	/* vertical blanking too wide */
    MODE_PANEL,		/* exceeds panel dimensions */
    MODE_INTERLACE_WIDTH, /* width too large for interlaced mode */
    MODE_ONE_WIDTH,	/* only one width is supported */
    MODE_ONE_HEIGHT,	/* only one height is supported */
    MODE_ONE_SIZE,	/* only one resolution is supported */
    MODE_REFRESH_LOW,	/* refresh rate below the target */
    MODE_TOO_BIG,	/* larger than the preferred mode */
    MODE_PANEL_NOSCALE,	/* won't scale to panel size */
    MODE_ASPECT_RATIO,	/* aspect ratio is too large */
    MODE_BAD = -2,	/* unspecified reason */
    MODE_ERROR	= -1	/* error condition */
} ModeStatus;

# define M_T_BUILTIN  0x001        /* built-in mode */
# define M_T_CLOCK_C (0x002 | M_T_BUILTIN) /* built-in mode - configure clock */
# define M_T_CRTC_C  (0x004 | M_T_BUILTIN) /* built-in mode - configure CRTC  */
# define M_T_CLOCK_CRTC_C  (M_T_CLOCK_C | M_T_CRTC_C)
                               /* built-in mode - configure CRTC and clock */
# define M_T_DEFAULT  0x010	/* (VESA) default modes */
# define M_T_USERDEF  0x020	/* One of the modes from the config file */
# define M_T_EDID     0x040	/* Mode from EDID detailed timings data. */
# define M_T_PREFER   0x080	/* Preferred mode from EDID data. */
# define M_T_LOWPREF  0x100	/* A low preference mode. */
	
	//|---------------------------		CrtcHTotal	------------------------------->|
	//|--------------------------	CrtcHBlankEnd	------------------------------->|
	//|---------------------	CrtcHSyncEnd	-------------------->|				|
	//|------------------	CrtcHSyncStart	----------------->|		 |				|
	//|----------------	CrtcHBlankStart	--------------->|	  |		 |				|
	//|----------------	CrtcHDisplay	--------------->|	  |		 |				|
	//|													|	  |		 |				|
	//|<------------	horizontalActive	----------->|	  |		 |				|
	//|													|<--- horizontalBlanking -->|
	//|													|<hSO>|		 |				|
	//|													|	  |<hSPW>|				|
	
/* Video mode */
typedef struct _DisplayModeRec {
    struct _DisplayModeRec *	prev;
    struct _DisplayModeRec *	next;
#define MODE_NAME_LEN 20
    char			name[MODE_NAME_LEN];		/* identifier for the mode */
    ModeStatus			status;
    int				type;
    
	SInt32			modeID;
    /* These are the values that the user sees/provides */
    int				Clock;		/* pixel clock freq */
    int				HDisplay;	/* horizontal timing */
    int				HSyncStart;
    int				HSyncEnd;
    int				HTotal;
    int				HSkew;
    int				VDisplay;	/* vertical timing */
    int				VSyncStart;
    int				VSyncEnd;
    int				VTotal;
    int				VScan;
    int				Flags;

  /* These are the values the hardware uses */
    int				ClockIndex;
    int				SynthClock;	/* Actual clock freq to
					  	 * be programmed */
    int				CrtcHDisplay;
    int				CrtcHBlankStart;
    int				CrtcHSyncStart;
    int				CrtcHSyncEnd;
    int				CrtcHBlankEnd;
    int				CrtcHTotal;
    int				CrtcHSkew;
    int				CrtcVDisplay;
    int				CrtcVBlankStart;
    int				CrtcVSyncStart;
    int				CrtcVSyncEnd;
    int				CrtcVBlankEnd;
    int				CrtcVTotal;
    Bool			CrtcHAdjusted;
    Bool			CrtcVAdjusted;
    int				PrivSize;
    INT32 *			Private;
    int				PrivFlags;

    float			HSync, VRefresh;
} DisplayModeRec, *DisplayModePtr;

/* The monitor description */

#define MAX_HSYNC 8
#define MAX_VREFRESH 8

typedef struct { float hi, lo; } range;
/*
 * These are the private bus types.  New types can be added here.  Types
 * required for the public interface should be added to xf86str.h, with
 * function prototypes added to xf86.h.
 */
 
typedef struct {
    int			vendor;
    int			chipType;
    int			chipRev;
    int			subsysVendor;
    int			subsysCard;
    int			bus;
    int			device;
    int			func;
    /* int			class; */
    int			subclass;
    int			interface;
    memType		memBase[6];
    memType		ioBase[6];
    int			size[6];
    unsigned char	type[6];
    memType		biosBase;
    int			biosSize;
    pointer		thisCard;
    int			validSize;	/* was Bool, now a bit mask */
    Bool		validate;
    CARD32		listed_class;
} pciVideoRec, *pciVideoPtr;

typedef struct _MemoryMap {
    unsigned int        MMIOMapSize;
    pointer             MMIOBase; /* map base if mmio */
	unsigned int        FbMapSize;
    pointer             FbBase;   /* map base of fb   */
	unsigned long		FbPhysBase;
    unsigned char*		BIOSCopy;
	int					BIOSLength;
	int					bitsPerPixel;
	int					bitsPerComponent;
	int					colorFormat;
} RHDMemoryMap;

typedef struct {
    unsigned long type;     /* shared, exclusive, unused etc. */
    memType a;
    memType b;
} resRange, *resList;

typedef struct { 
    int numChipset;
    int PCIid;
    resRange *resList;
} PciChipsets;

	typedef struct {
		Bool		debugMode;
		Bool		enableOSXI2C;
		Bool		enableGammaTable;
		Bool		setCLUTAtSetEntries;
		Bool		UseAtomBIOS;	//need initialize it to be true, then overwrote by user
		Bool		SetIGPMemory;
		Bool		audio;
		Bool		hdmi;
		Bool		coherent;
		Bool		HWCursorSupport;
		Bool        lowPowerMode;
		int			lowPowerModeEngineClock;
		int			lowPowerModeMemoryClock;
		int			verbosity;
		char		modeNameByUser[25];	//15 should be enough
		
		unsigned char		*EDID_Block[2];
		int					EDID_Length[2];
#define outputTypeLength 25
		char				outputTypes[2][outputTypeLength];
		bool				outputChecked[2];
		bool				UseFixedModes[2];
	} UserOptions;
	
/*
 * Driver entry point types
 */
typedef struct _ScrnInfoRec *ScrnInfoPtr;

/*
 * ScrnInfoRec
 *
 * There is one of these for each screen, and it holds all the screen-specific
 * information.
 *
 * Note: the size and layout must be kept the same across versions.  New
 * fields are to be added in place of the "reserved*" fields.  No fields
 * are to be dependent on compile-time defines.
 */

typedef struct _ScrnInfoRec {
    int			scrnIndex;		/* Number of this screen */

    int			bitsPerPixel;		/* fb bpp */
	int			bitsPerComponent;
	int			colorFormat;
    int			depth;			/* depth of default visual */
	unsigned long	memPhysBase;		/* Physical address of FB */
	unsigned long 	fbOffset;		/* Offset of FB in the above */
    int			virtualX;		/* Virtual width */
    int			virtualY; 		/* Virtual height */
    int			displayWidth;		/* memory pitch */
    int			frameX0;		/* viewport position */
    int			frameY0;
    int			frameX1;
    int			frameY1;
		
    DisplayModePtr	modes;			/* list of actual modes */
    //DisplayModePtr	NativeMode;
	
    int			widthmm;		/* physical display dimensions
								 * in mm */
    int			heightmm;
    int			xDpi;			/* width DPI */
    int			yDpi;			/* height DPI */
    char *		name;			/* Name to prefix messages */
    pointer		driverPrivate;		/* Driver private area */
	
	RHDMemoryMap	*memoryMap;	//add by Dong to communicate with IOKits
	pciVideoPtr		PciInfo;
	RegEntryIDPtr	PciTag;
	UserOptions		*options;

    /* Some of these may be moved out of here into the driver private area */

    char *		chipset;		/* chipset name */
    int			videoRam;		/* amount of video ram (kb) */
	Bool		dualLink;

    /* Allow screens to be enabled/disabled individually */
    Bool		vtSema;
    
} ScrnInfoRec;
extern ScrnInfoRec *	xf86Screens[];	/* only support one video card ATM */

typedef struct {
	int		token;
	char *	name;
} SymTabRec, *SymTabPtr;

	extern const char * xf86TokenToString(SymTabPtr table, int token);

/* For DPMS */
typedef void (*DPMSSetProcPtr)(ScrnInfoPtr, int, int);
	
	//helper functions Dong
	extern UInt32 getPitch(UInt32 width, UInt32 bytesPerPixel);
	extern UInt32 HALPixelSize(SInt32 depth);
	extern UInt32 HALColorBits(SInt32 depth);
	extern UInt32 HALColorFormat(SInt32 depth);
	
#ifdef __cplusplus
}
#endif

#endif /* _XF86STR_H */
