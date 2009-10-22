/*
 * Copyright 2007, 2008  Luc Verhaegen <libv@exsuse.de>
 * Copyright 2007, 2008  Matthias Hopf <mhopf@novell.com>
 * Copyright 2007, 2008 Egbert Eich   <eich@novell.com>
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

#include "xf86.h"
#include "xf86i2c.h"
#include "edid.h"

#include "rhd.h"
#ifdef ATOM_BIOS
# include "rhd_atombios.h"
#endif
#include "rhd_connector.h"
#include "rhd_output.h"
#include "rhd_regs.h"
#include "rhd_monitor.h"
#include "rhd_card.h"
#include "rhd_i2c.h"

/*
 *
 */
struct rhdHPD {
    Bool Stored;
    CARD32 StoreMask;
    CARD32 StoreEnable;
};

/*
 *
 */
void
RHDHPDSave(RHDPtr rhdPtr)
{
    struct rhdHPD *hpd = rhdPtr->HPD;

    RHDFUNC(rhdPtr);

    hpd->StoreMask = RHDRegRead(rhdPtr, DC_GPIO_HPD_MASK);
    hpd->StoreEnable = RHDRegRead(rhdPtr, DC_GPIO_HPD_EN);

    hpd->Stored = TRUE;
}

/*
 *
 */
void
RHDHPDRestore(RHDPtr rhdPtr)
{
    struct rhdHPD *hpd = rhdPtr->HPD;

    RHDFUNC(rhdPtr);

    if (hpd->Stored) {
	RHDRegWrite(rhdPtr, DC_GPIO_HPD_MASK, hpd->StoreMask);
	RHDRegWrite(rhdPtr, DC_GPIO_HPD_EN, hpd->StoreEnable);
    } else
	LOG("%s: no registers stored.\n", __func__);
}

/*
 *
 */
static void
RHDHPDSet(RHDPtr rhdPtr)
{
    RHDFUNC(rhdPtr);

    /* give the hw full control */
    RHDRegWrite(rhdPtr, DC_GPIO_HPD_MASK, 0);
    RHDRegWrite(rhdPtr, DC_GPIO_HPD_EN, 0);

    IODelay(1);
}

/*
 *
 */
static Bool
RHDHPDCheck(struct rhdConnector *Connector)
{
    Bool ret;

    RHDFUNC(Connector);

    ret = RHDRegRead(Connector, DC_GPIO_HPD_Y);
    LOG("%s returned: %x mask: %x\n",
	     __func__,ret, Connector->HPDMask);

    return (ret & Connector->HPDMask);
}

struct rhdCsState {
    int vga_cnt;
    int dvi_cnt;
};

/*
 *
 */
static char *
rhdConnectorSynthName(struct rhdConnectorInfo *ConnectorInfo,
		      struct rhdCsState **state)
{
	static char names[4][6] = {
		"DVI-I", "VGA", "DVI-A", "DVI-D"
	};
	
    char *str = NULL;
    char *TypeName;
    char *str1, *str2;
    int cnt;

    //ASSERT(state != NULL);

    if (!*state) {
		*state = IONew(struct rhdCsState, 1);
		if (!*state) return NULL;
		else bzero(*state, sizeof(struct rhdCsState));
    }
    switch (ConnectorInfo->Type) {
	case RHD_CONNECTOR_NONE:
	    return NULL;
	case RHD_CONNECTOR_DVI:
	case RHD_CONNECTOR_DVI_SINGLE:
	    if (ConnectorInfo->Output[0] && ConnectorInfo->Output[1]) {
		TypeName = names[0];
		cnt = ++(*state)->dvi_cnt;
	    } else if (ConnectorInfo->Output[0] == RHD_OUTPUT_DACA
		     || ConnectorInfo->Output[0] == RHD_OUTPUT_DACB
		     || ConnectorInfo->Output[1] == RHD_OUTPUT_DACA
		     || ConnectorInfo->Output[1] == RHD_OUTPUT_DACB
		) {
		if (ConnectorInfo->HPD == RHD_HPD_NONE) {
		    TypeName = names[1];
		    cnt = ++(*state)->vga_cnt;
		} else {
		    TypeName = names[2];
		    cnt = ++(*state)->dvi_cnt;
		}
	    } else {
		TypeName = names[3];
		cnt = ++(*state)->dvi_cnt;
	    }
	    str = (char *)IOMalloc(12);
	    snprintf(str, 11, "%s %d",TypeName, cnt);
	    return str;

	case RHD_CONNECTOR_VGA:
	    str = (char *)IOMalloc(10);
	    snprintf(str, 9, "VGA %d",++(*state)->vga_cnt);
	    return str;

	case RHD_CONNECTOR_PANEL:
	    str = (char *)IOMalloc(10);
	    snprintf(str, 9, "PANEL");
	    return str;

	case RHD_CONNECTOR_TV:
	    str1 = xstrdup(ConnectorInfo->Name);
	    str = (char *)IOMalloc(20);
	    str2 = strchr(str1, ' ');
	    if (str2) *(str2) = '\0';
	    snprintf(str, 20, "TV %s",str1);
	    IOFree(str1, 20);
	    return str;

	case RHD_CONNECTOR_PCIE: /* should never get here */
	    return NULL;
    }
    return NULL;
}

