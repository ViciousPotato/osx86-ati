/*
 * Copyright 2007-2008  Luc Verhaegen <libv@exsuse.de>
 * Copyright 2007-2008  Matthias Hopf <mhopf@novell.com>
 * Copyright 2007-2008  Egbert Eich   <eich@novell.com>
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

#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/ndrvsupport/IOMacOSVideo.h>

#include "xf86.h"

#include "rhd.h"
#include "rhd_lut.h"
#include "rhd_regs.h"
#include "rhd_crtc.h"
#include "rhd_cursor.h"

//#include <compiler.h>

#define RHD_REGOFFSET_LUTA 0x000
#define RHD_REGOFFSET_LUTB 0x800

/*
 *
 */
static void
LUTxSave(struct rhdLUT *LUT)
{
    CARD16 RegOff;
    int i;
    RHDFUNC(LUT);

    if (LUT->Id == RHD_LUT_A)
	RegOff = RHD_REGOFFSET_LUTA;
    else
	RegOff = RHD_REGOFFSET_LUTB;

    LUT->StoreControl = RHDRegRead(LUT, RegOff + DC_LUTA_CONTROL);

    LUT->StoreBlackBlue = RHDRegRead(LUT, RegOff + DC_LUTA_BLACK_OFFSET_BLUE);
    LUT->StoreBlackGreen = RHDRegRead(LUT, RegOff + DC_LUTA_BLACK_OFFSET_GREEN);
    LUT->StoreBlackRed = RHDRegRead(LUT, RegOff + DC_LUTA_BLACK_OFFSET_RED);

    LUT->StoreWhiteBlue = RHDRegRead(LUT, RegOff + DC_LUTA_WHITE_OFFSET_BLUE);
    LUT->StoreWhiteGreen = RHDRegRead(LUT, RegOff + DC_LUTA_WHITE_OFFSET_GREEN);
    LUT->StoreWhiteRed = RHDRegRead(LUT, RegOff + DC_LUTA_WHITE_OFFSET_RED);

    RHDRegWrite(LUT, DC_LUT_RW_MODE, 0); /* Table */
    if (LUT->Id == RHD_LUT_A)
	RHDRegWrite(LUT, DC_LUT_READ_PIPE_SELECT, 0);
    else
	RHDRegWrite(LUT, DC_LUT_READ_PIPE_SELECT, 1);

    RHDRegWrite(LUT, DC_LUT_RW_INDEX, 0);
    for (i = 0; i < 256; i++)
	LUT->StoreEntry[i] = RHDRegRead(LUT, DC_LUT_30_COLOR);

    LUT->Stored = TRUE;
}

/*
 *
 */
static void
LUTxRestore(struct rhdLUT *LUT)
{
    CARD16 RegOff;
    int i;
    RHDFUNC(LUT);

    if (!LUT->Stored) {
	LOG("%s: %s: nothing stored!\n",
		   __func__, LUT->Name);
	return;
    }

    if (LUT->Id == RHD_LUT_A)
	RegOff = RHD_REGOFFSET_LUTA;
    else
	RegOff = RHD_REGOFFSET_LUTB;

    RHDRegWrite(LUT, RegOff + DC_LUTA_BLACK_OFFSET_BLUE, LUT->StoreBlackBlue);
    RHDRegWrite(LUT, RegOff + DC_LUTA_BLACK_OFFSET_GREEN, LUT->StoreBlackGreen);
    RHDRegWrite(LUT, RegOff + DC_LUTA_BLACK_OFFSET_RED, LUT->StoreBlackRed);

    RHDRegWrite(LUT, RegOff + DC_LUTA_WHITE_OFFSET_BLUE, LUT->StoreWhiteBlue);
    RHDRegWrite(LUT, RegOff + DC_LUTA_WHITE_OFFSET_GREEN, LUT->StoreWhiteGreen);
    RHDRegWrite(LUT, RegOff + DC_LUTA_WHITE_OFFSET_RED, LUT->StoreWhiteRed);

    if (LUT->Id == RHD_LUT_A)
	RHDRegWrite(LUT, DC_LUT_RW_SELECT, 0);
    else
	RHDRegWrite(LUT, DC_LUT_RW_SELECT, 1);

    RHDRegWrite(LUT, DC_LUT_RW_MODE, 0); /* Table */
    RHDRegWrite(LUT, DC_LUT_WRITE_EN_MASK, 0x0000003F);
    RHDRegWrite(LUT, DC_LUT_RW_INDEX, 0);
    for (i = 0; i < 256; i++)
	RHDRegWrite(LUT, DC_LUT_30_COLOR, LUT->StoreEntry[i]);

    RHDRegWrite(LUT, RegOff + DC_LUTA_CONTROL, LUT->StoreControl);
}

