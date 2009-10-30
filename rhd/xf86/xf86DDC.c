/* $XFree86: xc/programs/Xserver/hw/xfree86/ddc/xf86DDC.c,v 1.29 2006/03/16 16:49:57 dawes Exp $ */

/* xf86DDC.c 
 * 
 * Copyright 1998,1999 by Egbert Eich <Egbert.Eich@Physik.TU-Darmstadt.DE>
 */

/*
 * Copyright (c) 1999-2006 by The XFree86 Project, Inc.
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

#include "xf86.h"
#include "xf86DDC.h"
#include "ddcPriv.h"

const char *i2cSymbols[] = {
    "xf86CreateI2CDevRec",
    "xf86I2CDevInit",
    "xf86I2CWriteRead",
    "xf86I2CFindDev",
    "xf86DestroyI2CDevRec",
    NULL
};

#define RETRIES 4

static unsigned char *EDIDRead_DDC1(
    ScrnInfoPtr pScrn,
    void (*)(ScrnInfoPtr,xf86ddcSpeed), 
    unsigned int (*)(ScrnInfoPtr)
);

static Bool TestDDC1(
    ScrnInfoPtr pScrn,
    unsigned int (*)(ScrnInfoPtr)
);

static unsigned int *FetchEDID_DDC1(
    ScrnInfoPtr,
    register unsigned int (*)(ScrnInfoPtr)
);

static unsigned char* EDID1Read_DDC2(
    int scrnIndex, 
    I2CBusPtr pBus
);

/*
static unsigned char * VDIFRead(
    int scrnIndex, 
    I2CBusPtr pBus, 
    int start
);
*/

static unsigned char * DDCRead_DDC2(
    int scrnIndex,
    I2CBusPtr pBus, 
    int start, 
    int len
);

typedef enum {
    DDCOPT_NODDC1,
    DDCOPT_NODDC2,
    DDCOPT_NODDC,
    DDCOPT_EDID_DATA
} DDCOpts;

/*
static const OptionInfoRec DDCOptions[] = {
    { DDCOPT_NODDC1,	"NoDDC1",	OPTV_BOOLEAN,	{0},	FALSE },
    { DDCOPT_NODDC2,	"NoDDC2",	OPTV_BOOLEAN,	{0},	FALSE },
    { DDCOPT_NODDC,	"NoDDC",	OPTV_BOOLEAN,	{0},	FALSE },
    { DDCOPT_EDID_DATA,	"EDID Data",	OPTV_STRING,	{0},	FALSE },
    { -1,		NULL,		OPTV_NONE,	{0},	FALSE },
};
*/

xf86MonPtr 
xf86DoEDID_DDC1(
    int scrnIndex, void (*DDC1SetSpeed)(ScrnInfoPtr, xf86ddcSpeed), 
    unsigned int (*DDC1Read)(ScrnInfoPtr)
)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    unsigned char *EDID_block = NULL;
    xf86MonPtr tmp = NULL;

	EDID_block = EDIDRead_DDC1(pScrn,DDC1SetSpeed,DDC1Read);

    if (EDID_block){
	tmp = xf86InterpretEDID(scrnIndex,EDID_block);
    }
	else LOGV("No EDID block returned\n");
    if (!tmp)
	LOGV("Cannot interpret EDID block\n");
	return tmp;
}

xf86MonPtr
xf86DoEDID_DDC2(int scrnIndex, I2CBusPtr pBus)
{
    unsigned char *EDID_block = NULL;
    xf86MonPtr tmp = NULL;

	EDID_block = EDID1Read_DDC2(scrnIndex,pBus);

    if (EDID_block){
	tmp = xf86InterpretEDID(scrnIndex,EDID_block);
    } else {
	LOGV("No EDID block returned\n");
	return NULL;
    }
    if (!tmp)
	LOGV("Cannot interpret EDID block\n");
    LOGV("Sections to follow: %d\n",tmp->no_sections);

    return tmp;
}

/* 
 * read EDID record , pass it to callback function to interpret.
 * callback function will store it for further use by calling
 * function; it will also decide if we need to reread it 
 */
