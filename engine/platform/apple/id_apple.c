/*
id_apple.c - macOS hardware serial id source
Copyright (C) 2026 Xash3D FWGS Contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"
#include <AvailabilityMacros.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

// kIOMasterPortDefault was renamed to kIOMainPortDefault in macOS 12 SDK
#if MAC_OS_X_VERSION_MIN_REQUIRED < 120000
#define kIOMainPortDefault kIOMasterPortDefault
#endif

static qboolean Apple_GetPlatformExpertProperty( CFStringRef key, char *out, size_t size )
{
	io_service_t service = IOServiceGetMatchingService( kIOMainPortDefault, IOServiceMatching( "IOPlatformExpertDevice" ));

	if( !service )
		return false;

	qboolean ret = false;
	CFTypeRef prop = IORegistryEntryCreateCFProperty( service, key, kCFAllocatorDefault, 0 );

	if( prop )
	{
		if( CFGetTypeID( prop ) == CFStringGetTypeID() && CFStringGetCString( (CFStringRef)prop, out, size, kCFStringEncodingUTF8 ))
			ret = true;
		CFRelease( prop );
	}

	IOObjectRelease( service );
	return ret;
}

qboolean Apple_GetSerialNumber( char *out, size_t size )
{
	return Apple_GetPlatformExpertProperty( CFSTR( "IOPlatformSerialNumber" ), out, size );
}