/*
 * Load a new LUT
 *
 * Assumes 256 rows of input. It's up to the caller to ensure there are exactly
 * 256 rows of data, as that's what the hardware exepcts.
 */
static void
rhdLUTSet(struct rhdLUT *LUT, CARD16 *red, CARD16 *green, CARD16 *blue)
{
	//UInt8 colorBits = xf86Screens[0]->bitsPerComponent;
	
    CARD16 RegOff;
    int i;

    LUT->Initialised = TRUE; /* thank you RandR */

    if (LUT->Id == RHD_LUT_A)
	RegOff = RHD_REGOFFSET_LUTA;
    else
	RegOff = RHD_REGOFFSET_LUTB;

    RHDRegWrite(LUT, RegOff + DC_LUTA_CONTROL, 0);

    RHDRegWrite(LUT, RegOff + DC_LUTA_BLACK_OFFSET_BLUE, 0);
    RHDRegWrite(LUT, RegOff + DC_LUTA_BLACK_OFFSET_GREEN, 0);
    RHDRegWrite(LUT, RegOff + DC_LUTA_BLACK_OFFSET_RED, 0);

    RHDRegWrite(LUT, RegOff + DC_LUTA_WHITE_OFFSET_BLUE, 0x0000FFFF);
    RHDRegWrite(LUT, RegOff + DC_LUTA_WHITE_OFFSET_GREEN, 0x0000FFFF);
    RHDRegWrite(LUT, RegOff + DC_LUTA_WHITE_OFFSET_RED, 0x0000FFFF);

    if (LUT->Id == RHD_LUT_A)
	RHDRegWrite(LUT, DC_LUT_RW_SELECT, 0);
    else
	RHDRegWrite(LUT, DC_LUT_RW_SELECT, 1);

    RHDRegWrite(LUT, DC_LUT_RW_MODE, 0); /* table */
    RHDRegWrite(LUT, DC_LUT_WRITE_EN_MASK, 0x0000003F);

    RHDRegWrite(LUT, DC_LUT_RW_INDEX, 0);
    for (i = 0; i < 256; i++) {
        RHDRegWrite(LUT, DC_LUT_30_COLOR,
                    ((red[i] & 0xFFC0) << 14) | ((green[i] & 0xFFC0) << 4) | (blue[i] >> 6));
    }
}

/*
 * Set specific rows of the LUT
 *
 * Assumes LUTs are already initialized to a sane state, and will only update
 * specific rows.  Use ONLY when just specific rows need to be updated.
 */
static void
rhdLUTSetRows(struct rhdLUT *LUT, int numColors, int *indices, LOCO *colors)
{
    CARD16 RegOff;
    int i, index;

    if (LUT->Id == RHD_LUT_A)
	RegOff = RHD_REGOFFSET_LUTA;
    else
	RegOff = RHD_REGOFFSET_LUTB;

    if (LUT->Id == RHD_LUT_A)
	RHDRegWrite(LUT, DC_LUT_RW_SELECT, 0);
    else
	RHDRegWrite(LUT, DC_LUT_RW_SELECT, 1);

    RHDRegWrite(LUT, DC_LUT_RW_MODE, 0); /* table */
    RHDRegWrite(LUT, DC_LUT_WRITE_EN_MASK, 0x0000003F);

    for (i = 0; i < numColors; i++) {
        index = indices[i];
        RHDRegWrite(LUT, DC_LUT_RW_INDEX, index);
        RHDRegWrite(LUT, DC_LUT_30_COLOR,
                    (colors[index].red << 20) | (colors[index].green << 10) | (colors[index].blue));
    }
}

/*
 *
 */
void
RHDLUTsInit(RHDPtr rhdPtr)
{
	static char LUTNames[2][6] = {"LUT A", "LUT B"};
    struct rhdLUT *LUT;

    RHDFUNC(rhdPtr);

    LUT = IONew(struct rhdLUT, 1);
	if (!LUT) return;
	bzero(LUT, sizeof(struct rhdLUT));

    LUT->scrnIndex = rhdPtr->scrnIndex;
    LUT->Name = LUTNames[0];
    LUT->Id = RHD_LUT_A;

    LUT->Save = LUTxSave;
    LUT->Restore = LUTxRestore;
    LUT->Set = rhdLUTSet;
    LUT->SetRows = rhdLUTSetRows;

    rhdPtr->LUT[0] = LUT;

    LUT = IONew(struct rhdLUT, 1);
	if (!LUT) return;
	bzero(LUT, sizeof(struct rhdLUT));

    LUT->scrnIndex = rhdPtr->scrnIndex;
    LUT->Name = LUTNames[1];
    LUT->Id = RHD_LUT_B;

    LUT->Save = LUTxSave;
    LUT->Restore = LUTxRestore;
    LUT->Set = rhdLUTSet;
    LUT->SetRows = rhdLUTSetRows;

    rhdPtr->LUT[1] = LUT;
}

