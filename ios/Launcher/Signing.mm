/*
Signing.mm
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#import <Foundation/Foundation.h>
#import "Signing.h"
#import "common.h"
#import "macho.h"

@implementation Signer

+ (BOOL)signAtPath:(NSString*)machoPath withCertificate:(nonnull NSData *)certData withProvision:(nullable NSData *)provisionData password:(nullable NSString *)password {
	
	ZSignAsset zsa = ZSignAsset();
	if (!zsa.InitSimple(certData.bytes, (int)certData.length, provisionData ? provisionData.bytes : nil, provisionData ? (int)provisionData.length : 0, password ? password.UTF8String : ""))
	{
		NSLog(@"Provided certificate data or password is invalid!");
		return NO;
	}
	
	ZMachO macho = ZMachO();
	if (!macho.Init(machoPath.UTF8String)) {
		NSLog(@"Specified URL doesn't point to a valid macho binary");
		return NO;
	}
	
	string bundle = NSBundle.mainBundle.bundleIdentifier.UTF8String;
	if (!macho.Sign(&zsa, true, bundle, "", "", ""))
	{
		NSLog(@"Failed to sign binary");
		return NO;
	}
	
	return YES;
}

+ (BOOL)signAtURL:(NSURL*)machoURL withCertificate:(nonnull NSData *)certData withProvision:(nullable NSData *)provisionData password:(nullable NSString *)password {
	
	ZSignAsset zsa = ZSignAsset();
	if (!zsa.InitSimple(certData.bytes, (int)certData.length, provisionData ? provisionData.bytes : nil, provisionData ? (int)provisionData.length : 0, password ? password.UTF8String : ""))
	{
		NSLog(@"Provided certificate data or password is invalid!");
		return NO;
	}
	
	ZMachO macho = ZMachO();
	if (!macho.Init(machoURL.path.UTF8String)) {
		NSLog(@"Specified URL doesn't point to a valid macho binary");
		return NO;
	}
	
	string bundle = NSBundle.mainBundle.bundleIdentifier.UTF8String;
	if (!macho.Sign(&zsa, true, bundle, "", "", ""))
	{
		NSLog(@"Failed to sign binary");
		return NO;
	}
	
	return YES;
}



@end
