/*
 *  RadeonController.cpp
 *  RadeonHD
 *
 *  Created by Dong Luo on 5/16/11.
 *  Copyright 2011 Boston University. All rights reserved.
 *
 */

#include <IOKit/platform/ApplePlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include "RadeonController.h"

class IONDRVDevice : public IOPlatformDevice
{
    OSDeclareDefaultStructors(IONDRVDevice)
	
public:
    virtual bool compareName( OSString * name, OSString ** matched = 0 ) const;
    virtual IOService * matchLocation( IOService * client );
    virtual IOReturn getResources( void );
    virtual void joinPMtree( IOService * driver );
};
extern "C" IOReturn _IONDRVLibrariesInitialize( IOService * provider );
#ifndef __IONDRVSUPPORT__
#define kIONDRVIgnoreKey        "AAPL,iokit-ignore-ndrv"
#endif
#ifndef _IOKIT_IOFRAMEBUFFER_H
#define kIOFBDependentIDKey     "IOFBDependentID"
#define kIOFBDependentIndexKey  "IOFBDependentIndex"
#endif

#define MAKE_REG_ENTRY(regEntryID,obj)                          \
(regEntryID)->opaque[ 0 ] = (void *) obj;                       \
(regEntryID)->opaque[ 1 ] = (void *) ~(uintptr_t)obj;           \
(regEntryID)->opaque[ 2 ] = (void *) 0x53696d65;                \
(regEntryID)->opaque[ 3 ] = (void *) 0x52756c7a;

#define RHD_VBIOS_BASE 0xC0000
#define RHD_VBIOS_SIZE 0x10000

#undef super
#define super IOService

OSDefineMetaClassAndStructors(RadeonController, IOService)

IOService * RadeonController::probe(IOService *provider, SInt32 *score) {
	return super::probe(provider, score);
}