/*
 *
 */
struct rhdLUTStore {
    CARD32 Select;
    CARD32 Mode;
    CARD32 Index;
    CARD32 Color;
    CARD32 ReadPipe;
    CARD32 WriteMask;
};

/*
 *
 */
void
RHDLUTsSave(RHDPtr rhdPtr)
{
    struct rhdLUTStore *Store = rhdPtr->LUTStore;

    RHDFUNC(rhdPtr);

    if (!Store) {
		Store = IONew(struct rhdLUTStore, 1);
		if (!Store) return;
		bzero(Store, sizeof(struct rhdLUTStore));
		rhdPtr->LUTStore = Store;
    }
	
    Store->Select = RHDRegRead(rhdPtr, DC_LUT_RW_SELECT);
    Store->Mode = RHDRegRead(rhdPtr, DC_LUT_RW_MODE);
    Store->Index = RHDRegRead(rhdPtr, DC_LUT_RW_INDEX);
    Store->Color = RHDRegRead(rhdPtr, DC_LUT_30_COLOR);
    Store->ReadPipe = RHDRegRead(rhdPtr, DC_LUT_READ_PIPE_SELECT);
    Store->WriteMask = RHDRegRead(rhdPtr, DC_LUT_WRITE_EN_MASK);

    rhdPtr->LUT[0]->Save(rhdPtr->LUT[0]);
    rhdPtr->LUT[1]->Save(rhdPtr->LUT[1]);
}

/*
 *
 */
void
RHDLUTsRestore(RHDPtr rhdPtr)
{
    struct rhdLUTStore *Store = rhdPtr->LUTStore;

    RHDFUNC(rhdPtr);

    rhdPtr->LUT[0]->Restore(rhdPtr->LUT[0]);
    rhdPtr->LUT[1]->Restore(rhdPtr->LUT[1]);

    if (!Store) {
	LOG("%s: nothing stored!\n", __func__);
	return;
    }

    RHDRegWrite(rhdPtr, DC_LUT_RW_SELECT, Store->Select);
    RHDRegWrite(rhdPtr, DC_LUT_RW_MODE, Store->Mode);
    RHDRegWrite(rhdPtr, DC_LUT_RW_INDEX, Store->Index);
    RHDRegWrite(rhdPtr, DC_LUT_30_COLOR, Store->Color);
    RHDRegWrite(rhdPtr, DC_LUT_READ_PIPE_SELECT, Store->ReadPipe);
    RHDRegWrite(rhdPtr, DC_LUT_WRITE_EN_MASK, Store->WriteMask);
}

/*
 *
 */
void
RHDLUTsDestroy(RHDPtr rhdPtr)
{
    RHDFUNC(rhdPtr);

    IODelete(rhdPtr->LUT[0], struct rhdLUT, 1);
    IODelete(rhdPtr->LUT[1], struct rhdLUT, 1);
    IODelete(rhdPtr->LUTStore, struct rhdLUTStore, 1);
}

/*
 * Workaround for missing RandR functionality. Initialise this
 * LUT with the content of the other LUT.
 */
void
RHDLUTCopyForRR(struct rhdLUT *LUT)
{
    CARD16 red[256], green[256], blue[256];
    CARD32 entry;
    int i;

    LOG("%s: %s\n", __func__, LUT->Name);

    RHDRegWrite(LUT, DC_LUT_RW_MODE, 0); /* Table */

    if (LUT->Id == RHD_LUT_A)
	RHDRegWrite(LUT, DC_LUT_READ_PIPE_SELECT, 1);
    else
	RHDRegWrite(LUT, DC_LUT_READ_PIPE_SELECT, 0);

    for (i = 0; i < 256; i++) {
        entry = RHDRegRead(LUT, DC_LUT_30_COLOR);
        red[i] = (entry >> 14) & 0xFFC0;
        green[i] = (entry >> 4) & 0xFFC0;
        blue[i] = (entry << 6) & 0xFFC0;
    }
	
	LUT->Set(LUT, red, green, blue);
}

//reversed code Dong
static float DecodeFloat16(SInt16 data) {			//get float value from a 16bits data
	float fData = 0;								//1:sign 5:exponent 10:significant bits
	long exponent = ((data >> 10) & 0x1F) - 15;		//15 biased
	if (!(data & 0x7FFF)) return (float) 0;			//data == 0
	fData = (float) (data & 0x3FF) / 1024.0 + 1.0;	//assume positive
	if (exponent > 14) exponent = 15;
	if (exponent <= -15) {
		exponent = -14;
		fData -= 1.0;
	}
	if (exponent < 0) fData = fData / (1 << -exponent);
	else fData *= (1 << exponent);
	if (data < 0) fData = -fData;
	return fData;
}