/*
 *
 */
Bool
RHDConnectorsInit(RHDPtr rhdPtr, struct rhdCard *Card)
{
    struct rhdConnectorInfo *ConnectorInfo;
    struct rhdConnector *Connector;
    struct rhdOutput *Output;
    struct rhdCsState *csstate = NULL;
    int i, j, k, l, hpd;
    Bool InfoAllocated = FALSE;
	
    RHDFUNC(rhdPtr);
	
    /* Card->ConnectorInfo is there to work around quirks, so check it first */
    if (Card && (Card->ConnectorInfo[0].Type != RHD_CONNECTOR_NONE)) {
		ConnectorInfo = Card->ConnectorInfo;
		LOG("ConnectorInfo from quirk table:\n");
		RhdPrintConnectorInfo (rhdPtr->scrnIndex, ConnectorInfo);
    } else {
#ifdef ATOM_BIOS
		/* common case */
		AtomBiosArgRec data;
		AtomBiosResult result;
		
		data.chipset = rhdPtr->ChipSet;
		result = RHDAtomBiosFunc(rhdPtr->scrnIndex, rhdPtr->atomBIOS,
								 ATOM_GET_CONNECTORS, &data);
		if (result == ATOM_SUCCESS) {
			ConnectorInfo = data.ConnectorInfo;
			InfoAllocated = TRUE;
		} else
#endif
        {
			LOG("%s: Failed to retrieve "
				"Connector information.\n", __func__);
			return FALSE;
		}
    }
    /* Init HPD */
    rhdPtr->HPD = IONew(struct rhdHPD, 1);
	if (!rhdPtr->HPD) return FALSE;
	bzero(rhdPtr->HPD, sizeof(struct rhdHPD));
    RHDHPDSave(rhdPtr);
    RHDHPDSet(rhdPtr);
	
    for (i = 0, j = 0; i < RHD_CONNECTORS_MAX; i++) {
		if (ConnectorInfo[i].Type == RHD_CONNECTOR_NONE)
			continue;
		
		LOG("%s: %d (%s) type %d, ddc %d, hpd %d\n",
			__func__, i, ConnectorInfo[i].Name, ConnectorInfo[i].Type,
			ConnectorInfo[i].DDC, ConnectorInfo[i].HPD);
		
		Connector = IONew(struct rhdConnector, 1);
		if (!Connector) return FALSE;
		bzero(Connector, sizeof(struct rhdConnector));
		
		Connector->scrnIndex = rhdPtr->scrnIndex;
		
		Connector->Type = ConnectorInfo[i].Type;
		Connector->Name = rhdConnectorSynthName(&ConnectorInfo[i], &csstate);
		
		/* Get the DDC bus of this connector */
		if (ConnectorInfo[i].DDC != RHD_DDC_NONE) {
			RHDI2CDataArg data;
			int ret;
			
			data.i = ConnectorInfo[i].DDC;
			ret = RHDI2CFunc(rhdPtr->scrnIndex,
							 rhdPtr->I2C, RHD_I2C_GETBUS, &data);
			if (ret == RHD_I2C_SUCCESS)
				Connector->DDC = data.i2cBusPtr;
		}
		
		/* attach HPD */
		hpd = ConnectorInfo[i].HPD;
		switch (rhdPtr->hpdUsage) {
			case RHD_HPD_USAGE_OFF:
			case RHD_HPD_USAGE_AUTO_OFF:
				hpd = RHD_HPD_NONE;
				break;
			case RHD_HPD_USAGE_SWAP:
			case RHD_HPD_USAGE_AUTO_SWAP:
				switch (hpd) {
					case RHD_HPD_0:
						hpd = RHD_HPD_1;
						break;
					case RHD_HPD_1:
						hpd = RHD_HPD_0;
						break;
				}
				break;
			default:
				break;
		}
		switch(hpd) {
			case RHD_HPD_0:
				Connector->HPDMask = 0x00000001;
				Connector->HPDCheck = RHDHPDCheck;
				break;
			case RHD_HPD_1:
				Connector->HPDMask = 0x00000100;
				Connector->HPDCheck = RHDHPDCheck;
				break;
			case RHD_HPD_2:
				Connector->HPDMask = 0x00010000;
				Connector->HPDCheck = RHDHPDCheck;
				break;
			case RHD_HPD_3:
				Connector->HPDMask = 0x01000000;
				Connector->HPDCheck = RHDHPDCheck;
				break;
			default:
				Connector->HPDCheck = NULL;
				break;
		}
		
		/* create Outputs */
		for (k = 0; k < 2; k++) {
			if (ConnectorInfo[i].Output[k] == RHD_OUTPUT_NONE)
				continue;
			
			/* Check whether the output exists already */
			for (Output = rhdPtr->Outputs; Output; Output = Output->Next)
				if (Output->Id == ConnectorInfo[i].Output[k])
					break;
			
			if (!Output) {
				if (!RHDUseAtom(rhdPtr, NULL, atomUsageOutput)) {
					switch (ConnectorInfo[i].Output[k]) {
						case RHD_OUTPUT_DACA:
							Output = RHDDACAInit(rhdPtr);
							RHDOutputAdd(rhdPtr, Output);
							break;
						case RHD_OUTPUT_DACB:
							Output = RHDDACBInit(rhdPtr);
							RHDOutputAdd(rhdPtr, Output);
							break;
						case RHD_OUTPUT_TMDSA:
							Output = RHDTMDSAInit(rhdPtr);
							RHDOutputAdd(rhdPtr, Output);
							break;
						case RHD_OUTPUT_LVTMA:
							Output = RHDLVTMAInit(rhdPtr, ConnectorInfo[i].Type);
							RHDOutputAdd(rhdPtr, Output);
							break;
						case RHD_OUTPUT_DVO:
							Output = RHDDDIAInit(rhdPtr);
							if (Output)
								RHDOutputAdd(rhdPtr, Output);
							break;
						case RHD_OUTPUT_KLDSKP_LVTMA:
						case RHD_OUTPUT_UNIPHYA:
						case RHD_OUTPUT_UNIPHYB:
							Output = RHDDIGInit(rhdPtr, ConnectorInfo[i].Output[k], ConnectorInfo[i].Type);
							RHDOutputAdd(rhdPtr, Output);
							break;
						default:
							LOG("%s: unhandled output id: %d. Trying fallback to AtomBIOS\n", __func__,
								ConnectorInfo[i].Output[k]);
							break;
					}
				}
#ifdef ATOM_BIOS
				if (!Output) {
					Output = RHDAtomOutputInit(rhdPtr, ConnectorInfo[i].Type,
											   ConnectorInfo[i].Output[k]);
					if (Output)
						RHDOutputAdd(rhdPtr, Output);
				}
#endif
			}
			
			if (Output) {
				LOG("Attaching Output %s to Connector %s\n",
					Output->Name, Connector->Name);
				for (l = 0; l < 2; l++)
					if (!Connector->Output[l]) {
						Connector->Output[l] = Output;
						break;
					}
			}
		}
		
		rhdPtr->Connector[j] = Connector;
		j++;
    }
    if (csstate)
		IODelete(csstate, struct rhdCsState, 1);
	
    /* Deallocate what atombios code allocated */
    if (ConnectorInfo && InfoAllocated) {
		for (i = 0; i < RHD_CONNECTORS_MAX; i++)
			if (ConnectorInfo[i].Type != RHD_CONNECTOR_NONE)
				IOFree(ConnectorInfo[i].Name, strlen(ConnectorInfo[i].Name) + 1);
		/* Don't free the Privates as they are hooked into the rhdConnector structures !!! */
		IODelete(ConnectorInfo, struct rhdConnectorInfo, 1);
    }
	
    RHDHPDRestore(rhdPtr);
	
    return (j && 1);
}

