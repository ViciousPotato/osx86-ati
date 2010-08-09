/*
 * Copyright 2007  Luc Verhaegen <libv@exsuse.de>
 * Copyright 2007  Matthias Hopf <mhopf@novell.com>
 * Copyright 2007  Egbert Eich   <eich@novell.com>
 * Copyright 2007  Advanced Micro Devices, Inc.
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

/*
 * Cursor handling.
 *
 * Only supports ARGB cursors.
 * Bitmap cursors are converted to ARGB internally.
 */

/* All drivers should typically include these */
#include "xf86.h"

#ifdef MACOSX_10_5
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/ndrvsupport/IOMacOSVideo.h>
#else
#include <IOKit/ndrvsupport/IONDRVLibraries.h>
#endif

//#include "xf86Cursor.h"
//#include "cursorstr.h"

/* Driver specific headers */
#include "rhd.h"
#include "rhd_cursor.h"
#include "rhd_crtc.h"
#include "rhd_regs.h"

#ifndef BITMAP_SCANLINE_PAD
#define BITMAP_SCANLINE_PAD  32
#define LOG2_BITMAP_PAD		5
#define LOG2_BYTES_PER_SCANLINE_PAD	2
#endif

#define BitmapBytePad(w) \
(((int)((w) + BITMAP_SCANLINE_PAD - 1) >> LOG2_BITMAP_PAD) << LOG2_BYTES_PER_SCANLINE_PAD)

/* Temp space for storing hardwareCursorData */
static CARD32 *tempCursorImage = NULL;

/*
 * Bit-banging ONLY
 */

/* RadeonHD registers are double buffered, exchange only during vertical blank.
 * By locking registers, a set of registers is updated atomically.
 * Probably not necessary for cursors, but trivial and fast. */
static void
lockCursor(struct rhdCursor *Cursor, Bool Lock)
{
    /* Locking disables double buffering of HW cursor registers.
     * Set D*CURSOR_UPDATE_LOCK bit to 1 to lock.
     * We want *_DISABLE_MULTIPLE_UPDATE to always be 0, and since all other
     * meaningful bits are read-only for D*CUR_UPDATE registers, it is safe
     * to use RHDRegWrite() instead of RHDRegMask(); the latter is slower.
     */
    if (Lock)
	RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_UPDATE, 0x00010000);
    else
	RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_UPDATE, 0x00000000);
}

/* RadeonHD has hardware support for hotspots, but doesn't allow negative
 * cursor coordinates. Emulated in rhdShowCursor.
 * Coordinates are absolute, not relative to visible fb portion. */
static void
setCursorPos(struct rhdCursor *Cursor, CARD32 x, CARD32 y,
	     CARD32 hotx, CARD32 hoty)
{
    /* R600 only has 13 bits, but well... */
    //ASSERT (x < 0x10000);
    //ASSERT (y < 0x10000);
    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_POSITION, x << 16 | y);
    /* Note: unknown whether hotspot may be outside width/height */
    //ASSERT (hotx < MAX_CURSOR_WIDTH);
    //ASSERT (hoty < MAX_CURSOR_HEIGHT);
    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_HOT_SPOT, hotx << 16 | hoty);
}

static void
setCursorSize(struct rhdCursor *Cursor, CARD32 width, CARD32 height)
{
    //ASSERT ((width  > 0) && (width  <= MAX_CURSOR_WIDTH));
    //ASSERT ((height > 0) && (height <= MAX_CURSOR_HEIGHT));
    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_SIZE,
		(width - 1) << 16 | (height - 1));
}

static void
enableCursor(struct rhdCursor *Cursor, Bool Enable)
{
	UInt32 data = RHDRegRead(Cursor, Cursor->RegOffset + D1CUR_CONTROL);
    /* Make sure mode stays the same even when disabled; bug #13405 */
    if (Enable)
	/* pre-multiplied ARGB, Enable */	//osx use unmultiplied ARGB
		RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_CONTROL, (data & 0xFFFFFCFE) | 0x301);//0x00000201);
    else
		RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_CONTROL, (data & 0xFFFFFCFE) | 0x300);//0x00000200);
}

/* Activate already uploaded cursor image. */
static void
setCursorImage(struct rhdCursor *Cursor)
{
    //RHDPtr rhdPtr = RHDPTRI(Cursor);

    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_SURFACE_ADDRESS,
		/*rhdPtr->FbIntAddress + */Cursor->Base);
}

