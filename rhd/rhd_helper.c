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

#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/ndrvsupport/IOMacOSVideo.h>
#include "xf86.h"
#include "rhd.h"

/*
 * Create a new string where s2 is attached to s1, free s1.
 */
char *
RhdAppendString(char *s1, const char *s2)
{
    if (!s2)
		return s1;
    else if (!s1)
		return xstrdup(s2);
    else {
		int len = strlen(s1) + strlen(s2) + 1;
		char *result  = (char *)IOMalloc(len);
		
		if (!result) return s1;
		
		strncpy(result, s1, strlen(s1));
		strncat(result, s2, strlen(s2));
		IOFree(s1, strlen(s1) + 1);
		return result;
    }
}

//Helper functions Dong

UInt32 getPitch(UInt32 width, UInt32 bytesPerPixel) {
	if (bytesPerPixel == 0) bytesPerPixel = 1;
	return ((width * bytesPerPixel + 0xFF) & (~ 0xFF));
}

UInt32 HALPixelSize(IOIndex depth) {
	const static UInt32 pixelSize[7] = {8, 16, 32, 32, 64, 64, 128};
	
	if (depth < kDepthMode1) return 0;
	depth -= kDepthMode1;
	if (depth > 5) return 0;
	return pixelSize[depth];
}

UInt32 HALColorBits(IOIndex depth) {
	const static UInt32 colorBits[7] = {8, 5, 8, 10, 16, 16, 32};
	
	if (depth < kDepthMode1) return 0;
	depth -= kDepthMode1;
	if (depth > 5) return 0;
	return colorBits[depth];
}

UInt32 HALColorFormat(IOIndex depth) {
	const static UInt32 colorFormat[7] = {0, 0, 0, 0, 0, 3, 3};
	if (depth < kDepthMode1) return 0;
	depth -= kDepthMode1;
	if (depth > 5) return 0;
	return colorFormat[depth];
}