static void createNubs(IOService *provider) {
	const char nameID[2][8] = {"@0,name", "@1,name"};
	const char name[11] = "Aty,Radeon";
	const char typeID[2][15] = {"@0,device_type", "@1,device_type"};
	const char type[] = "display";
	OSObject *tempObj;
	
	int i;
	
	if (provider->getProperty(kIONDRVIgnoreKey)) return;
	provider->setProperty(kIONDRVIgnoreKey, kOSBooleanTrue);	//prevent IONDRVFramebuffer from match
	
	LOG("createNubs\n");
	
	if (!provider->getProperty("@0,name") && !provider->getProperty("@1,name")) {
		for (i = 0;i < 2;i++) {	// Debug
			tempObj = OSData::withBytes(name, 11);
			provider->setProperty(nameID[i], tempObj);
			tempObj->release();
			tempObj = OSData::withBytes(type, 8);
			provider->setProperty(typeID[i], tempObj);
			tempObj->release();
		}
	}
	// have to move below part from IONDRVFramebuffer to make it work
	IORegistryIterator * iter;
	IORegistryEntry *    next;
	IOService *          newNub;
	OSArray *            toDo = 0;
	bool                 firstLevel;
    OSData *            data;
	
	if (provider->getProperty("@0,name"))
	{
		OSDictionary *         dict;
		OSCollectionIterator * keys;
		const OSSymbol *       key;
		char                   buffer[80];
		const char *           keyChrs;
		size_t                 len;
		char                   c;
		
		dict = provider->dictionaryWithProperties();
		keys = OSCollectionIterator::withCollection(dict);
		if (dict)
			dict->release();
		if (keys)
		{
			while ((key = OSDynamicCast(OSSymbol, keys->getNextObject())))
			{
				keyChrs = key->getCStringNoCopy();
				if ('@' != keyChrs[0])
					continue;
				
				len = 0;
				do
				{
					c = keyChrs[len];
					if (!c || (c == ','))
						break;
					buffer[len] = c;
					len++;
				}
				while (len < (sizeof(buffer) - 1));
				if (!c)
					continue;
				
				buffer[len] = 0;
				keyChrs += len + 1;
				
				next = provider->childFromPath(buffer, gIODTPlane);
				if (!next)
				{
					next = new IOService;
					if (next && !next->init())
					{
						next->release();
						next = 0;
					}
					if (!next)
						continue;
					next->setLocation(&buffer[1]);
					if (!next->attachToParent(provider, gIODTPlane))
						continue;
				}
				
				OSObject * obj = dict->getObject(key);
				next->setProperty(keyChrs, dict->getObject(key));
				if (!strcmp(keyChrs, "name"))
				{
					OSData * data = OSDynamicCast(OSData, obj);
					if (data)
						next->setName((const char *) data->getBytesNoCopy());
				}
				next->release();
				provider->removeProperty(key);
			}
			keys->release();
		}
	}
	
	iter = IORegistryIterator::iterateOver( provider, gIODTPlane, 0 );
	toDo = OSArray::withCapacity(2);
	
	if (iter && toDo)
	{
		bool haveDoneLibInit = false;
		UInt32 index = 0;
		do
		{
			while ((next = (IORegistryEntry *) iter->getNextObject()))
			{
				firstLevel = (provider == next->getParentEntry(gIODTPlane));
				if (firstLevel)
				{
					data = OSDynamicCast( OSData, next->getProperty("device_type"));
					if (!data || (0 != strcmp("display", (char *) data->getBytesNoCopy())))
						continue;
					
					if (!haveDoneLibInit)
					{
						haveDoneLibInit = (kIOReturnSuccess == _IONDRVLibrariesInitialize(provider));
						if (!haveDoneLibInit)
							continue;
					}
					next->setProperty( kIOFBDependentIDKey, (uintptr_t) provider, 64 );
					next->setProperty( kIOFBDependentIndexKey, index, 32 );
					next->setProperty( kIONDRVIgnoreKey, kOSBooleanTrue );
					index++;
				}
				
				toDo->setObject( next );
				iter->enterEntry();
			}
		}
		while (iter->exitEntry());
	}
	if (iter)
		iter->release();
	
	if (toDo)
	{
		OSObject * obj;
		OSArray  * deviceMemory;
		obj = provider->copyProperty(gIODeviceMemoryKey);
		deviceMemory = OSDynamicCast(OSArray, obj);
		
		for (unsigned int i = 0;
			 (next = (IORegistryEntry *) toDo->getObject(i));
			 i++)
		{
			newNub = new IONDRVDevice;
			if (!newNub)
				continue;
			if (!newNub->init(next, gIODTPlane))
			{
				newNub->free();
				newNub = 0;
				continue;
			}
			if (deviceMemory)
				newNub->setDeviceMemory(deviceMemory);
			newNub->attach(provider);
			newNub->registerService(kIOServiceSynchronous);
		}
		if (obj)
			obj->release();
		toDo->release();
	}
}

extern SymTabRec RHDModels[];
static void setModel(IOPCIDevice *device) {
	if (!device) return;
	char *model = (char *)xf86TokenToString(RHDModels, device->configRead16(kIOPCIConfigDeviceID));
	//if ((model == NULL) && (xf86Screens[0] != NULL)) model = xf86Screens[0]->chipset;
	if (model != NULL) device->setProperty("model", model);
}