/* Upload image.
 * Hardware only supports 64-wide cursor images.
 * img: (MAX_CURSOR_WIDTH * height) ARGB tuples */
static void
uploadCursorImage(struct rhdCursor *Cursor, CARD32 *img)
{
    RHDPtr rhdPtr = RHDPTRI(Cursor);

    memcpy(((CARD8 *) rhdPtr->FbBase + Cursor->Base), img,
	   MAX_CURSOR_WIDTH * MAX_CURSOR_HEIGHT * 4);
}

static void
saveCursor(struct rhdCursor *Cursor)
{
    ScrnInfoPtr pScrn  = xf86Screens[Cursor->scrnIndex];
    RHDPtr      rhdPtr = RHDPTR(pScrn);

    RHDFUNC(Cursor);

    Cursor->StoreControl  = RHDRegRead(Cursor, Cursor->RegOffset
				       + D1CUR_CONTROL);
    Cursor->StoreOffset   = RHDRegRead(Cursor, Cursor->RegOffset
				       + D1CUR_SURFACE_ADDRESS)
			    - rhdPtr->FbIntAddress;
    Cursor->StoreSize     = RHDRegRead(Cursor, Cursor->RegOffset
				       + D1CUR_SIZE);
    Cursor->StorePosition = RHDRegRead(Cursor, Cursor->RegOffset
				       + D1CUR_POSITION);
    Cursor->StoreHotSpot  = RHDRegRead(Cursor, Cursor->RegOffset
				       + D1CUR_HOT_SPOT);

    Cursor->Stored = TRUE;
}

static void
restoreCursor(struct rhdCursor *Cursor)
{
    RHDPtr rhdPtr = RHDPTRI(Cursor);
    RHDFUNC(Cursor);

    if (!Cursor->Stored) {
	LOG("%s: trying to restore "
		   "uninitialized values.\n", __func__);
	return;
    }

    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_CONTROL,
		Cursor->StoreControl);
    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_SURFACE_ADDRESS,
		Cursor->StoreOffset + rhdPtr->FbIntAddress);
    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_SIZE,
		Cursor->StoreSize);
    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_POSITION,
		Cursor->StorePosition);
    RHDRegWrite(Cursor, Cursor->RegOffset + D1CUR_HOT_SPOT,
		Cursor->StoreHotSpot);
}

/*
 * Helper functions
 */

/* Internal interface to RealizeCursor - we need width/height */
struct rhd_Cursor_Bits {
    int width, height;
    /* Cursor source bitmap follows */
    /* Cursor mask bitmap follows */
} ;

/* Convert bitmaps as defined in rhd_Cursor_Bits to ARGB tupels */
static void
convertBitsToARGB(struct rhd_Cursor_Bits *bits, CARD32 *dest,
		  CARD32 color0, CARD32 color1)
{
    CARD8 *src      = (CARD8 *) &bits[1];
    int    srcPitch = BitmapBytePad(bits->width);
    CARD8 *mask     = src + srcPitch * bits->height;
    int x, y;

    memset(dest, 0, MAX_CURSOR_WIDTH * MAX_CURSOR_HEIGHT * 4);

    for (y = 0; y < bits->height; y++) {
	CARD8  *s = src, *m = mask;
	CARD32 *d = dest;
	for (x = 0; x < bits->width; x++) {
	    if (m[x/8] & (1<<(x&7))) {
		if (s[x/8] & (1<<(x&7)))
		    *d++ = color1;
		else
		    *d++ = color0;
	    } else
		*d++ = 0;
	}
	src  += srcPitch;
	mask += srcPitch;
	dest += MAX_CURSOR_WIDTH;
    }
}

/*
 * Returns if CRTC has a visible cursor
 */
static Bool
hasVisibleCursor(struct rhdCrtc *Crtc, int X, int Y)
{
    if (((X + MAX_CURSOR_WIDTH) < Crtc->X) &&
	((Y + MAX_CURSOR_HEIGHT) < Crtc->Y))
	return FALSE;

    if ((X >= (Crtc->X + Crtc->Width)) &&
	(Y >= (Crtc->Y + Crtc->Height)))
        return FALSE;

    return TRUE;
}


/*
 * Internal Driver + Xorg Interface
 */