static SInt16 EncodeFloat16(float fData) {
	SInt16 part1, part2;	//var_18, var_14
	SInt32 data;			//var_2C
	
	if (fData == 0.0) return 0;
	SInt16 signMask = (fData > 0.0)?0:0x8000;	//var_E
	bcopy(&fData, &data, 4);
	part1 = ((data >> 23) & 0xFF) - 0x7F;
	if (part1 <= -15) {
		part2 = (SInt16) (fData / 0.00006103515625 * 1024);
		part1 = 0;
	} else {
		part1 += 15;
		part2 = (data & 0x7FFFFF) >> 13;
	}
	return (((0x1F & part1) << 10) | part2 | signMask);
}

static UInt32 NewRange(UInt16 oldRange, UInt8 oldBits, UInt8 newBits) {
	if (oldBits > newBits)
		return (oldRange >> (oldBits - newBits));
	else
		return ((oldRange << (newBits - oldBits)) | (oldRange >> (oldBits - newBits + oldBits)));	
}

static UInt32 PixelConvertUnsigned2Float(UInt32 value, UInt32 colorBits) {
	float fData;	//var_C
	UInt32 data;	//var_10
	
	data = 1 << (colorBits - 1);
	if (value >= data) fData = (float) (value - data);
	else fData = (float) value - (float) (data - 1);
	return EncodeFloat16(fData);
}

static UInt32 PixelConvertUnsigned2Signed(UInt32 value, UInt32 colorBits) {
	UInt32 data = 1 << (colorBits - 1);
	if (value >= data)
		return (value - data);
	else
		return (value | data);
}

static UInt32 EncodeCLUTEntry2PixelColor(UInt8 value, UInt16 depth) {
	UInt32 newValue;
	
	if (HALColorFormat(depth) & 1) {	//64bit
		newValue = NewRange(value, 8, HALColorBits(depth));
		if (HALColorFormat(depth) & 2)
			newValue = PixelConvertUnsigned2Float(newValue, HALColorBits(depth));
		else
			newValue = PixelConvertUnsigned2Signed(newValue, HALColorBits(depth));
	} else
		newValue = NewRange(value, 8, HALColorBits(depth));
	return newValue;
}

void HALGrayPage(UInt16 depth, int index) {
	ScrnInfoPtr pScrn = xf86Screens[0];
	RHDPtr rhdPtr = RHDPTR(pScrn);
	struct rhdCrtc *Crtc = rhdPtr->Crtc[index];
	
	if (!Crtc->Active || !rhdPtr->FbBase) return;
	if (!pScrn->bitsPerPixel || (pScrn->bitsPerPixel > 32)) return;
	UInt32 FBBase = (UInt32)rhdPtr->FbBase + Crtc->Offset;
	UInt32 grayValue = 0xF9808080;
	UInt16 bitsPerPixel = Crtc->bpp;
	UInt16 height = Crtc->Height;
	UInt32 rowBytes = Crtc->Pitch;
	
	UInt8 pixelUnit = bitsPerPixel / 32;
	if (pixelUnit == 0) pixelUnit = 1;
	UInt32 colorBits = HALColorBits(depth);
	UInt32 colors[2];
	int i, j, k;
	for (i = 0;i < pixelUnit;i++) colors[i] = 0;
	for (i = 0;i <= 2;i++) {
		UInt32 pixelColor;
		if (bitsPerPixel <= 15) pixelColor = (grayValue >> 24) & 0xFF;
		else pixelColor = (grayValue >> (8 * i)) & 0xFF;
		pixelColor = EncodeCLUTEntry2PixelColor(pixelColor, depth);
		j = 0;
		while (j < pixelUnit) {
			if (colorBits <= 32) {
				k = 0;
				while (k < 32) {
					colors[colorBits * i /32 + j] |= pixelColor << ((colorBits * i) & 0x1F + k);
					k += bitsPerPixel;
				}
			} else
				for (k = 0;k < bitsPerPixel / 32;k++)
					colors[colorBits * i /32 + j] |= pixelColor >> (k * 32);
			if (bitsPerPixel < 32) {
				j++;
				continue;
			}
			j += bitsPerPixel / 32;
		}
	}
	pixelUnit--;
	k = rowBytes * 8 / 32;
	for (j = 0;j <= height;j++) {
		UInt32 *base = (UInt32 *)(FBBase + j * rowBytes);
		for (i = 0;i < k;i++)
			if (pixelUnit != 0) base[i] = colors[pixelUnit & i];
			else base[i] = colors[0];
	}
}

