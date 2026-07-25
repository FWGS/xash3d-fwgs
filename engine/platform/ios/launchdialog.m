/*
 launchdialog.m - iOS lauch dialog
 Copyright (C) 2016 mittorn
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 */

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

#define XASHLIB "@rpath/libxash.dylib"

int szArgc;
char **szArgv;
float g_iOSVer;

//UI vars
UIAlertController *alert;
UIViewController *controller;
bool finishedDialog = false;

const char *IOS_GetDocsDir( void )
{
	static const char *dir = NULL;
	
	if( dir )
		return dir;
	
	NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
	NSString *documentsDirctory = [paths objectAtIndex:0];
	[[NSFileManager defaultManager] createDirectoryAtPath:documentsDirctory withIntermediateDirectories:YES attributes:nil error:nil];
	
	dir = [documentsDirctory fileSystemRepresentation];
	NSLog(@"IOS_GetDocsDir: %s", dir);
	
	return dir;
}

const char *IOS_GetExecDir( void )
{
	static const char *dir = NULL;
	
	if( dir )
		return dir;
	
	NSString *executableDirctory = [[NSBundle mainBundle] bundlePath];
	
	dir = [executableDirctory fileSystemRepresentation];
	NSLog(@"IOS_GetExecDir: %s", dir);
	
	return dir;
}


#define SETTINGS_MAGIC 111

typedef struct settings_s
{
	unsigned char magic;
	char args[1024];
} settings_t;

settings_t settings;
FILE *settingsfile;
char settingspath[256];

static void SaveSettings( void ) 
{
	if( (settingsfile = fopen( settingspath, "wb" )) )
	{
		strlcpy( settings.args, [alert.textFields.firstObject.text UTF8String], 1024 );
		settings.magic = 111;
	
		fwrite( &settings, sizeof(settings), 1, settingsfile );
		fclose( settingsfile );
	}
}

static void IOS_PrepareView( void )
{
	UIWindow *window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
	controller = [[UIViewController alloc] init];
	[controller.view setBackgroundColor:[UIColor grayColor]];
	[window setRootViewController:controller];
	[window makeKeyAndVisible];
	alert = [UIAlertController alertControllerWithTitle:@"Xash3D FWGS" 
	message:nil
	preferredStyle:UIAlertControllerStyleAlert];

	UIAlertAction* startAction = [UIAlertAction actionWithTitle:@"Start" style:UIAlertActionStyleDefault
   	handler:^(UIAlertAction *action) { finishedDialog = true; }];
	UIAlertAction* exitAction = [UIAlertAction actionWithTitle:@"Exit" style:UIAlertActionStyleDefault
   	handler:^(UIAlertAction *action) { 
	SaveSettings();
	NSLog(@"Exit Selected\n");
	exit(0); }];
	[alert addAction:startAction];
	[alert addAction:exitAction];
}

void IOS_LaunchDialog( void )
{
	NSLog(@"System Version is %@",[[UIDevice currentDevice] systemVersion]);
	NSString *ver = [[UIDevice currentDevice] systemVersion];
	g_iOSVer = [ver floatValue];

	//request microphone permissions otherwise we will crash when joining an online server
	[[AVAudioSession sharedInstance] requestRecordPermission:^(BOOL granted){}];

	IOS_PrepareView();	

	const char *docsDir = IOS_GetDocsDir();

	//set working directory to documents so logs can be generated there
	[[NSFileManager defaultManager] 
	changeCurrentDirectoryPath:[NSString stringWithUTF8String:IOS_GetDocsDir()]];
	
	snprintf( settingspath, sizeof( settingspath ), "%s/settings.bin", docsDir );
	settingspath[255] = 0;
 
	settingsfile = fopen( settingspath, "rb" );
	bool ret = false;
	if ( settingsfile ) 
	{
		ret = fread( &settings, sizeof( settings ), 1, settingsfile ) == 1;
	}


	[alert addTextFieldWithConfigurationHandler:^(UITextField *textField) {
		[textField setPlaceholder:@"Launch Options"];
		if (ret && ( settings.magic == SETTINGS_MAGIC ))
		{
			[textField setText:@(settings.args)];
		}
		else
		{
			[textField setText:@"-dev 2 -log"];
		}
		[textField setAutocapitalizationType:UITextAutocapitalizationTypeNone];
	}];

	[controller presentViewController:alert animated:YES completion:nil];

	@autoreleasepool {
		while( !finishedDialog ) {
			[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];
		}
	}

	NSArray *argv = [alert.textFields.firstObject.text componentsSeparatedByString:@" "];

	SaveSettings();
	
	int count = [argv count];
	szArgv = calloc( count + 2, sizeof( char* ) );
	int i;
	for( i = 0; i<count; i++ )
	{
		szArgv[i + 1] = strdup( [argv[i] UTF8String] );
	}
	szArgc = count + 1;
	szArgv[count + 1] = 0;

	[alert release];
}

char *IOS_GetUDID( void )
{
	static char udid[256];
	NSString *id = [[[UIDevice currentDevice]identifierForVendor] UUIDString];
	strncpy( udid, [id UTF8String], 255 );
	[id release];
	return udid;
}

void IOS_Log( const char *text )
{
	NSLog(@"Xash: %@", [NSString stringWithUTF8String:text]);
}

int IOS_GetArgs( char ***out )
{
	*out = szArgv;
	return szArgc;
}