bool RadeonController::start( IOService * provider )
{
	if (!super::start(provider)) return false;
	
	device = OSDynamicCast(IOPCIDevice, provider);
	if (device == NULL) return false;
	
	//get user options
	OSBoolean *prop;
	
	OSDictionary *dict = OSDynamicCast(OSDictionary, getProperty("UserOptions"));
	
	bzero(&options, sizeof(UserOptions));
	options.HWCursorSupport = FALSE;
	options.enableGammaTable = FALSE;
	options.enableOSXI2C = FALSE;
	
	options.lowPowerMode = FALSE;
	if (dict) {
		prop = OSDynamicCast(OSBoolean, dict->getObject("enableHWCursor"));
		if (prop) options.HWCursorSupport = prop->getValue();
		prop = OSDynamicCast(OSBoolean, dict->getObject("debugMode"));
		if (prop) options.debugMode = prop->getValue();
		if (options.debugMode) options.HWCursorSupport = FALSE;
		prop = OSDynamicCast(OSBoolean, dict->getObject("enableGammaTable"));
		if (prop) options.enableGammaTable = prop->getValue();
		prop = OSDynamicCast(OSBoolean, dict->getObject("lowPowerMode"));
		if (prop) options.lowPowerMode = prop->getValue();
	}
	options.verbosity = 1;
#ifdef DEBUG
	if (0 == getRegistryRoot()->getProperty("RadeonDumpReady")) {
		getRegistryRoot()->setProperty("RadeonDumpReady", kOSBooleanTrue);
		DumpMsg.mVerbose = 1;
		DumpMsg.client = 1;
		DumpMsg.mMsgBufferSize = 65535;
		if (dict) {
			OSNumber *optionNum;
			optionNum = OSDynamicCast(OSNumber, dict->getObject("verboseLevel"));
			if (optionNum) DumpMsg.mVerbose = optionNum->unsigned32BitValue();
			optionNum = OSDynamicCast(OSNumber, dict->getObject("MsgBufferSize"));
			if (optionNum) DumpMsg.mMsgBufferSize = max(65535, optionNum->unsigned32BitValue());
		}	
		DumpMsg.mMsgBufferEnabled = false;
		DumpMsg.mMsgBufferPos = 0;
		DumpMsg.mMessageLock = IOLockAlloc();
		DumpMsg.mMsgBuffer = (char *) IOMalloc(DumpMsg.mMsgBufferSize);
		if (!DumpMsg.mMsgBuffer) {
			IOLog("error: couldn't allocate message buffer (%ld bytes)\n", DumpMsg.mMsgBufferSize);
			return false;
		}
		enableMsgBuffer(true);
	} else DumpMsg.client += 1;
	options.verbosity = DumpMsg.mVerbose;
#endif
	
	device->setMemoryEnable(true);
	IOMap = device->mapDeviceMemoryWithRegister( kIOPCIConfigBaseAddress2 );
	if (IOMap == NULL) return false;
	FBMap = device->mapDeviceMemoryWithRegister( kIOPCIConfigBaseAddress0 );
	if (FBMap == NULL) return false;
	memoryMap.MMIOBase = (pointer) IOMap->getVirtualAddress();
	memoryMap.MMIOMapSize = IOMap->getLength();
	memoryMap.FbBase = (pointer) FBMap->getVirtualAddress();
	memoryMap.FbMapSize = FBMap->getLength();
	memoryMap.FbPhysBase = (unsigned long)FBMap->getPhysicalAddress();
	memoryMap.bitsPerPixel = 32;
	memoryMap.bitsPerComponent = 8;
	memoryMap.colorFormat = 0;	//0 for non-64 bit
	
	memoryMap.BIOSCopy = NULL;
	memoryMap.BIOSLength = 0;
	
	IOMemoryDescriptor * mem;
	mem = IOMemoryDescriptor::withPhysicalAddress((IOPhysicalAddress) RHD_VBIOS_BASE, RHD_VBIOS_SIZE, kIODirectionOut);
	if (mem) {
		memoryMap.BIOSCopy = (unsigned char *)IOMalloc(RHD_VBIOS_SIZE);
		if (memoryMap.BIOSCopy) {
			mem->prepare(kIODirectionOut);
			if (!(memoryMap.BIOSLength = mem->readBytes(0, memoryMap.BIOSCopy, RHD_VBIOS_SIZE))) {
				LOG("Cannot read BIOS image\n");
				memoryMap.BIOSLength = 0;
			}
			if ((unsigned int)memoryMap.BIOSLength != RHD_VBIOS_SIZE)
				LOG("Read only %d of %d bytes of BIOS image\n", memoryMap.BIOSLength, RHD_VBIOS_SIZE);
			mem->complete(kIODirectionOut);
		}
	}

	if (dict) {
		const char typeKey[2][8] = {"@0,TYPE", "@1,TYPE"};
		const char EDIDKey[2][8] = {"@0,EDID", "@1,EDID"};
		const char fixedModesKey[2][17] = {"@0,UseFixedModes", "@1,UseFixedModes"};
		OSString *type;
		OSData *edidData;
		OSBoolean *boolData;
		int i;
		for (i = 0;i < 2;i++) {
			type = OSDynamicCast(OSString, dict->getObject(typeKey[i]));
			if (!type) continue;
			edidData = OSDynamicCast(OSData, dict->getObject(EDIDKey[i]));
			if (edidData == NULL) continue;
			options.EDID_Block[i] = (unsigned char *)IOMalloc(edidData->getLength());
			if (options.EDID_Block[i] == NULL) continue;
			strncpy(options.outputTypes[i], type->getCStringNoCopy(), outputTypeLength);
			bcopy(edidData->getBytesNoCopy(), options.EDID_Block[i], edidData->getLength());
			options.EDID_Length[i] = edidData->getLength();
			
			boolData = OSDynamicCast(OSBoolean, dict->getObject(fixedModesKey[i]));
			if (boolData) options.UseFixedModes[i] = boolData->getValue();
		}
	}
		
	xf86Screens[0] = IONew(ScrnInfoRec, 1);	//using global variable, will change it later
	ScrnInfoPtr pScrn = xf86Screens[0];
	if (pScrn == NULL) return false;
	bzero(pScrn, sizeof(ScrnInfoRec));
	MAKE_REG_ENTRY(&nub, device);
	pciRec.chipType = device->configRead16(kIOPCIConfigDeviceID);
	pciRec.subsysVendor = device->configRead16(kIOPCIConfigSubSystemVendorID);
	pciRec.subsysCard = device->configRead16(kIOPCIConfigSubSystemID);
	pciRec.biosSize = 16;	//RHD_VBIOS_SIZE = 1 << 16
	pScrn->PciTag = &nub;
	pScrn->PciInfo = &pciRec;
	pScrn->options = &options;
	pScrn->memPhysBase = (unsigned long)FBMap->getPhysicalAddress();
	pScrn->fbOffset = 0;	//scanout offset
	pScrn->bitsPerPixel = 32;
	pScrn->bitsPerComponent = 8;
	pScrn->colorFormat = 0;
	pScrn->depth = pScrn->bitsPerPixel;
	pScrn->memoryMap = &memoryMap;
	
	
	createNubs(provider);
	
	setModel(device);
	
	return true;
}