static UInt16 colorRange[256];

static UInt16 GetGammaEntry(void *gFormulaData, UInt8 index, UInt8 entryBytes) {
	if (entryBytes == 2) return *((UInt16 *)gFormulaData + index * 2);
	return *((UInt8 *)gFormulaData + index);
}

static void SetGammaEntry(void *gFormulaData, UInt8 index, UInt8 entryBytes, UInt16 data) {
	if (entryBytes == 2) *((UInt16 *)gFormulaData + index * 2) = data;
	else *((UInt8 *)gFormulaData + index) = data;
}

UInt32 GammaCorrectARGB32(GammaTbl *gTable, UInt32 data) {
	UInt16 *redTable, *greenTable, *blueTable;
	
	if (gTable == NULL) return data;
	redTable = (UInt16 *)gTable->gFormulaData + gTable->gFormulaSize;//point to correction table
	UInt32 entryBits = gTable->gDataWidth + 7;
	if (entryBits < 0) entryBits += 7;
	UInt16 entryBytes = entryBits / 8;
	if (gTable->gChanCnt == 1) {	//rgb using same correction table
		greenTable = redTable;
		blueTable = redTable;
	}
	if (gTable->gChanCnt == 3) {	//rgb using different correction tables
		greenTable = entryBytes * gTable->gDataCnt + redTable;
		blueTable = entryBytes * gTable->gDataCnt + greenTable;
	}
	UInt8 alpha = (data >> 24) & 0xFF;		//alpha bits
	UInt8 red = (data >> 16) & 0xFF;		//red bits
	UInt8 green = (data >> 8) & 0xFF;		//green bits
	UInt8 blue = data & 0xFF;			//blue bits
	return ((alpha << 24)
			| (NewRange(GetGammaEntry(redTable, red, entryBytes), gTable->gDataWidth, 8) << 16)
			| (NewRange(GetGammaEntry(greenTable, green, entryBytes), gTable->gDataWidth, 8) << 8)
			| (NewRange(GetGammaEntry(blueTable, blue, entryBytes), gTable->gDataWidth, 8)));
}

void CreateLinearGamma(GammaTbl *gTable) {
	UInt8 entryBytes = 1;
	
	if (!gTable) return;
	gTable->gVersion = 0;
	gTable->gType = 0;
	gTable->gFormulaSize = 0;
	gTable->gChanCnt = 1;
	gTable->gDataCnt = 256;
	gTable->gDataWidth = 10;
	entryBytes = 2;
	int i;
	for (i = 0;i < 256;i++) {
		SetGammaEntry(gTable->gFormulaData, i, entryBytes, NewRange(i, 8, 10));
		colorRange[i] = (NewRange(i, 8, 10) << 20) | (NewRange(i, 8, 10) << 10) | NewRange(i, 8, 10);
	}
}

static void SetPaletteAccess(RHDPtr rhdPtr, UInt8 index) {
	ScrnInfoPtr pScrn = xf86Screens[rhdPtr->scrnIndex];
	//Controller->Crtc = rhdPtr->Crtc[index];
	
	UInt32 lutControl = 0;
	UInt32 lutRWMode = 0;
	UInt32 blackOffset = 0;
	UInt32 whiteOffset = 0;
	UInt32 offset = (index)?0x800:0;
	
	RHDRegWrite(rhdPtr, 0x6480, index);					//DC_LUT_RW_SELECT, 0:lowerHalf 1:upperHalf
	
	if (pScrn->bitsPerComponent > 8) {					//for pixelSize 32:10bitsEach or 64
		lutRWMode = 1;
		lutControl = pScrn->bitsPerComponent - 7;
		if (pScrn->colorFormat != 1) {
			whiteOffset = 0x7FFF;
			lutControl--;
			lutControl |= 16;
			if (((pScrn->colorFormat & 2)) && (pScrn->bitsPerComponent == 16))
				whiteOffset = 0x7BFF;
		}
		if ((pScrn->colorFormat & 2))	lutControl |= 32;
	}
	
	RHDRegWrite(rhdPtr, 0x6484, lutRWMode);														//DC_LUT_RW_MODE, 0:tableMode 1:linearMode
	RHDRegWrite(rhdPtr, 0x64C0 + offset, lutControl | (lutControl << 8) | (lutControl << 16));	//DC_LUT_CONTROL, RGB each 8 bits
	RHDRegWrite(rhdPtr, 0x64CC + offset, blackOffset);											//DC_LUT_BLACK_OFFSET for 3 color components
	RHDRegWrite(rhdPtr, 0x64C8 + offset, blackOffset);
	RHDRegWrite(rhdPtr, 0x64C4 + offset, blackOffset);
	RHDRegWrite(rhdPtr, 0x64D8 + offset, whiteOffset);											//DC_LUT_WHITE_OFFSET for 3 color components
	RHDRegWrite(rhdPtr, 0x64D4 + offset, whiteOffset);
	RHDRegWrite(rhdPtr, 0x64D0 + offset, whiteOffset);
	
	if (!pScrn->options->setCLUTAtSetEntries) WaitForVBL(rhdPtr, index, FALSE);
}

