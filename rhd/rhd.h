/*
 * Copyright 2007, 2008  Luc Verhaegen <libv@exsuse.de>
 * Copyright 2007, 2008  Matthias Hopf <mhopf@novell.com>
 * Copyright 2007, 2008  Egbert Eich   <eich@novell.com>
 * Copyright 2007, 2008  Advanced Micro Devices, Inc.
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

#ifndef _RHD_H
# define _RHD_H

#include "xf86str.h"	//have to place it here, otherwise need replace a lot xf86.h

#define RHD_NAME "RADEONHD"
#define RHD_DRIVER_NAME "radeonhd"
#define ATOM_BIOS
#define ATOM_BIOS_PARSER

enum RHD_CHIPSETS {
    RHD_UNKNOWN = 0,
    /* R500 */
    RHD_RV505,
    RHD_RV515,
    RHD_RV516,
    RHD_R520,
    RHD_RV530,
    RHD_RV535,
    RHD_RV550,
    RHD_RV560,
    RHD_RV570,
    RHD_R580,
    /* R500 Mobility */
    RHD_M52,
    RHD_M54,
    RHD_M56,
    RHD_M58,
    RHD_M62,
    RHD_M64,
    RHD_M66,
    RHD_M68,
    RHD_M71,
    /* R500 integrated */
    RHD_RS600,
    RHD_RS690,
    RHD_RS740,
    /* R600 */
    RHD_R600,
    RHD_RV610,
    RHD_RV630,
    /* R600 Mobility */
    RHD_M72,
    RHD_M74,
    RHD_M76,
    /* R600 second batch - RV670 came into existence after RV6x0 and M7x */
    RHD_RV670,
    RHD_M88,
    RHD_R680,
    RHD_RV620,
    RHD_M82,
    RHD_RV635,
    RHD_M86,
    RHD_RS780,
    RHD_RS880,
    RHD_RV770,
    /* R700 */
    RHD_R700,
    RHD_M98,
    RHD_RV730,
    RHD_M96,
    RHD_RV710,
    RHD_M92,
    RHD_M93,
    RHD_M97,
    RHD_RV790,
    RHD_RV740,
    RHD_CHIP_END
};

enum RHD_HPD_USAGE {
    RHD_HPD_USAGE_AUTO = 0,
    RHD_HPD_USAGE_OFF,
    RHD_HPD_USAGE_NORMAL,
    RHD_HPD_USAGE_SWAP,
    RHD_HPD_USAGE_AUTO_SWAP,
    RHD_HPD_USAGE_AUTO_OFF
};

enum RHD_TV_MODE {
    RHD_TV_NONE = 0,
    RHD_TV_NTSC = 1,
    RHD_TV_NTSCJ = 1 << 2,
    RHD_TV_PAL = 1 << 3,
    RHD_TV_PALM = 1 << 4,
    RHD_TV_PALCN = 1 << 5,
    RHD_TV_PALN = 1 << 6,
    RHD_TV_PAL60 = 1 << 7,
    RHD_TV_SECAM = 1 << 8,
    RHD_TV_CV = 1 << 9
};

enum rhdPropertyAction {
    rhdPropertyCheck,
    rhdPropertyGet,
    rhdPropertySet,
    rhdPropertyCommit
};

union rhdPropertyData
{
    CARD32 integer;
    char *string;
    Bool aBool;
};

#define RHD_CONNECTORS_MAX 6

/* More realistic powermanagement */
#define RHD_POWER_ON       0
#define RHD_POWER_RESET    1   /* off temporarily */
#define RHD_POWER_SHUTDOWN 2   /* long term shutdown */
#define RHD_POWER_UNKNOWN  3   /* initial state */

#define RHD_VBIOS_SIZE 0x10000

#ifndef NO_ASSERT
enum debugFlags {
    VGA_SETUP,
    MC_SETUP
};
#endif

enum rhdCardType {
    RHD_CARD_NONE,
    RHD_CARD_AGP,
    RHD_CARD_PCIE
};

enum {
    RHD_PCI_CAPID_AGP    = 0x02,
    RHD_PCI_CAPID_PCIE   = 0x10
};

typedef struct BIOSScratchOutputPrivate rhdOutputDriverPrivate;
typedef struct _rhdI2CRec *rhdI2CPtr;
typedef struct _atomBiosHandle *atomBiosHandlePtr;

#define PCITAG RegEntryIDPtr

