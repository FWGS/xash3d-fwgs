/*
 iosFtpServer
 Copyright (C) 2008  Richard Dearlove ( monsta )
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#import "list.h"

// ----------------------------------------------------------------------------------------------------------
NSString* createList(NSString* directoryPath)
// ----------------------------------------------------------------------------------------------------------
{
	NSFileManager *fileManager = [ NSFileManager defaultManager ];
	NSDictionary *fileAttributes;
	NSError *error;
	
	NSString*			fileType;
	NSNumber*			filePermissions;
	long				fileSubdirCount;
	NSString*			fileOwner;
	NSString*			fileGroup;
	NSNumber*			fileSize;
	NSDate*				fileModified;
	NSString*			fileDateFormatted;
	NSDateFormatter*    dateFormatter = [[[ NSDateFormatter alloc ] init ] autorelease ];
	
	BOOL				fileIsDirectory;

	
	NSMutableString*	returnString= [ NSMutableString new ];
	NSString*			formattedString;
	
	NSString*			binaryString;
	
	[returnString appendString:@"\r\n"];
	
	NSDirectoryEnumerator *dirEnum =    [fileManager  enumeratorAtPath:directoryPath];	
	NSString *filePath;
	
	NSString* firstChar;
	NSString* fullFilePath;

	[dateFormatter setDateFormat:@"MMM dd HH:mm"]; 
	NSLocale *englishLocale = [[NSLocale alloc] initWithLocaleIdentifier:@"en"]; 
	[dateFormatter setLocale:englishLocale]; 
	[englishLocale release];
	
	
	NSLog(@"Get LS for %@", directoryPath );
	int numberOfFiles = 0;
	while (filePath = [dirEnum nextObject]) {
		
		firstChar = [ filePath substringToIndex:1 ];
		
		[dirEnum skipDescendents ];			// don't go down that recursive road

		if ( ![ firstChar isEqualToString:@"."] )			// dont show hidden files
		{
			fullFilePath = [directoryPath stringByAppendingPathComponent:filePath];
			
			fileAttributes = [ fileManager attributesOfItemAtPath:fullFilePath error:&error ];
			
			fileType =		  [ fileAttributes valueForKey:NSFileType ];
			
			filePermissions		= [ fileAttributes valueForKey:NSFilePosixPermissions ];
			fileSubdirCount		= filesinDirectory(fullFilePath);
			fileOwner			= [ fileAttributes valueForKey:NSFileOwnerAccountName ];
			fileGroup			= [ fileAttributes valueForKey:NSFileGroupOwnerAccountName ];
			fileSize			= [ fileAttributes valueForKey:NSFileSize ];
			fileModified		= [ fileAttributes valueForKey:NSFileModificationDate ];
			fileDateFormatted	= [ dateFormatter stringFromDate:fileModified ];
			
			fileIsDirectory     = (fileType == NSFileTypeDirectory );
			
			
			fileSubdirCount = fileSubdirCount <1 ? 1 : fileSubdirCount;
			
			binaryString =  int2BinString([filePermissions unsignedLongValue]) ;
			binaryString = [ binaryString substringFromIndex:7 ];// snip off the front
			formattedString = [ NSString stringWithFormat:@"%@%@ %5i %12@ %12@ %10qu %@ %@", fileIsDirectory ? @"d" : @"-" ,bin2perms(binaryString),fileSubdirCount, fileOwner, fileGroup, [fileSize unsignedLongLongValue], fileDateFormatted , filePath ];
			
			[ returnString appendString:formattedString ];
			[ returnString appendString:@"\n" ];
			numberOfFiles++;
		}
	}
	[returnString insertString: [NSString stringWithFormat:@"total %d", numberOfFiles] atIndex:0];
	//	NSLog(returnString );
	return [returnString autorelease];																				// FIXME - release count
	
}
// ----------------------------------------------------------------------------------------------------------
int filesinDirectory(NSString* filePath )
// ----------------------------------------------------------------------------------------------------------
{
	int no_files =0;
	NSFileManager *fileManager = [ NSFileManager defaultManager ];
	NSDirectoryEnumerator *dirEnum =    [fileManager  enumeratorAtPath:filePath];	
	
	while (filePath = [dirEnum nextObject]) {
		[dirEnum skipDescendents ];										// don't want children
		no_files++;
	}
	
	return no_files;
}
// ----------------------------------------------------------------------------------------------------------
NSMutableString* int2BinString(int x)
// ----------------------------------------------------------------------------------------------------------
{
	NSMutableString *returnString = [[ NSMutableString alloc ] init];
	int hi, lo;
	hi=(x>>8) & 0xff;
	lo=x&0xff;
	
	[ returnString appendString:byte2String(hi) ];
	[ returnString appendString:byte2String(lo) ];
	return [ returnString autorelease ];
}



// ----------------------------------------------------------------------------------------------------------
NSMutableString *byte2String(int x )
// ----------------------------------------------------------------------------------------------------------
{
	NSMutableString *returnString  = [[ NSMutableString alloc ]init];
	
	int n;
	
	for(n=0; n<8; n++)
	{		
		if((x & 0x80) !=0)
		{
			
			[ returnString appendString:@"1"];
			
			
		}
		else
		{
			[ returnString appendString:@"0"];
		}
		x = x<<1;
	}
	
	return [returnString autorelease];
}

// ----------------------------------------------------------------------------------------------------------
NSMutableString *bin2perms(NSString *binaryValue)
// ----------------------------------------------------------------------------------------------------------
{
	NSMutableString *returnString = [[ NSMutableString alloc ] init];
	NSRange subStringRange;
	subStringRange.length = 1;
	NSString *replaceWithChar;
	
	for (int n=0; n < [binaryValue length]; n++) 
	{
		subStringRange.location = n;
		// take the char
		// if pos = 0, 3,6
		if ( n == 0 || n == 3 || n ==6)
		{
			replaceWithChar = @"r";
		}
		if ( n == 1 || n == 4 || n ==7)
		{
			replaceWithChar = @"w";			
		}
		if ( n == 2 || n == 5 || n ==7)
		{
			replaceWithChar = @"x";
		}
		
		if ( [[binaryValue substringWithRange:subStringRange ] isEqualToString:@"1" ] )
		{
			[ returnString appendString:replaceWithChar ];
		}
		else
		{
			[ returnString appendString:@"-" ];
		}
		
	}
	
	return [ returnString autorelease ];
}