static void RF_DacRWIdx(RHDPtr rhdPtr, UInt8 index) {
	RHDRegWrite(rhdPtr, 0x6488, index);					//DC_LUT_RW_INDEX set start index
	IODelay(1);
}

static void HW_GetPaletteEntry(SInt16 offset, UInt16* red, UInt16* green,
							   UInt16* blue, UInt8 newBits) {
	UInt16 data = (colorRange[offset] & 0x3FF00000) >> 20;	//get red 10 bits
	*red = (UInt16) NewRange(data, 10, newBits);
	data = (colorRange[offset] & 0xFFC00) >> 10;			//get green 10 bits
	*green = (UInt16) NewRange(data, 10, newBits);
	data = colorRange[offset] & 0x3FF;						//get blue 10 bits
	*blue = (UInt16) NewRange(data, 10, newBits);
}

static void HW_GetScaledPaletteEntry(ScrnInfoPtr pScrn, UInt8 index, UInt16* red, UInt16* green,
									 UInt16* blue, UInt8 colorBits) {
	if (!(pScrn->colorFormat & (1 << 1))) {
		HW_GetPaletteEntry(index, red, green, blue, colorBits);
		return;
	}
	UInt32 iData, rgbData1, rgbData2;
	iData = index << (pScrn->bitsPerComponent - 9);
	if ((iData & 0x7FFF) > 0x7BFF) {
		HW_GetPaletteEntry(255, red, green, blue, colorBits);
		return;
	}
	float fData = DecodeFloat16(iData);
	fData /= (float) (1 << (pScrn->bitsPerComponent - 9));
	index = (UInt8) fData;
	rgbData1 = (colorRange[index] & 0x3FF00000) >> 20;
	if ((float) index != fData) {
		rgbData2 = (colorRange[index + 1] & 0x3FF00000) >> 20;
		rgbData1 += (UInt16) ((fData - (float) index) * (float) (rgbData2 - rgbData1));
	}
	(*red) = NewRange(rgbData1, 10, colorBits);
	rgbData1 = (iData & 0xFFC00) >> 10;
	if ((float) index != fData) {
		rgbData2 = (colorRange[index + 1] & 0xFFC00) >> 10;
		rgbData1 += (UInt16) ((fData - (float) index) * (float) (rgbData2 - rgbData1));
	}
	(*green) = NewRange(rgbData1, 10, colorBits);
	rgbData1 = iData & 0x3FF;
	if ((float) index != fData) {
		rgbData2 = colorRange[index + 1] & 0x3FF;
		rgbData1 += (UInt16) ((fData - (float) index) * (float) (rgbData2 - rgbData1));
	}
	(*blue) = NewRange(rgbData1, 10, colorBits);
}

static void SetPaletteEntry(RHDPtr rhdPtr, UInt8 index, UInt16 red, UInt16 green,
							UInt16 blue, UInt8 colorBits, Bool firstEntry, UInt8 crtcIndex) {
	ScrnInfoPtr pScrn = xf86Screens[rhdPtr->scrnIndex];
	
	colorRange[index] = (NewRange(red, colorBits, 10) << 20)
						| (NewRange(green, colorBits, 10) << 10)
						| NewRange(blue, colorBits, 10);
	
	/*
	if (Controller->Crtc != rhdPtr->Crtc[crtcIndex]) {
		SetPaletteAccess(rhdPtr, index);
		firstEntry = TRUE;
	}*/
	if (pScrn->bitsPerComponent > 8) {	//64 bit
		RGBColor c1, c2;	//var_28, var_24
		UInt8 offset;
		UInt32 data;
		
		if ((!index & 1)) {		//even
			HW_GetScaledPaletteEntry(pScrn, index, &c1.red, &c1.green, &c1.blue, colorBits);
			offset = (index == 254)?1:2;
			HW_GetScaledPaletteEntry(pScrn, index + offset, &c2.red, &c2.green, &c2.blue, colorBits);
			RF_DacRWIdx(rhdPtr, index / 2);
			data = NewRange(c1.red, colorBits, 16);
			if (c2.red > c1.red)
				data |= NewRange((c2.red - c1.red) << (2 - offset), colorBits, 16) << 16;
			RHDRegWrite(rhdPtr, 0x6490, data);						//DC_LUT_PWL_DATA
			data = NewRange(c1.green, colorBits, 16);
			if (c2.green > c1.green)
				data |= NewRange((c2.green - c1.green) << (2 - offset), colorBits, 16) << 16;
			RHDRegWrite(rhdPtr, 0x6490, data);						//DC_LUT_PWL_DATA
			data = NewRange(c1.blue, colorBits, 16);
			if (c2.blue > c1.blue)
				data |= NewRange((c2.blue - c1.blue) << (2 - offset), colorBits, 16) << 16;
			RHDRegWrite(rhdPtr, 0x6490, data);						//DC_LUT_PWL_DATA
		}
	} else {
		if (firstEntry) RF_DacRWIdx(rhdPtr, index);
		RHDRegWrite(rhdPtr, 0x6494, colorRange[index]);		//DC_LUT_30_COLOR, it's index changed accordingly
	}
}