void
rhdShowCursor(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	if (Crtc->Active && (Crtc->scrnIndex == pScrn->scrnIndex) &&
	    hasVisibleCursor(Crtc, Crtc->Cursor->X, Crtc->Cursor->Y))
            rhdCrtcShowCursor(Crtc);
    }
}

void
rhdHideCursor(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	if (Crtc->Active && Crtc->scrnIndex == pScrn->scrnIndex) {
            rhdCrtcHideCursor(Crtc);
	}
    }
}

/* Called for saving VT cursor info */
void
rhdSaveCursor(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    RHDFUNC(pScrn);
    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	/* Even save cursor state for non-active screens */
	if (Crtc->scrnIndex == pScrn->scrnIndex)
	    saveCursor(Crtc->Cursor);
    }
}

/* Called for restoring VT cursor info */
void
rhdRestoreCursor(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    RHDFUNC(pScrn);
    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	if (Crtc->Active && Crtc->scrnIndex == pScrn->scrnIndex) {
	    struct rhdCursor *Cursor = Crtc->Cursor;

	    lockCursor   (Cursor, TRUE);
	    restoreCursor(Cursor);
	    lockCursor   (Cursor, FALSE);
	}
    }
}

/* Called for restoring Xorg cursor */
void
rhdReloadCursor(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    RHDFUNC(pScrn);
    if (! rhdPtr->CursorImage)
	return;
    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	if (Crtc->scrnIndex == pScrn->scrnIndex) {
            rhdCrtcLoadCursorARGB(Crtc, rhdPtr->CursorImage);
	}
    }
}

/*
 * Xorg Interface
 */
static void
rhdSetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	/* Cursor here is relative to frame. */
	if (Crtc->Active && (Crtc->scrnIndex == pScrn->scrnIndex) &&
	    hasVisibleCursor(Crtc, x + pScrn->frameX0, y + pScrn->frameY0))
	    rhdCrtcSetCursorPosition(Crtc, x + pScrn->frameX0, y + pScrn->frameY0);
    }
}

static void
rhdSetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    rhdPtr->CursorColor0 = bg | 0xff000000;
    rhdPtr->CursorColor1 = fg | 0xff000000;

    if (!rhdPtr->CursorBits)
	return;

    /* Re-convert cursor bits if color changed */
    convertBitsToARGB(rhdPtr->CursorBits,   rhdPtr->CursorImage,
		      rhdPtr->CursorColor0, rhdPtr->CursorColor1);

    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	if (Crtc->scrnIndex == pScrn->scrnIndex) {
            rhdCrtcLoadCursorARGB(Crtc, rhdPtr->CursorImage);
	}
    }
}

static void
rhdLoadCursorImage(ScrnInfoPtr pScrn, unsigned char *src)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    struct rhd_Cursor_Bits *bits = (struct rhd_Cursor_Bits *) src;
    int i;

    rhdPtr->CursorBits   = bits;
    convertBitsToARGB(bits, rhdPtr->CursorImage,
		      rhdPtr->CursorColor0, rhdPtr->CursorColor1);

    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	if (Crtc->scrnIndex == pScrn->scrnIndex) {
            rhdCrtcLoadCursorARGB(Crtc, rhdPtr->CursorImage);
	}
    }
}

static Bool
rhdUseHWCursor(struct rhd_Cursor_Bits *cur)
{
    /* Inconsistency in interface: UseHWCursor == NULL is trivial accept,
     * UseHWCursorARGB == NULL is trivial reject. */
    if (cur->width <= MAX_CURSOR_WIDTH &&
	cur->height <= MAX_CURSOR_HEIGHT)
	return TRUE;
    return FALSE;
}

