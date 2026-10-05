/*
 touch_safearea.m - iOS touch viewport
 Copyright (C) 2026 Xash3D FWGS contributors
 SPDX-License-Identifier: GPL-3.0-or-later
*/
#import <UIKit/UIKit.h>
#include "build.h"
#include "touch_safearea.h"
#if XASH_SDL == 3
#include <SDL3/SDL.h>
#elif XASH_SDL == 2
#include <SDL.h>
#include <SDL_syswm.h>
#endif

float IOS_GetTouchInsets( void *window, float *left, float *top, float *right, float *bottom )
{
	*left = *top = *right = *bottom = 0;
	if( !window )
		return 0;

	UIWindow *nativeWindow = nil;
#if XASH_SDL == 3
	nativeWindow = (__bridge UIWindow *)SDL_GetPointerProperty( SDL_GetWindowProperties( window ), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL );
#elif XASH_SDL == 2
	SDL_SysWMinfo info;
	SDL_VERSION( &info.version );
	if( SDL_GetWindowWMInfo( window, &info ) && info.subsystem == SDL_SYSWM_UIKIT )
		nativeWindow = info.info.uikit.window;
#endif
	if( !nativeWindow )
		return 0;

	if( @available( iOS 11.0, * ))
	{
		// UIKit reports points, while the renderer may use Retina pixels. Return
		// fractions for both viewport transforms and points for minimum hit sizes.
		CGSize size = nativeWindow.bounds.size;
		UIEdgeInsets insets = nativeWindow.safeAreaInsets;
		if( size.width <= 0 || size.height <= 0 ||
			insets.left + insets.right >= size.width || insets.top + insets.bottom >= size.height )
			return 0;

		*left = insets.left / size.width;
		*right = insets.right / size.width;
		*top = insets.top / size.height;
		*bottom = insets.bottom / size.height;
		return size.width;
	}
	return 0;
}