static void HALSetDACWithLinearRamp(RHDPtr rhdPtr) {
	UInt8 unitSize = 1;
	UInt16 length;
	UInt8 channelSize;	//not used here
	UInt8 i, j, k;
	
	if (xf86Screens[rhdPtr->scrnIndex]->depth == 16) {
		length = 32;
		unitSize = 8;
		channelSize = 5;
	} else {
		length = 256;
		unitSize = 1;
		channelSize = 8;
	}
	for (k = 0;k < 2;k++) {
		if (!rhdPtr->Crtc[k]->Active) continue;
		SetPaletteAccess(rhdPtr, k);
		for (i = 0;i < length;i++)
			for (j = 0;j < unitSize;j++)
				SetPaletteEntry(rhdPtr, i, i, i, i, length, (i == 0), k);
	}
}

static void HALSetDACGamma(GammaTbl *gTable) {
	RHDPtr rhdPtr = RHDPTR(xf86Screens[0]);
	
	if (gTable == NULL) {
		HALSetDACWithLinearRamp(rhdPtr);
		return;
	}
	unsigned long rOffset, gOffset, bOffset;
	
	rOffset = (unsigned long) gTable->gFormulaData;
	rOffset += gTable->gFormulaSize;
	UInt16 dataCnt = gTable->gDataCnt;
	int entryBits = gTable->gDataWidth + 7;
	if (entryBits < 0) entryBits += 7;
	UInt16 entryBytes = entryBits / 8;
	if (gTable->gChanCnt == 3) {
		gOffset = rOffset + dataCnt * entryBytes;
		bOffset = gOffset + dataCnt * entryBytes;
	} else {
		gOffset = rOffset;
		bOffset = rOffset;
	}
	UInt8 j;
	UInt16 i;
	for (j = 0;j < 2;j++) {
		if (!rhdPtr->Crtc[j]->Active) continue;
		SetPaletteAccess(rhdPtr, j);
		for (i = 0;i < dataCnt;i++)
			SetPaletteEntry(rhdPtr, i,
							GetGammaEntry((void *)rOffset, i, entryBytes),
							GetGammaEntry((void *)gOffset, i, entryBytes),
							GetGammaEntry((void *)bOffset, i, entryBytes),
							gTable->gDataWidth, (i == 0), j);
	}
}

static void DoGammaCorrectCLUT(UInt32 count, ColorSpec *table, ColorSpec *colorTable, GammaTbl *gTable) {
	bcopy(table, colorTable, count * 8);
	unsigned long offsetR, offsetG, offsetB;
	offsetR = (unsigned long)gTable->gFormulaData;
	offsetR += gTable->gFormulaSize;
	UInt32 entryBits = gTable->gDataWidth + 7;
	if (entryBits < 0) entryBits += 7;
	UInt16 entryBytes = entryBits / 8;
	if (gTable->gChanCnt == 1) {
		offsetG = offsetR;
		offsetB = offsetR;
	}
	if (gTable->gChanCnt == 3) {
		offsetG = offsetR + gTable->gDataCnt * entryBytes;
		offsetB = offsetG + gTable->gDataCnt * entryBytes;
	}
	int i;
	for (i = 0;i < count;i++) {
		colorTable[i].rgb.red = NewRange(GetGammaEntry((void *)offsetR, colorTable[i].rgb.red >> 8, entryBytes), gTable->gDataWidth, 16);
		colorTable[i].rgb.green = NewRange(GetGammaEntry((void *)offsetG, colorTable[i].rgb.green >> 8, entryBytes), gTable->gDataWidth, 16);
		colorTable[i].rgb.blue = NewRange(GetGammaEntry((void *)offsetB, colorTable[i].rgb.blue >> 8, entryBytes), gTable->gDataWidth, 16);
	}
}