#ifdef ARGB_CURSOR
static void
rhdLoadCursorARGB(ScrnInfoPtr pScrn, CursorPtr cur)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    int i;

    rhdPtr->CursorBits   = NULL;

    /* Hardware only supports 64-wide cursor images. */
    memset(rhdPtr->CursorImage, 0, MAX_CURSOR_WIDTH * MAX_CURSOR_HEIGHT * 4);
    for (i = 0; i < cur->bits->height; i++) {
	CARD32 *img = rhdPtr->CursorImage + MAX_CURSOR_WIDTH*i;
	memcpy(img,
	       cur->bits->argb + cur->bits->width*i,
	       cur->bits->width*4);
    }

    for (i = 0; i < 2; i++) {
	struct rhdCrtc *Crtc = rhdPtr->Crtc[i];

	if (Crtc->scrnIndex == pScrn->scrnIndex) {
            rhdCrtcLoadCursorARGB(Crtc, rhdPtr->CursorImage);
	}
    }
}
#endif
/* Save cursor parameters for later re-use */
static unsigned char*
rhdRealizeCursor(xf86CursorInfoPtr infoPtr, CursorPtr cur)
{
    int    len = BitmapBytePad(cur->bits->width) * cur->bits->height;
    struct rhd_Cursor_Bits *bits = (struct rhd_Cursor_Bits *)IOMalloc(sizeof(struct rhd_Cursor_Bits)
					  + 2*len);
	if (!bits) return NULL;
    char  *bitmap = (char *) &bits[1];

    bits->width  = cur->bits->width;
    bits->height = cur->bits->height;
    memcpy (bitmap,     cur->bits->source, len);
    memcpy (bitmap+len, cur->bits->mask,   len);

    return (unsigned char *) bits;
}

/*
 * Init
 */

void
RHDCursorsInit(RHDPtr rhdPtr)
{
    int size = RHD_FB_CHUNK(MAX_CURSOR_WIDTH * MAX_CURSOR_HEIGHT * 4);
    int i;

    RHDFUNC(rhdPtr);

    for (i = 0; i < 2; i++) {
		struct rhdCursor *Cursor = IONew(struct rhdCursor, 1);
		if (Cursor) {
			bzero(Cursor, sizeof(struct rhdCursor));
			
			Cursor->scrnIndex = rhdPtr->scrnIndex;
			
			Cursor->RegOffset = i * 0x0800;
			
			/* grab our cursor FB */
			Cursor->Base = RHDAllocFb(rhdPtr, size, "Cursor Image");
			//ASSERT(Cursor->Base != -1);
		}
		rhdPtr->Crtc[i]->Cursor = Cursor;	/* HW is fixed anyway */
    }
}

void
RHDCursorsDestroy(RHDPtr rhdPtr)
{
    int i;
    RHDFUNC(rhdPtr);

    for (i = 0; i < 2; i++) {
	if (!rhdPtr->Crtc[i] || !rhdPtr->Crtc[i]->Cursor)
	    continue;

	IODelete(rhdPtr->Crtc[i]->Cursor, struct rhdCursor, 1);
	rhdPtr->Crtc[i]->Cursor = NULL;
    }
	//Free stuff Dong
	if (rhdPtr->CursorImage) {
		IOFree(rhdPtr->CursorImage, MAX_CURSOR_WIDTH * MAX_CURSOR_HEIGHT * 4);
		rhdPtr->CursorImage = NULL;
	}
	if (rhdPtr->CursorInfo) {
		IODelete(rhdPtr->CursorInfo, xf86CursorInfoRec, 1);
		rhdPtr->CursorInfo = NULL;
	}
	
	if (tempCursorImage) {
		IOFree(tempCursorImage, MAX_CURSOR_WIDTH * MAX_CURSOR_HEIGHT * 4);
		tempCursorImage = NULL;
	}
}

Bool
RHDxf86InitCursor(ScrnInfoPtr pScrn)
{
    RHDPtr rhdPtr = RHDPTR(pScrn);
    xf86CursorInfoPtr infoPtr;

    infoPtr = IONew(xf86CursorInfoRec, 1);
    if (!infoPtr)
	return FALSE;

    infoPtr->MaxWidth  = MAX_CURSOR_WIDTH;
    infoPtr->MaxHeight = MAX_CURSOR_HEIGHT;
	/*
    infoPtr->Flags     = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
			 HARDWARE_CURSOR_UPDATE_UNHIDDEN |
			 HARDWARE_CURSOR_AND_SOURCE_WITH_MASK
#if defined (ARGB_CURSOR) && defined (HARDWARE_CURSOR_ARGB)
			 | HARDWARE_CURSOR_ARGB
#endif
			 ;
	 */
    infoPtr->SetCursorColors   = rhdSetCursorColors;
    infoPtr->SetCursorPosition = rhdSetCursorPosition;
    infoPtr->LoadCursorImage   = rhdLoadCursorImage;
    infoPtr->HideCursor        = rhdHideCursor;
    infoPtr->ShowCursor        = rhdShowCursor;
    infoPtr->UseHWCursor       = rhdUseHWCursor;
#ifdef ARGB_CURSOR
    infoPtr->UseHWCursorARGB   = rhdUseHWCursor;
    infoPtr->LoadCursorARGB    = rhdLoadCursorARGB;
#endif
    infoPtr->RealizeCursor     = rhdRealizeCursor;

	/* should be init stuff in osx 
    if (!xf86InitCursor(pScreen, infoPtr)) {
        xf86DestroyCursorInfoRec(infoPtr);
        return FALSE;
    } */
    rhdPtr->CursorInfo   = infoPtr;
    rhdPtr->CursorImage  = (CARD32 *)IOMalloc(MAX_CURSOR_WIDTH * MAX_CURSOR_HEIGHT * 4);
	if (!rhdPtr->CursorImage) return FALSE;
	
	tempCursorImage = (CARD32 *) IOMalloc(MAX_CURSOR_WIDTH * MAX_CURSOR_HEIGHT * 4);
	if (!tempCursorImage) return FALSE;
	
	RHDCursorsInit(rhdPtr);	//move it here Dong
    LOG("Using HW cursor\n");

    return TRUE;
}

