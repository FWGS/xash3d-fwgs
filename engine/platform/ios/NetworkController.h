//
//  networkController.h
//  DiddyDJ
//
//  Created by Richard Dearlove on 21/10/2008.
//  Copyright 2008 DiddySoft. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
 #include <unistd.h>
#include <ifaddrs.h>

@interface NetworkController : NSObject {

}
+ (NSString *)localWifiIPAddress;
+ (NSString *) localIPAddress;
+ (BOOL)addressFromString:(NSString *)IPAddress address:(struct sockaddr_in *)address;
+ (NSString *) getIPAddressForHost: (NSString *) theHost;
+ (BOOL) hostAvailable: (NSString *) theHost;
+ (BOOL) connectedToNetwork;
@end