static void HW_SetClutDataEntry(UInt8 index, UInt16 red, UInt16 green, UInt16 blue, UInt8 colorBits) {
	colorRange[index] = (NewRange(red, colorBits, 10) << 20)
						| (NewRange(green, colorBits, 10) << 10)
						| NewRange(blue, colorBits, 10);
}

void HALSetDACWithTable(ScrnInfoPtr pScrn, SInt16 offset, UInt32 length, ColorSpec *colorTable) {
	RHDPtr rhdPtr = RHDPTR(pScrn);
	UInt32 colorOffset;		//var_18
	SInt32 firstOffset;		//var_14
	UInt32 unitSize;		//var_10
	UInt16 i, j, k;			//var_20, var_1C
	
	for (k = 0;k < 2;k++) {
		if (!rhdPtr->Crtc[k]->Active) continue;
		SetPaletteAccess(rhdPtr, k);
		if (!pScrn->options->setCLUTAtSetEntries) WaitForVBL(rhdPtr, k, FALSE);
		if (pScrn->bitsPerPixel == 16)
			unitSize = 8;
		else
			unitSize = 1;
		firstOffset = -1;
		for (i = 0;i < length;i++) {
			colorOffset = offset + i;
			if (offset == -1) colorOffset = colorTable[i].value;
			if (!colorOffset || (firstOffset != (colorOffset - 1))) firstOffset = -1;
			
			if (pScrn->bitsPerComponent > 8)
				for (j = 0;j < unitSize;j++)	//don't get the meaning here
					HW_SetClutDataEntry(colorOffset * unitSize, colorTable[i].rgb.red,
										colorTable[i].rgb.green, colorTable[i].rgb.blue, 16);
			for (j = 0;j < unitSize;j++) {
				SetPaletteEntry(rhdPtr, colorOffset * unitSize, colorTable[i].rgb.red,
								colorTable[i].rgb.green, colorTable[i].rgb.blue, 16, (firstOffset == -1), k);
				firstOffset = 0;
			}
			firstOffset = colorOffset;
		}
	}
}

void RadeonHDSetGamma(GammaTbl *gTable, GammaTbl *gTableNew) {
	ScrnInfoPtr pScrn = xf86Screens[0];
	RHDPtr rhdPtr = RHDPTR(pScrn);
	if (!gTable) return; 
	if (!gTableNew) CreateLinearGamma(gTable);	//reset gTable
	else {
		if (!gTableNew->gVersion || !gTableNew->gType) return;
		if ((gTableNew->gChanCnt != 1) && (gTableNew->gChanCnt != 3)) return;
		if ((gTableNew->gDataWidth <= 7) || (gTableNew->gDataWidth > 16)) return;
		gTable->gVersion = gTableNew->gVersion;
		gTable->gType = gTableNew->gType;
		gTable->gFormulaSize = gTableNew->gFormulaSize;
		gTable->gChanCnt = gTableNew->gChanCnt;
		gTable->gDataCnt = gTableNew->gDataCnt;
		gTable->gDataWidth = gTableNew->gDataWidth;
		UInt32 dataNum = gTableNew->gDataCnt * gTableNew->gChanCnt;
		int entryBits = gTableNew->gDataWidth + 7;
		if (entryBits < 0) entryBits += 7;
		UInt32 dataBytes = entryBits / 8 * dataNum;
		bcopy(gTableNew->gFormulaData, gTable->gFormulaData, dataBytes);
	}

	if (pScrn->bitsPerPixel > 8) HALSetDACGamma(gTable);
	UInt8 k;
	for (k = 0;k < 2;k++)
		//if (pScrn->options->HWCursorSupport && rhdPtr->Crtc[k]->Active) DrawCrsr(rhdPtr, k);
		if (pScrn->options->HWCursorSupport && rhdPtr->Crtc[k]->Active) rhdCrtcLoadCursorARGB(rhdPtr->Crtc[k], rhdPtr->CursorImage);
}

void RadeonHDSetEntries(GammaTbl *gTable, ColorSpec *cTable, SInt16 offset, UInt16 length) {
	ScrnInfoPtr pScrn = xf86Screens[0];
	ColorSpec colors[256];
	int i;
	
	if (pScrn->bitsPerPixel > 8) return;
	if (!cTable) return;
	if ((offset + length) > 256) return;
	if (gTable != NULL)
		DoGammaCorrectCLUT(length, cTable, colors, gTable);
	else
		for (i = 0;i < length;i++) {
			colors[i].value = cTable[i].value;
			colors[i].rgb.red = cTable[i].rgb.red;
			colors[i].rgb.green = cTable[i].rgb.green;
			colors[i].rgb.blue = cTable[i].rgb.blue;
		}
	HALSetDACWithTable(pScrn, offset, length, colors);
}

