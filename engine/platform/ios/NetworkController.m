//
//  networkController.m
//
//  Created by Richard Dearlove on 21/10/2008.
//  Copyright 2008 DiddySoft. All rights reserved.
//

#import "NetworkController.h"

@implementation  NetworkController

// Return the localized IP address

// ----------------------------------------------------------------------------------------------------------
+ (NSString *)localWifiIPAddress
// ----------------------------------------------------------------------------------------------------------

{
	NSString *address = @"error";
	struct ifaddrs *interfaces = NULL;
	struct ifaddrs *temp_addr = NULL;
	int success = 0;
	
	// retrieve the current interfaces - returns 0 on success
	success = getifaddrs(&interfaces);
	if (success == 0)
	{
		// Loop through linked list of interfaces
		temp_addr = interfaces;
		while(temp_addr != NULL)
		{
			if(temp_addr->ifa_addr->sa_family == AF_INET)
			{
				// Check if interface is en0 which is the wifi connection on the iPhone
				if([[NSString stringWithUTF8String:temp_addr->ifa_name] isEqualToString:@"en0"])
				{
					// Get NSString from C String
					address = [NSString stringWithUTF8String:inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr)];
				}
			}
			
			temp_addr = temp_addr->ifa_next;
		}
	}
	
	// Free memory
	freeifaddrs(interfaces);
	
	return address;
}
// ----------------------------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------------------------
+ (NSString *) localIPAddress
// ----------------------------------------------------------------------------------------------------------
{
	char baseHostName[255];
	gethostname(baseHostName, 255);
	
	// Adjust for iPhone -- add .local to the host name
	char hn[255];
	sprintf(hn, "%s.local", baseHostName);
	
	struct hostent *host = gethostbyname(hn);
    if (host == NULL)
	{
        herror("resolv");
		return NULL;
	}
    else {
        struct in_addr **list = (struct in_addr **)host->h_addr_list;
		return [NSString stringWithCString:inet_ntoa(*list[0])];
    }
	
	return NULL;
}

// ----------------------------------------------------------------------------------------------------------
+ (BOOL)addressFromString:(NSString *)IPAddress address:(struct sockaddr_in *)address
// ----------------------------------------------------------------------------------------------------------
{
	if (!IPAddress || ![IPAddress length]) {
		return NO;
	}
	
	memset((char *) address, sizeof(struct sockaddr_in), 0);
	address->sin_family = AF_INET;
	address->sin_len = sizeof(struct sockaddr_in);
	
	int conversionResult = inet_aton([IPAddress UTF8String], &address->sin_addr);
	if (conversionResult == 0) {
		NSAssert1(conversionResult != 1, @"Failed to convert the IP address string into a sockaddr_in: %@", IPAddress);
		return NO;
	}
	
	return YES;
}

// ----------------------------------------------------------------------------------------------------------
+ (NSString *) getIPAddressForHost: (NSString *) theHost
// ----------------------------------------------------------------------------------------------------------
{
	struct hostent *host = gethostbyname([theHost UTF8String]);
	
    if (host == NULL) {
        herror("resolv");
		return NULL;
	}
	
	struct in_addr **list = (struct in_addr **)host->h_addr_list;
	NSString *addressString = [NSString stringWithCString:inet_ntoa(*list[0])];
	return addressString;
}


// ----------------------------------------------------------------------------------------------------------
+ (BOOL) hostAvailable: (NSString *) theHost
// ----------------------------------------------------------------------------------------------------------
{
	
	NSString *addressString = [self getIPAddressForHost:theHost];
	if (!addressString) 
	{
		printf("Error recovering IP address from host name\n");
		return NO;
	}
	
	struct sockaddr_in address;
	BOOL gotAddress = [self addressFromString:addressString address:&address];
	
	if (!gotAddress)
	{
		printf("Error recovering sockaddr address from %s\n", [addressString UTF8String]);
		return NO;
	}
	
	SCNetworkReachabilityRef defaultRouteReachability = SCNetworkReachabilityCreateWithAddress(NULL, (struct sockaddr *)&address);
	SCNetworkReachabilityFlags flags;
	
	BOOL didRetrieveFlags = SCNetworkReachabilityGetFlags(defaultRouteReachability, &flags);
	CFRelease(defaultRouteReachability);
	
	if (!didRetrieveFlags) 
	{
		printf("Error. Could not recover network reachability flags\n");
		return NO;
	}
	
	BOOL isReachable = flags & kSCNetworkFlagsReachable;
	return isReachable ? YES : NO;;
}


// ----------------------------------------------------------------------------------------------------------
+ (BOOL) connectedToNetwork
// ----------------------------------------------------------------------------------------------------------
{
	// Create zero addy
	struct sockaddr_in zeroAddress;
	bzero(&zeroAddress, sizeof(zeroAddress));
	zeroAddress.sin_len = sizeof(zeroAddress);
	zeroAddress.sin_family = AF_INET;
	
	// Recover reachability flags
	SCNetworkReachabilityRef defaultRouteReachability = SCNetworkReachabilityCreateWithAddress(NULL, (struct sockaddr *)&zeroAddress);	
	CFRelease(defaultRouteReachability);
	
	SCNetworkReachabilityFlags flags;
	
	BOOL didRetrieveFlags = SCNetworkReachabilityGetFlags(defaultRouteReachability, &flags);
	if (!didRetrieveFlags) 
	{
		printf("Error. Could not recover network reachability flags\n");
		return 0;
	}
	
	BOOL isReachable = flags & kSCNetworkFlagsReachable;
	BOOL needsConnection = flags & kSCNetworkFlagsConnectionRequired;
	// BOOL isEDGE = flags & kSCNetworkReachabilityFlagsIsWWAN;
	return (isReachable && !needsConnection) ? YES : NO;
}


@end
