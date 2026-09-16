/*
Downloader.m - do not include this file, only copy the header
Copyright (C) 2015-2025 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#import "Downloader.h"
#import <CommonCrypto/CommonCrypto.h>
#import <ZipArchive/ZipArchive.h>
#import "Globals.h"
#import "Alert.h"

NSString *const RELEASE_BASE_URL = @"https://github.com/FWGS/hlsdk-mega-build/releases/download/continuous";
NSString *const MANIFEST_URL = @"/manifest.json";
NSUInteger const MANIFEST_VERSION = 1;

@implementation LibDownloader

-(instancetype)init {
	[self fetchManifest];

	return [super init];
}

-(void)fetchManifest {
	if (self.manifest)
		return;
	
	NSString *manifestPath = [libraryDirectory stringByAppendingPathComponent:@"manifest.plist"];
	NSString * __block errormessage;
	
	NSURL *manifestURL = [NSURL URLWithString:[RELEASE_BASE_URL stringByAppendingString:MANIFEST_URL]];
	NSURLSessionDataTask *task = [NSURLSession.sharedSession dataTaskWithURL:manifestURL completionHandler:^(NSData *data, NSURLResponse *response, NSError *error){
		if (error)
		{
			errormessage = [NSString stringWithFormat:@"Request for manifest failed with error: %@", error.localizedDescription];
			return;
		}
		
		NSHTTPURLResponse *httpResponse = (NSHTTPURLResponse*)response;
		if (httpResponse.statusCode < 200 || httpResponse.statusCode > 299 )
		{
			errormessage = [NSString stringWithFormat:@"Request for manifest returned with non-success code: %ld", httpResponse.statusCode];
			return;
		}
		
		NSError *serializationerror;
		self.manifest = [NSJSONSerialization JSONObjectWithData:data options:0 error:&serializationerror];
		
		if (serializationerror)
		{
			errormessage = [NSString stringWithFormat:@"Failed to create JSON object with error: %@", serializationerror.localizedDescription];
			self.manifest = nil;
			if ([NSFileManager.defaultManager fileExistsAtPath:manifestPath])
			{
				self.manifest = [NSDictionary dictionaryWithContentsOfFile:manifestPath];
			}
			return;
		}
		
		NSString *manifestver = [self.manifest valueForKey:@"version"];
		if ([manifestver integerValue] != MANIFEST_VERSION)
		{
			errormessage = [NSString stringWithFormat:@"Manifest has version %@ but expected %lu", manifestver, (unsigned long)MANIFEST_VERSION];
			self.manifest = nil;
			if ([NSFileManager.defaultManager fileExistsAtPath:manifestPath])
			{
				self.manifest = [NSDictionary dictionaryWithContentsOfFile:manifestPath];
			}
			return;
		}
		
		[self.manifest writeToFile:manifestPath atomically:NO];
	}];
	
	[task resume];
	@autoreleasepool {
		while (task.state != NSURLSessionTaskStateCompleted) {
			[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];
		}
	}
	
	if (errormessage)
		[Alert displayErrorAlertWithMessage:errormessage];
}

- (BOOL)fetchLibsForGame:(Game *)game {
	t_gameentry entry;
	
	if(![self setEntry:&entry forGame:game])
	{
		//unsupported
		return NO;
	}
	
	NSString *libszip = [libraryDirectory stringByAppendingPathComponent:entry.filename];
	NSURL *libsdest = [NSURL fileURLWithPath:libszip.stringByDeletingPathExtension];
	if (![NSFileManager.defaultManager fileExistsAtPath:libszip] || ![[self sha256ForFileAtPath:libszip] isEqualToString:entry.sha256] )
	{
		NSError *deleteerror;
		[NSFileManager.defaultManager removeItemAtPath:libszip error:&deleteerror];
		
		if (deleteerror)
			NSLog(@"Deleting existing zip file failed with error: %@", deleteerror.localizedDescription);
		
		NSURL *libsurl = [NSURL URLWithString:[RELEASE_BASE_URL stringByAppendingPathComponent:entry.filename]];
		NSString * __block errormessage;
		NSURLSessionTask *task = [NSURLSession.sharedSession downloadTaskWithURL:libsurl completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
			if (error)
			{
				errormessage = [NSString stringWithFormat:@"Failed to download libraries for %@ with error: %@", game.gameInfo->title, error.localizedDescription];
				return;
			}
			
			NSHTTPURLResponse *httpResponse = (NSHTTPURLResponse*)response;
			if (httpResponse.statusCode < 200 || httpResponse.statusCode > 299 )
			{
				errormessage = [NSString stringWithFormat:@"Request for %@'s libraries returned with non-success code: %ld", game.gameInfo->title, (long)httpResponse.statusCode];
				return;
			}
			
			NSError *copyerror;
			[NSFileManager.defaultManager copyItemAtURL:location toURL:[NSURL fileURLWithPath:libszip] error:&copyerror];
			
			if (copyerror)
			{
				errormessage = [NSString stringWithFormat:@"Failed to copy downloaded file: %@", error.localizedDescription];
			}
						
			NSError *directoryerror;
			[NSFileManager.defaultManager createDirectoryAtURL:libsdest withIntermediateDirectories:YES attributes:nil error:&directoryerror];
			
			if (directoryerror)
				NSLog(@"Failed to create directory %@, error: %@", libsdest, directoryerror.localizedDescription);
			
			if (![SSZipArchive unzipFileAtPath:libszip toDestination:libsdest.path])
			{
				errormessage = @"Failed to unzip game libs";
				return;
			}
			
		}];
		
		[task resume];
		
		@autoreleasepool {
			while (task.state == NSURLSessionTaskStateRunning) {
				[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];
			}
		}
		
		if (errormessage)
			[Alert displayErrorAlertWithMessage:errormessage];
		
		return YES;
	}
	else
	{
		
		NSError *directoryerror;
		[NSFileManager.defaultManager createDirectoryAtURL:libsdest withIntermediateDirectories:YES attributes:nil error:&directoryerror];
		
		if (directoryerror)
			NSLog(@"Failed to create directory %@, error: %@", libsdest, directoryerror.localizedDescription);
		
		if (![SSZipArchive unzipFileAtPath:libszip toDestination:libsdest.path])
		{
			[Alert displayErrorAlertWithMessage:@"Failed to unzip game libs"];
			return NO;
		}

		return YES;
	}
}

-(NSString*)sha256ForFileAtPath:(NSString *)filePath {
	unsigned char digest[CC_SHA256_DIGEST_LENGTH];
	NSData *fileBytes = [NSData dataWithContentsOfFile:filePath];
	
	CC_SHA256(fileBytes.bytes, (uint)fileBytes.length, digest);
	
	NSMutableString *checksum = [[NSMutableString alloc] initWithCapacity:CC_SHA256_DIGEST_LENGTH];
	for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
	{
		[checksum appendFormat:@"%02x", digest[i]];
	}
	
	return checksum;
}

-(BOOL)setEntry:(t_gameentry*)entry forGame:(Game *)game{
	
	if (!self.manifest)
	{
		NSLog(@"No manifest!");
		return NO;
	}
	
	NSDictionary *mods = [self.manifest valueForKey:@"mods"];
	entry->modKey = [mods valueForKey:game.URL.lastPathComponent];
	
	if (!entry->modKey)
	{
		NSLog(@"Couldn't find a key for %@, it may be unsupported.", game.gameDir);;
		return NO;
	}
	
	NSDictionary *build = [[entry->modKey valueForKey:@"builds"] valueForKey:@"ios-arm64"];
	entry->filename = [build valueForKey:@"filename"];
	entry->sha256 = [build valueForKey:@"sha256"];
	entry->sourceJson = [build valueForKey:@"source"];
	
	return YES;
}

@end

