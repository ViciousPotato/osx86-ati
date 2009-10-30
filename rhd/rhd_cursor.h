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

#ifndef _RHD_CURSOR_H
# define _RHD_CURSOR_H

#define MAX_CURSOR_WIDTH  64
#define MAX_CURSOR_HEIGHT 64

#define ARGB_CURSOR

/*
 * source and mask point directly to the bits, which are in the server-defined
 * bitmap format.
 */
typedef struct _CursorBits {
    unsigned char *source;			/* points to bits */
    unsigned char *mask;			/* points to bits */
    Bool emptyMask;				/* all zeros mask */
    unsigned short width, height, xhot, yhot;	/* metrics */
    int refcnt;					/* can be shared */
    //pointer devPriv[MAXSCREENS];		/* set by pScr->RealizeCursor*/
#ifdef ARGB_CURSOR
    CARD32 *argb;				/* full-color alpha blended */
#endif
} xf86CursorBits, *CursorBitsPtr;

typedef struct _Cursor {
    CursorBitsPtr bits;
    unsigned short foreRed, foreGreen, foreBlue; /* device-independent color */
    unsigned short backRed, backGreen, backBlue; /* device-independent color */
    int refcnt;
    //pointer devPriv[MAXSCREENS];		/* set by pScr->RealizeCursor*/
} CursorRec;
typedef struct _Cursor *CursorPtr;

typedef struct _xf86CursorInfoRec {
    ScrnInfoPtr pScrn;
    int Flags;
    int MaxWidth;
    int MaxHeight;
    void (*SetCursorColors)(ScrnInfoPtr pScrn, int bg, int fg);
    void (*SetCursorPosition)(ScrnInfoPtr pScrn, int x, int y);
    void (*LoadCursorImage)(ScrnInfoPtr pScrn, unsigned char *bits);
    void (*HideCursor)(ScrnInfoPtr pScrn);
    void (*ShowCursor)(ScrnInfoPtr pScrn);
    unsigned char* (*RealizeCursor)(struct _xf86CursorInfoRec *, CursorPtr);
    Bool (*UseHWCursor)(struct rhd_Cursor_Bits *);
	
#ifdef ARGB_CURSOR
    Bool (*UseHWCursorARGB) (struct rhd_Cursor_Bits *);
    void (*LoadCursorARGB) (ScrnInfoPtr, CursorPtr);
#endif
	
} xf86CursorInfoRec, *xf86CursorInfoPtr;

/*
 *
 */
struct rhdCursor
{
    int scrnIndex;

    int RegOffset;

    int Base;

    int X;
    int Y;

    Bool Stored;

    CARD32  StoreControl;
    CARD32  StoreOffset;
    CARD32  StoreSize;
    CARD32  StorePosition;
    CARD32  StoreHotSpot;
};

void RHDCursorsInit(RHDPtr rhdPtr);
void RHDCursorsDestroy(RHDPtr rhdPtr);
Bool RHDxf86InitCursor(ScrnInfoPtr pScrn);
void rhdShowCursor(ScrnInfoPtr);
void rhdHideCursor(ScrnInfoPtr);
void rhdReloadCursor(ScrnInfoPtr pScrn);
void rhdSaveCursor(ScrnInfoPtr pScrn);
void rhdRestoreCursor(ScrnInfoPtr pScrn);

void rhdCrtcShowCursor(struct rhdCrtc *Crtc);      /* */
void rhdCrtcHideCursor(struct rhdCrtc *Crtc);      /* */
void rhdCrtcLoadCursorARGB(struct rhdCrtc *Crtc, CARD32 *Image);  /* */
void rhdCrtcSetCursorColors(struct rhdCrtc *Crtc, int bg, int fg);
void rhdCrtcSetCursorPosition(struct rhdCrtc *Crtc, int x, int y);

#endif
