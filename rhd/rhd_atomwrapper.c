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

#include "rhd_atomwrapper.h"

#define INT32 INT32
#include "CD_Common_Types.h"
#include "CD_Definitions.h"


int
ParseTableWrapper(void *pspace, int index, void *handle, void *BIOSBase,
		  char **msg_return)
{
	static char msgs[12][64] = {
		"ParseTable said: CD_SUCCESS",
		"ParseTable said: CD_CALL_TABLE",
		"ParseTable said: CD_COMPLETED",
		" ParseTable said: CD_GENERAL_ERROR",
		" ParseTable said: CD_INVALID_OPCODE",
		" ParseTable said: CD_NOT_IMPLEMENTED",
	    " ParseTable said: CD_EXEC_TABLE_NOT_FOUND",
	    " ParseTable said: CD_EXEC_PARAMETER_ERROR",
	    " ParseTable said: CD_EXEC_PARSER_ERROR",
	    " ParseTable said: CD_INVALID_DESTINATION_TYPE",
	    " ParseTable said: CD_UNEXPECTED_BEHAVIOR",
	    " ParseTable said: CD_INVALID_SWITCH_OPERAND_SIZE\n"
	};
	
    DEVICE_DATA deviceData;
    int ret = 0;

    /* FILL OUT PARAMETER SPACE */
    deviceData.pParameterSpace = (UINT32*) pspace;
    deviceData.CAIL = handle;
    deviceData.pBIOS_Image = BIOSBase;
    deviceData.format = TABLE_FORMAT_BIOS;

    switch (ParseTable(&deviceData, index)) { /* IndexInMasterTable */
	case CD_SUCCESS:
	    ret = 1;
	    *msg_return = msgs[0];
	    break;
	case CD_CALL_TABLE:
	    ret = 1;
	    *msg_return = msgs[1];
	    break;
	case CD_COMPLETED:
	    ret = 1;
	    *msg_return = msgs[2];
	    break;
	case CD_GENERAL_ERROR:
	    ret = 0;
	    *msg_return = msgs[3];
	    break;
	case CD_INVALID_OPCODE:
	    ret = 0;
	    *msg_return = msgs[4];
	    break;
	case CD_NOT_IMPLEMENTED:
	    ret = 0;
	    *msg_return = msgs[5];
	    break;
	case CD_EXEC_TABLE_NOT_FOUND:
	    ret = 0;
	    *msg_return = msgs[6];
	    break;
	case CD_EXEC_PARAMETER_ERROR:
	    ret = 0;
	    *msg_return = msgs[7];
	    break;
	case CD_EXEC_PARSER_ERROR:
	    ret = 0;
	    *msg_return = msgs[8];
	    break;
	case CD_INVALID_DESTINATION_TYPE:
	    ret = 0;
	    *msg_return = msgs[9];
	    break;
	case CD_UNEXPECTED_BEHAVIOR:
	    ret = 0;
	    *msg_return = msgs[10];
	    break;
	case CD_INVALID_SWITCH_OPERAND_SIZE:
	    ret = 0;
	    *msg_return = msgs[11];
	    break;
    }
    return ret;
}