static unsigned char *
EDIDRead_DDC1(ScrnInfoPtr pScrn, void (*DDCSpeed)(ScrnInfoPtr,xf86ddcSpeed), 
              unsigned int (*read_DDC)(ScrnInfoPtr))
{
    unsigned char *EDID_block = NULL;
    int count = RETRIES;

    if (!read_DDC) { 
	LOG("chipset doesn't support DDC1\n");
	return NULL; 
    };

    if (TestDDC1(pScrn,read_DDC)==-1) { 
	LOG("No DDC signal\n"); 
	return NULL; 
    };

    if (DDCSpeed) DDCSpeed(pScrn,DDC_FAST);
    do {
	EDID_block = GetEDID_DDC1(FetchEDID_DDC1(pScrn,read_DDC)); 
	count --;
    } while (!EDID_block && count);
    if (DDCSpeed) DDCSpeed(pScrn,DDC_SLOW);

    return EDID_block;
}

/* test if DDC1  return 0 if not */
static Bool
TestDDC1(ScrnInfoPtr pScrn, unsigned int (*read_DDC)(ScrnInfoPtr))
{
    int old, count;

    old = read_DDC(pScrn);
    count = HEADER * BITS_PER_BYTE;
    do {
	/* wait for next retrace */
	if (old != read_DDC(pScrn)) break;
    } while(count--);
    return (count);
}

/* fetch entire EDID record; DDC bit needs to be masked */
static unsigned int * 
FetchEDID_DDC1(register ScrnInfoPtr pScrn,
	       register unsigned int (*read_DDC)(ScrnInfoPtr))
{
    int count = NUM;
    unsigned int *ptr, *xp;

    ptr=xp=(unsigned int *)IOMalloc(sizeof(int)*NUM); 

    if (!ptr)  return NULL;
    do {
	/* wait for next retrace */
	*xp = read_DDC(pScrn);
	xp++;
    } while(--count);
    return (ptr);
}

static unsigned char*
EDID1Read_DDC2(int scrnIndex, I2CBusPtr pBus)
{
    return  DDCRead_DDC2(scrnIndex, pBus, 0, EDID1_LEN);
}

static unsigned char *
DDCRead_DDC2(int scrnIndex, I2CBusPtr pBus, int start, int len)
{
	static char name[5] = "ddc2";
    I2CDevPtr dev;
    unsigned char W_Buffer[2];
    int w_bytes;
    unsigned char *R_Buffer;
    int i;

    if (!(dev = xf86I2CFindDev(pBus, 0x00A0))) {
	dev = xf86CreateI2CDevRec();
	dev->DevName = name;
	dev->SlaveAddr = 0xA0;
	dev->ByteTimeout = 2200; /* VESA DDC spec 3 p. 43 (+10 %) */
	dev->StartTimeout = 550;
	dev->BitTimeout = 40;
	dev->ByteTimeout = 40;
	dev->AcknTimeout = 40;

	dev->pI2CBus = pBus;
	if (!xf86I2CDevInit(dev)) {
	    LOG("No DDC2 device\n");
	    return NULL;
	}
    }
    if (start < 0x100) {
	w_bytes = 1;
	W_Buffer[0] = start;
    } else {
	w_bytes = 2;
	W_Buffer[0] = start & 0xFF;
	W_Buffer[1] = (start & 0xFF00) >> 8;
    }
    R_Buffer = (unsigned char *)IOMalloc(sizeof(unsigned char) * len);
	if (!R_Buffer) return NULL;
	bzero(R_Buffer, sizeof(unsigned char) * len);
    for (i=0; i<RETRIES; i++) {
		if (xf86I2CWriteRead(dev, W_Buffer,w_bytes, R_Buffer,len)) {
			if (!DDC_checksum(R_Buffer,len))
				return R_Buffer;
			else LOGV("Checksum error in EDID block\n");
		}
		else LOGV("Error reading EDID block\n");
    }
    
    xf86DestroyI2CDevRec(dev,TRUE);
    IOFree(R_Buffer, sizeof(unsigned char) * len);
    return NULL;
}