typedef struct RHDRec {
    int                 scrnIndex;

    enum RHD_CHIPSETS   ChipSet;
    pciVideoRec         *PciInfo;
    PCITAG              PciTag;
    //PCITAG		NBPciTag;	//since 1.2.5, no need for this

    unsigned int	PciDeviceID;
    enum rhdCardType	cardType;
    int			entityIndex;
    struct rhdCard      *Card;
    Bool		UseAtomBIOS;	//need initialize it to be true, then overwrote by user
	Bool		SetIGPMemory;
    Bool		audio;
    Bool		hdmi;
    Bool		coherent;
    Bool              lowPowerMode;
    int              lowPowerModeEngineClock;
    int              lowPowerModeMemoryClock;
    enum RHD_HPD_USAGE	hpdUsage;
    unsigned int        FbMapSize;
    pointer             FbBase;   /* map base of fb   */
    unsigned int        FbPhysAddress; /* card PCI BAR address of FB */
    unsigned int        FbIntAddress; /* card internal address of FB */
    CARD32              FbIntSize; /* card internal FB aperture size */
    unsigned int        FbPCIAddress; /* physical address of FB */

    /* Some simplistic memory handling */
#define ALIGN(x,align)	(((x)+(align)-1)&~((align)-1))
#define RHD_FB_ALIGNMENT 0x1000
    /* Use this macro to always chew up 4096byte aligned pieces. */
#define RHD_FB_CHUNK(x)     ALIGN((x), RHD_FB_ALIGNMENT)
    unsigned int        FbFreeStart;
    unsigned int        FbFreeSize;

    /* visible part of the framebuffer */
    unsigned int        FbScanoutStart;
    unsigned int        FbScanoutSize;

    unsigned int        MMIOMapSize;
    pointer             MMIOBase; /* map base of mmio */
    unsigned int        MMIOPCIAddress; /* physical address of mmio */

    struct _xf86CursorInfoRec  *CursorInfo;
    struct rhd_Cursor_Bits     *CursorBits; /* ARGB if NULL */
    CARD32              CursorColor0, CursorColor1;
    CARD32             *CursorImage;

    struct _I2CBusRec	**I2C;  /* I2C bus list */
    atomBiosHandlePtr   atomBIOS; /* handle for AtomBIOS */
    /*
     * BIOS copy - kludge that should go away
     * once we know how to read PCI BIOS on
     * POSTed hardware
     */
    unsigned char*	BIOSCopy;
	unsigned int	BIOSSize;

    struct rhdMC       *MC;  /* Memory Controller */
    struct rhdVGA      *VGA; /* VGA compatibility HW */
    struct rhdCrtc     *Crtc[2];
    struct rhdPLL      *PLLs[2]; /* Pixelclock PLLs */
    //struct rhdAudio    *Audio;

    struct rhdLUTStore  *LUTStore;
    struct rhdLUT       *LUT[2];

    /* List of output devices:
     * we can go up to 5: DACA, DACB, TMDS, shared LVDS/TMDS, DVO.
     * Will also include displayport when this happens. */
    struct rhdOutput   *Outputs;

    struct rhdConnector *Connector[RHD_CONNECTORS_MAX];
    struct rhdHPD      *HPD; /* Hot plug detect subsystem */

    /* don't ignore the Monitor section of the conf file */
    //struct rhdMonitor  *ConfigMonitor;	//later may support this, but not now
    enum RHD_TV_MODE   tvMode;

    /* log verbosity - store this for convenience */
    int			verbosity;

    /* BIOS Scratch registers */
    struct rhdBiosScratchRegisters *BIOSScratch;
    /* AtomBIOS usage */

    struct rhdPm       *Pm;

    struct rhdOutput *DigEncoderOutput[2];
# define RHD_CHECKDEBUGFLAG(rhdPtr, FLAG) (rhdPtr->DebugFlags & (1 << FLAG))
#ifndef NO_ASSERT
# define RHD_SETDEBUGFLAG(rhdPtr, FLAG) (rhdPtr->DebugFlags |= (1 << FLAG))
# define RHD_UNSETDEBUGFLAG(rhdPtr, FLAG) (rhdPtr->DebugFlags &= ~((CARD32)1 << FLAG))
    CARD32 DebugFlags;
#endif
} RHDRec, *RHDPtr;

#define RHDPTR(p) 	((RHDPtr)((p)->driverPrivate))
#define RHDPTRI(p) 	(RHDPTR(xf86Screens[(p)->scrnIndex]))

#if defined(__GNUC__)
#  define NORETURN __attribute__((noreturn))
#  define CONST    __attribute__((pure))
#else
#  define NORETURN
#  define CONST
#endif