/*
 *  Cursor Funcs as used by RandR
 */
void
rhdCrtcShowCursor(struct rhdCrtc *Crtc)
{
    struct rhdCursor *Cursor = Crtc->Cursor;
    lockCursor  (Cursor, TRUE);
    enableCursor(Cursor, TRUE);
    lockCursor  (Cursor, FALSE);
}

/*
 *
 */
void
rhdCrtcHideCursor(struct rhdCrtc *Crtc)
{
    struct rhdCursor *Cursor = Crtc->Cursor;

    lockCursor  (Cursor, TRUE);
    enableCursor(Cursor, FALSE);
    lockCursor  (Cursor, FALSE);
}

/*
 *
 */
void
rhdCrtcSetCursorPosition(struct rhdCrtc *Crtc, int x, int y)
{
    RHDPtr rhdPtr = RHDPTRI(Crtc);
    struct rhdCursor *Cursor = Crtc->Cursor;
    int hotx, hoty, width, cursor_end, frame_end;

    Cursor->X = x;
    Cursor->Y = y;

    hotx = 0;
    hoty = 0;

    /* Hardware doesn't allow negative cursor pos; compensate using hotspot */
    if (x < 0) {
        hotx = -x;
        x = 0;
    }
    if (y < 0) {
        hoty = -y;
        y = 0;
    }

    lockCursor   (Cursor, TRUE);

    /* Work around rare corruption cases by adjusting cursor size;
     * related to bug #13405
     * For dual-screen:
     * - Cursor's right-edge must not end on multiples of 128px.
     * - For panning, cursor image cannot horizontally extend past end of viewport.
     */
    if (rhdPtr->Crtc[0]->Active && rhdPtr->Crtc[1]->Active) {
        width      = MAX_CURSOR_WIDTH;
        cursor_end = x + width;
        frame_end  = Crtc->X   + Crtc->Width;

        if (cursor_end > frame_end) {
            width     -= cursor_end - frame_end;
            cursor_end = x + width;
        }
        if (! (cursor_end & 0x7f)) {
            width--;
        }
        /* If the cursor is effectively invisible, move it out of visible area */
        if (width <= 0) {
            width = 1;
            x = 0;
            y = Crtc->Y + Crtc->Height;
            hotx = 0;
            hoty = 0;
        }
        setCursorSize(Cursor, width, MAX_CURSOR_HEIGHT);
    }

    setCursorPos (Cursor, x, y, hotx, hoty);
    lockCursor   (Cursor, FALSE);
}

/*
 *
 */
void
rhdCrtcSetCursorColors(struct rhdCrtc *Crtc, int bg, int fg)
{
    RHDPtr rhdPtr = RHDPTRI(Crtc);

    rhdPtr->CursorColor0 = bg | 0xff000000;
    rhdPtr->CursorColor1 = fg | 0xff000000;
}

/*
 *
 */
void
rhdCrtcLoadCursorARGB(struct rhdCrtc *Crtc, CARD32 *Image)
{
    struct rhdCursor *Cursor = Crtc->Cursor;

    lockCursor       (Cursor, TRUE);
    uploadCursorImage(Cursor, Image);
    setCursorImage   (Cursor);
    setCursorSize    (Cursor, MAX_CURSOR_WIDTH, MAX_CURSOR_HEIGHT);
    lockCursor       (Cursor, FALSE);
}

