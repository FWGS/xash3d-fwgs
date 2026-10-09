/*
window_insets.m - window safe area insets
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

#import <UIKit/UIKit.h>
#include "build.h"

// SDL3 has SDL_GetWindowSafeArea
#if XASH_SDL == 2
#include <SDL.h>
#include <SDL_syswm.h>

void IOS_GetWindowInsets( void *window, float insets[4] )
{
	SDL_SysWMinfo info;
	UIWindow *nativeWindow;
	CGSize size;
	UIEdgeInsets safe;

	insets[0] = insets[1] = insets[2] = insets[3] = 0.0f;

	SDL_VERSION( &info.version );
	if( !window || !SDL_GetWindowWMInfo( window, &info ) || info.subsystem != SDL_SYSWM_UIKIT )
		return;

	nativeWindow = info.info.uikit.window;
	if( !nativeWindow )
		return;

	if( @available( iOS 11.0, * ))
	{
		// UIKit reports points while the renderer may use Retina pixels, so return fractions
		size = nativeWindow.bounds.size;
		if( size.width <= 0 || size.height <= 0 )
			return;

		safe = nativeWindow.safeAreaInsets;
		insets[0] = safe.left / size.width;
		insets[1] = safe.top / size.height;
		insets[2] = safe.right / size.width;
		insets[3] = safe.bottom / size.height;
	}
}
#endif // XASH_SDL == 2