enum atomSubSystem {
    atomUsageCrtc,
    atomUsagePLL,
    atomUsageOutput,
    atomUsageAny
};

#define DPMSModeOn	0
#define DPMSModeStandby	1
#define DPMSModeSuspend	2
#define DPMSModeOff	3

struct rhdPowerState {
    /* All entries: 0 means unknown / unspecified / dontchange */
    /* Clocks in kHz, Voltage in mV */
    CARD32 EngineClock;
    CARD32 MemoryClock;
    CARD32 VDDCVoltage;
};


/* rhd_driver.c */
/* Some handy functions that makes life so much more readable */
extern Bool isDisplayEnabled(RHDPtr rhdPtr, UInt8 index);
extern void WaitForVBL(RHDPtr rhdPtr, UInt8 index, Bool disable);
extern unsigned int RHDReadPCIBios(RHDPtr rhdPtr, unsigned char **prt);
extern void RHDPrepareMode(RHDPtr rhdPtr);
extern Bool RHDUseAtom(RHDPtr rhdPtr, enum RHD_CHIPSETS *BlackList, enum atomSubSystem subsys);

extern CARD32 myRegRead(pointer MMIOBase, CARD16 offset);
extern void myRegWrite(pointer MMIOBase, CARD16 offset, CARD32 value);
#define MMIO_IN32 myRegRead
#define MMIO_OUT32 myRegWrite

#define RHDRegRead(ptr, offset) MMIO_IN32(RHDPTRI(ptr)->MMIOBase, offset)
#define RHDRegWrite(ptr, offset, value) MMIO_OUT32(RHDPTRI(ptr)->MMIOBase, offset, value)
#define RHDRegMask(ptr, offset, value, mask)	\
do {						\
    CARD32 tmp;					\
    tmp = RHDRegRead((ptr), (offset));		\
    tmp &= ~(mask);				\
    tmp |= ((value) & (mask));			\
    RHDRegWrite((ptr), (offset), tmp);		\
} while(0)

extern CARD32 _RHDReadMC(int scrnIndex, CARD32 addr);
#define RHDReadMC(ptr,addr) _RHDReadMC((ptr)->scrnIndex,(addr))
extern void _RHDWriteMC(int scrnIndex, CARD32 addr, CARD32 data);
#define RHDWriteMC(ptr,addr,value) _RHDWriteMC((ptr)->scrnIndex,(addr),(value))
extern CARD32 _RHDReadPLL(int scrnIndex, CARD16 offset);
#define RHDReadPLL(ptr, off) _RHDReadPLL((ptr)->scrnIndex,(off))
extern void _RHDWritePLL(int scrnIndex, CARD16 offset, CARD32 data);
#define RHDWritePLL(ptr, off, value) _RHDWritePLL((ptr)->scrnIndex,(off),(value))
extern unsigned int RHDAllocFb(RHDPtr rhdPtr, unsigned int size, const char *name);

/* rhd_id.c */
Bool RHDIsIGP(enum RHD_CHIPSETS chipset);

/* rhd_helper.c */
char *RhdAppendString(char *s1, const char *s2);

/* __func__ is really nice, but not universal */
#if !defined(__GNUC__) && !defined(C99)
#define __func__ "unknown"
#endif

#define LOG_DEBUG 7
#define RHDFUNC(ptr) LOGV("%s\n", __func__)
#define RHDFUNCI(scrnIndex) LOGV("%s\n", __func__)

#ifdef RHD_DEBUG
CARD32 _RHDRegReadD(int scrnIndex, CARD16 offset);
# define RHDRegReadD(ptr, offset) _RHDRegReadD((ptr)->scrnIndex, (offset))
void _RHDRegWriteD(int scrnIndex, CARD16 offset, CARD32 value);
# define RHDRegWriteD(ptr, offset, value) _RHDRegWriteD((ptr)->scrnIndex, (offset), (value))
void _RHDRegMaskD(int scrnIndex, CARD16 offset, CARD32 value, CARD32 mask);
# define RHDRegMaskD(ptr, offset, value, mask) _RHDRegMaskD((ptr)->scrnIndex, (offset), (value), (mask))
#else
# define RHDRegReadD(ptr, offset) RHDRegRead(ptr, offset)
# define RHDRegWriteD(ptr, offset, value) RHDRegWrite(ptr, offset, value)
# define RHDRegMaskD(ptr, offset, value, mask) RHDRegMask(ptr, offset, value, mask)
#endif

#endif /* _RHD_H */