// 10.5 condition
#ifdef MACOSX_10_5
#ifndef __IONDRVLIBRARIES__
typedef UInt32 *                        UInt32Ptr;

struct HardwareCursorDescriptorRec {
	UInt16              majorVersion;
	UInt16              minorVersion;
	UInt32              height;
	UInt32              width;
	UInt32              bitDepth;
	UInt32              maskBitDepth;
	UInt32              numColors;
	UInt32Ptr           colorEncodings;
	UInt32              flags;
	UInt32              supportedSpecialEncodings;
	UInt32              specialEncodings[16];
};
typedef struct HardwareCursorDescriptorRec HardwareCursorDescriptorRec;
typedef HardwareCursorDescriptorRec *   HardwareCursorDescriptorPtr;

struct IOHardwareCursorInfo {
	UInt16		majorVersion;
	UInt16		minorVersion;
	UInt32		cursorHeight;
	UInt32		cursorWidth;
	// nil or big enough for hardware's max colors
	IOColorEntry *	colorMap;
	UInt8 *		hardwareCursorData;
	UInt16		cursorHotSpotX;
	UInt16		cursorHotSpotY;
	UInt32		reserved[5];
};
typedef struct IOHardwareCursorInfo    HardwareCursorInfoRec;
typedef HardwareCursorInfoRec *         HardwareCursorInfoPtr;

extern Boolean
VSLPrepareCursorForHardwareCursor(void *                        cursorRef,
								  HardwareCursorDescriptorPtr   hardwareDescriptor,
								  HardwareCursorInfoPtr         hwCursorInfo);

#endif
#endif

// code reversed Dong
static UInt8 cursorMode = 0;
static UInt32 bitDepth = 0;
static Bool cursorSet = FALSE;
static Bool cursorVisible = FALSE;
static IOColorEntry colorMap[2] = {{0, 0, 0, 0}, {1, 0xFFFF, 0xFFFF, 0xFFFF}};
static SInt32 cursorX = 0;
static SInt32 cursorY = 0;

#define bitswap(x) (((x) << 24) & 0xFF000000) | (((x) << 8) & 0x00FF0000) | (((x) >> 8) & 0x0000FF00) | (((x) >> 24) & 0x000000FF);

//static RGBColor rgbData[2] = {{0xFFFF, 0xFFFF, 0xFFFF}, {0, 0, 0}};