/*
 *
 */
void
RHDConnectorsDestroy(RHDPtr rhdPtr)
{
    struct rhdConnector *Connector;
    int i;

    RHDFUNC(rhdPtr);

    for (i = 0; i < RHD_CONNECTORS_MAX; i++) {
	Connector = rhdPtr->Connector[i];
	if (Connector) {
	    if (Connector->Monitor)
		RHDMonitorDestroy(Connector->Monitor);
	    IOFree(Connector->Name, strlen(Connector->Name) + 1);
	    IODelete(Connector, struct rhdConnector, 1);
	}
    }
}

/*
 *
 */
void
RhdPrintConnectorInfo(int scrnIndex, struct rhdConnectorInfo *cp)
{
    int n;
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    RHDPtr rhdPtr = RHDPTR(pScrn);

    const char *c_name[] =
	{ "RHD_CONNECTOR_NONE", "RHD_CONNECTOR_VGA", "RHD_CONNECTOR_DVI",
	  "RHD_CONNECTOR_DVI_SINGLE", "RHD_CONNECTOR_PANEL",
	  "RHD_CONNECTOR_TV", "RHD_CONNECTOR_PCIE" };

    const char *ddc_name[] =
	{ "RHD_DDC_0", "RHD_DDC_1", "RHD_DDC_2", "RHD_DDC_3", "RHD_DDC_4" };

    const char *hpd_name_normal[] =
	{ "RHD_HPD_NONE", "RHD_HPD_0", "RHD_HPD_1", "RHD_HPD_2", "RHD_HPD_3" };
    const char *hpd_name_off[] =
	{ "RHD_HPD_NONE", "RHD_HPD_NONE /*0*/", "RHD_HPD_NONE /*1*/", "RHD_HPD_NONE /*2*/", "RHD_HPD_NONE /*3*/" };
    const char *hpd_name_swapped[] =
	{ "RHD_HPD_NONE", "RHD_HPD_1 /*swapped*/", "RHD_HPD_0 /*swapped*/", "RHD_HPD_2", "RHD_HPD_3" };

    const char *output_name[] =
	{ "RHD_OUTPUT_NONE", "RHD_OUTPUT_DACA", "RHD_OUTPUT_DACB", "RHD_OUTPUT_TMDSA",
	  "RHD_OUTPUT_LVTMA", "RHD_OUTPUT_DVO", "RHD_OUTPUT_KLDSKP_LVTMA",
	  "RHD_OUTPUT_UNIPHYA", "RHD_OUTPUT_UNIPHYB", "RHD_OUTPUT_UNIPHYC", "RHD_OUTPUT_UNIPHYD",
	  "RHD_OUTPUT_UNIPHYE", "RHD_OUTPUT_UNIPHYF" };
    const char **hpd_name;

    switch (rhdPtr->hpdUsage) {
    case RHD_HPD_USAGE_OFF:
    case RHD_HPD_USAGE_AUTO_OFF:
	hpd_name = hpd_name_off;
	break;
    case RHD_HPD_USAGE_SWAP:
    case RHD_HPD_USAGE_AUTO_SWAP:
	hpd_name = hpd_name_swapped;
	break;
    default:
	hpd_name = hpd_name_normal;
	break;
    }

    for (n = 0; n < RHD_CONNECTORS_MAX; n++) {
	if (cp[n].Type == RHD_CONNECTOR_NONE)
	    break;
	LOG("Connector[%d] {%s, \"%s\", %s, %s, { %s, %s } }\n",
		   n, c_name[cp[n].Type], cp[n].Name,
		   cp[n].DDC == RHD_DDC_NONE ? "RHD_DDC_NONE" : ddc_name[cp[n].DDC],
		   hpd_name[cp[n].HPD], output_name[cp[n].Output[0]],
		   output_name[cp[n].Output[1]]);
    }
}

/*
 * Should we enable HDMI on this connector?
 */
Bool RHDConnectorEnableHDMI(struct rhdConnector *Connector)
{
    RHDPtr rhdPtr = RHDPTRI(Connector);
    RHDFUNC(rhdPtr);

    /* check if user forced HDMI on this connector */
    if (rhdPtr->hdmi) {	//now only one connector (Dong)
	    LOG("Enabling HDMI on %s because of config option\n", Connector->Name);
	    return TRUE;
	}
    return FALSE;
}
