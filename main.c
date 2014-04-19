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

int main(int argc, const char * argv[]) {
	sdmmd_return_t result = 0;
	if (argc == 4) {
		real_dmg = argv[1];
		real_dmg_signature = argv[2];
		ddi_dmg = argv[3];
		
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
			
			CFTypeRef version = SDMMD_AMDeviceCopyValue(dev, NULL, CFSTR(kProductVersion));

Retry:
			printf("");
			SDMMD_AMConnectionRef conn = NULL;
			result = SDMMD_AMDeviceStartService(dev, CFSTR(AMSVC_AFC), NULL, &conn);
			CheckErrorAndReturn(result);
			SDMMD_AFCConnectionRef afc = SDMMD_AFCConnectionCreate(conn);
			
			SDMMD_AFCOperationRef make_dir = SDMMD_AFCOperationCreateMakeDirectory(CFSTR("PublicStaging"));
			result = SDMMD_AFCProcessOperation(afc, &make_dir);
			CheckErrorAndReturn(result);
			
			bool not_6 = (CFStringCompare(version, CFSTR("6.0"), kCFCompareNumerically) == kCFCompareLessThan ? true : false);
			
			SDMMD_AMConnectionRef image1 = NULL;
			result = SDMMD_AMDeviceStartService(dev, CFSTR(AMSVC_MOBILE_IMAGE_MOUNT), NULL, &image1);
			CheckErrorAndReturn(result);
			
			if (not_6) {
				SDMMD_AFCOperationRef remove_real = SDMMD_AFCOperationCreateRemovePath(CFSTR("real.dmg"));
				result = SDMMD_AFCProcessOperation(afc, &remove_real);
				CheckErrorAndReturn(result);
			
				SDMMD_AFCOperationRef remove_ddi = SDMMD_AFCOperationCreateRemovePath(CFSTR("ddi.dmg"));
				result = SDMMD_AFCProcessOperation(afc, &remove_ddi);
				CheckErrorAndReturn(result);
				
				printf("%s -> real.dmg\n",real_dmg);
				result = SDMMD_AMDeviceCopyFile(NULL, NULL, NULL, afc, real_dmg, "real.dmg");
				printf("%s -> ddi.dmg\n",ddi_dmg);
				result = SDMMD_AMDeviceCopyFile(NULL, NULL, NULL, afc, ddi_dmg, "ddi.dmg");
			}
			else {
				SDMMD_AFCOperationRef remove_real = SDMMD_AFCOperationCreateRemovePath(CFSTR("PublicStaging/staging.dimage"));
				result = SDMMD_AFCProcessOperation(afc, &remove_real);
				CheckErrorAndReturn(result);
		
				SDMMD_AFCOperationRef remove_ddi = SDMMD_AFCOperationCreateRemovePath(CFSTR("PublicStaging/ddi.dimage"));
				result = SDMMD_AFCProcessOperation(afc, &remove_ddi);
				CheckErrorAndReturn(result);
				
				printf("%s -> staging.dimage\n",real_dmg);		
				result = SDMMD_AMDeviceCopyFile(NULL, NULL, NULL, afc, real_dmg, "PublicStaging/staging.dimage");
				
				printf("%s -> ddi.dimage\n",ddi_dmg);
				result = SDMMD_AMDeviceCopyFile(NULL, NULL, NULL, afc, ddi_dmg, "PublicStaging/ddi.dimage");
			}
			SDMMD_AMConnectionRef image2 = NULL;
			
			CFMutableDictionaryRef message = SDMMD_create_dict();
			CFDictionarySetValue(message, CFSTR("Command"), CFSTR("MountImage"));
			CFDictionarySetValue(message, CFSTR("ImageType"), CFSTR("Developer"));
			
						
			if (not_6) {
				result = SDMMD_AMDeviceStartService(dev, CFSTR(AMSVC_MOBILE_IMAGE_MOUNT), NULL, &image2);
				CheckErrorAndReturn(result);
				CFDictionarySetValue(message, CFSTR("ImagePath"), CFSTR("/var/mobile/Media/real.dmg"));
			}
			else {
				CFDictionarySetValue(message, CFSTR("ImagePath"), CFSTR("/var/mobile/Media/PublicStaging/staging.dimage"));
			}
			
			CFDataRef sig_data = CFDataCreateFromFilePath(real_dmg_signature);
			
			CFDictionarySetValue(message, CFSTR("ImageSignature"), sig_data);
			result = SDMMD_ServiceSendMessage(SDMMD_TranslateConnectionToSocket(image1), message, kCFPropertyListXMLFormat_v1_0);
			CheckErrorAndReturn(result);
			
			usleep(sleep_time);
			
			if (not_6) {
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
				SDMMD_AFCOperationRef new_path = SDMMD_AFCOperationCreateRenamePath(CFSTR("PublicStaging/ddi.dimage"), CFSTR("PublicStaging/staging.dimage"));
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