static UInt32 cursorColorEncodings1[2] = {0, 1};	//black and white
static HardwareCursorDescriptorRec hardwareCursorDescriptor1 = {
	kHardwareCursorDescriptorMajorVersion,
	kHardwareCursorDescriptorMinorVersion,
	64, 
	64, 
	2,		//bitDepth
	0,
	2,		//numColors - Number of colors for indexed pixel types
	cursorColorEncodings1,		//colorEncodings - An array pointer specifying the pixel values corresponding to the indices into the color table, for indexed pixel types.
	0, 
	kTransparentEncodedPixel | kInvertingEncodedPixel,		//supportedSpecialEncodings Mask of supported special pixel values, eg. kTransparentEncodedPixel, kInvertingEncodedPixel.
	2,		//specialEncodings Array of pixel values for each supported special encoding
	3, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static HardwareCursorDescriptorRec hwcDescDirect32 =           {
	kHardwareCursorDescriptorMajorVersion,		//majorVersion
	kHardwareCursorDescriptorMinorVersion,		//minorVersion
	64,		//height
	64,		//width
	32,		//bitDepth
	0,		//maskBitDepth (unused)
	0,		//numColors - Number of colors for indexed pixel types
	0,		//colorEncodings - An array pointer specifying the pixel values corresponding to the indices into the color table, for indexed pixel types.
	0,		//flags (None defined, set to zero)
	kTransparentEncodedPixel,		//supportedSpecialEncodings Mask of supported special pixel values, eg. kTransparentEncodedPixel, kInvertingEncodedPixel.
	0,		//specialEncodings Array of pixel values for each supported special encoding
};
static HardwareCursorInfoRec hardwareCursorInfo = {kHardwareCursorDescriptorMajorVersion, kHardwareCursorDescriptorMinorVersion, };


void ProgramCrsrState(RHDPtr rhdPtr, SInt32 x, SInt32 y, Bool visible, UInt8 index) {
	UInt32 hotSpotX = 0;
	UInt32 hotSpotY = 0;
	
	if (!visible) {
		x = 0x1FFF;
		y = 0x1FFF;
	} else {
		if (x < 0) {
			hotSpotX = -x;
			x = 0;
		}
		if (y < 0) {
			hotSpotY = -y;
			y = 0;
		}
		hotSpotX = (hotSpotX > 0x3F)?0x3F:hotSpotX;
		hotSpotY = (hotSpotY > 0x3F)?0x3F:hotSpotY;
	}
	UInt32 RegOffset = rhdPtr->Crtc[index]->Cursor->RegOffset;
	UInt32 cursorAddress = /*rhdPtr->FbIntAddress + */rhdPtr->Crtc[index]->Cursor->Base;
	UInt32 value2 = (x << 16) | y;
	UInt32 value1 = (hotSpotX << 16) | hotSpotY;
	RHDRegWrite(rhdPtr, D1CUR_HOT_SPOT + RegOffset, value1);
	value1 = RHDRegRead(rhdPtr, D1CUR_CONTROL + RegOffset);
	RHDRegWrite(rhdPtr, D1CUR_POSITION + RegOffset, value2);
	value2 = (cursorMode << 8) | (value1 & 0xFFFFFCFF) | 1;	//set cursorMode and enable
	if (value2 != value1) RHDRegWrite(rhdPtr, D1CUR_CONTROL + RegOffset, value2);
	RHDRegWrite(rhdPtr, D1CUR_SURFACE_ADDRESS + RegOffset, cursorAddress);
}

static void SetCrsrState(RHDPtr rhdPtr, SInt32 x, SInt32 y, Bool visible, UInt8 index) {
	cursorVisible = FALSE;
	if ((!bitDepth || !cursorSet) && visible) return;	//no way to show crsr
	cursorVisible = visible;
	if (!visible) {
		x = 0x1FFF;
		y = 0x1FFF;
	}
	rhdCrtcSetCursorPosition(rhdPtr->Crtc[index], x, y);
	struct rhdCursor *Cursor = rhdPtr->Crtc[index]->Cursor;
	//lockCursor(Cursor, TRUE);
	setCursorImage(Cursor);
	//lockCursor(Cursor, FALSE);
}

extern UInt32 GammaCorrectARGB32(GammaTbl *gTable, UInt32 data);

Bool RadeonHDSetHardwareCursor(void *cursorRef, GammaTbl *gTable) {
	RHDPtr rhdPtr = RHDPTR(xf86Screens[0]);
	if (!rhdPtr->FbBase) return FALSE;
	
	cursorSet = FALSE;
	cursorVisible = FALSE;
	cursorMode = 0;
	bitDepth = 0;
	
	hardwareCursorInfo.colorMap = colorMap;
	hardwareCursorInfo.hardwareCursorData = (UInt8 *)tempCursorImage;
	if (VSLPrepareCursorForHardwareCursor(cursorRef, &hwcDescDirect32, &hardwareCursorInfo)) {
		bitDepth = 32;
		cursorMode = 3;
		//LOG("32 bits Cursor selected\n");
	} else {
		//Color map is needed to avoid KP: use a simple one for black and white pixels
		//IOColorEntry colors[2] = {{0, 0, 0, 0}, {1, 0xFFFF, 0xFFFF, 0xFFFF}};
		//hardwareCursorInfo.colorMap = &colors;
		if (VSLPrepareCursorForHardwareCursor(cursorRef, &hardwareCursorDescriptor1, &hardwareCursorInfo)) {
			bitDepth = 2;
			cursorMode = 1;
			//LOG("2 bits Cursor selected\n");
		} else {
			LOG("Prepare Hardware cursor failed\n");
		}
	}
	
	if (bitDepth) {
		cursorSet = TRUE;
		cursorVisible = TRUE;
	}	
	
	int i, j;
	UInt32 h = hardwareCursorInfo.cursorHeight;
	UInt32 w = hardwareCursorInfo.cursorWidth;
	UInt32 *p = (UInt32*)hardwareCursorInfo.hardwareCursorData;
	
	/** 32 bits cursor padding
	 * Fix by semantics. 
	 *
	 * The hardwareCurosrData returned is not laid out in 64x64 fashion,
	 * but in cursorWidth x cursorHeight. When copying it to the
	 * cursorImage buffer for use by hardware, proper padding should be
	 * manually added.
	 */
	if (bitDepth == 32) {
		for (i = 0; i<64; i++) {
			for (j=0; j<64; j++) {
				if (i < h && j < w) {
					rhdPtr->CursorImage[i*64+j] = p[w*i+j];
				}
				else {
					rhdPtr->CursorImage[i*64+j] = 0;
				}
				rhdPtr->CursorImage[i * 64 + j] = GammaCorrectARGB32(gTable, rhdPtr->CursorImage[i * 64 + j]);					
			}
		}
	}
	/* end of fix */
		
	/* 2 bits cursor padding
	 * Reversed code use 0x80FFFFFF and 0x80000000 for black and transparent
	 * I have to use 0xFF000000 and 0x00FFFFFF instead, did not figure out the reason yet
	 * dong
	 */
	if (bitDepth == 2) {
		UInt32 pixelData;
		int m, n, leftBits;
		
		//clear arear outside cursor
		for (i = 0;i < 64;i++)
			for (j = (i < h)?w:0;j < 64;j++)
				rhdPtr->CursorImage[i * 64 + j] = 0x00FFFFFF;

		m = 0;
		n = 0;
		for (i = 0;i < h;i++) {
			for (j = 0;j < bitDepth * w / 32;j++) {
				leftBits = 32 - bitDepth;	//var_28
				while (leftBits >= 0) {						
					UInt32 temp = p[n];
					temp = bitswap(temp);
					UInt8 mode = (temp >> leftBits) & 3;
					
					//According with "Designing PCI Card Drivers" manual, using the current cursor
					//hardware descriptor, four values can be used to draw 2 bit cursor:
					//"A cursor pixel value of 0 will display the first color in the cursor’s color map, 
					//and a pixel value of 1 will display the second color. A cursor pixel value of 2 will 
					//display the color of the screen pixel underneath the cursor. A cursor pixel value 
					//of 3 will display the inverse of the color of the screen pixel underneath the cursor."
					switch (mode) {
						case 0:
							pixelData = 0xFF000000; //Black
							break;
						case 1:
							pixelData = 0xFFFFFFFF; //White
							break;
						case 2:
							pixelData = 0x00FFFFFF; //Transparent
							break;
						case 3:
							pixelData = 0x80000000; //Transparent black (Inverted?)
							break;
						default:
							break;
					}
					/*
					if (mode == 2) pixelData = 0x00FFFFFF;
					else if (mode == 3) pixelData = 0xFF000000;
					else if (mode == 3) pixelData = 0xFF000000;
					else {
						LOG("2 bits cursor use preset rgbData (mode %d)\n", mode);	//not reached here for me dong
						mode &= 1;
						pixelData = ((rgbData[mode].red >> 8) << 16)
						| ((rgbData[mode].green >> 8) << 8)
						| (rgbData[mode].blue >> 8);
					}
					 */
					rhdPtr->CursorImage[m] = pixelData;
					m++;
					leftBits -= bitDepth;
				}
				n++;		//point to next value
			}
			m += (64 - w);	//point to next line
		}
	}
	
	UInt8 k;
	for (k = 0;k < 2;k++) {	//k = 0 is enough for me since only one head is supported
		if (!rhdPtr->Crtc[k]->Active) continue;
		if (cursorMode) rhdCrtcLoadCursorARGB(rhdPtr->Crtc[k], rhdPtr->CursorImage);
		if (!bitDepth) SetCrsrState(rhdPtr, 0, 0, FALSE, k);
	}
	if (bitDepth) return TRUE;
	else return FALSE;
}

Bool RadeonHDDrawHardwareCursor(SInt32 x, SInt32 y, Bool visible) {
	if (!cursorSet) return FALSE;
	
	RHDPtr rhdPtr = RHDPTR(xf86Screens[0]);
	cursorX = x;
	cursorY = y;
	UInt8 k;
	for (k = 0;k < 2;k++)	//k = 0 is enough for me since only one head is supported
		if (rhdPtr->Crtc[k]->Active)
			SetCrsrState(rhdPtr, x, y, visible, k);
	return TRUE;
}

void RadeonHDGetHardwareCursorState(SInt32 *x, SInt32 *y, UInt32 *set, UInt32 *visible) {
	*set = cursorSet;	//typo fix
	if (cursorSet) {
		*x = cursorX;
		*y = cursorY;
		*visible = cursorVisible;
	}
}