void RadeonController::stop(IOService *provider) {
	super::stop(provider);
}

void RadeonController::free( void ) {
	ScrnInfoPtr pScrn = xf86Screens[0];
	if (pScrn) {
		IODelete(pScrn, ScrnInfoRec, 1);
		xf86Screens[0] = NULL;
	}
	
#ifdef DEBUG
	DumpMsg.client--;
	if (DumpMsg.client == 0) {
		DumpMsg.mMsgBufferEnabled = false;
		if (DumpMsg.mMsgBuffer) {
			IOFree(DumpMsg.mMsgBuffer, DumpMsg.mMsgBufferSize);
			DumpMsg.mMsgBuffer = NULL;
		}
		if (DumpMsg.mMessageLock) {
			IOLockLock(DumpMsg.mMessageLock);
			IOLockFree(DumpMsg.mMessageLock);
			DumpMsg.mMessageLock = NULL;
		}
		getRegistryRoot()->removeProperty("RadeonDumpReady");
	}
#endif

	int i;
	for (i = 0;i < 2;i++) {
		if (options.EDID_Block[i]) {
			IOFree(options.EDID_Block[i], options.EDID_Length[i]);
			options.EDID_Block[i] = NULL;
		}
	}
	if (memoryMap.BIOSCopy) {
		IOFree(memoryMap.BIOSCopy, memoryMap.BIOSLength);
		memoryMap.BIOSCopy = NULL;
	}

	if (IOMap) IOMap->release();
	if (FBMap) FBMap->release();

	super::free();
}

UserOptions * RadeonController::getUserOptions(void) {
	return &options;
}
