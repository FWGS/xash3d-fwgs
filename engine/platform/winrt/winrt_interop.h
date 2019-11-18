
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
	
void WinRT_FullscreenMode_Install(int fullscreen);
void WinRT_BackButton_Install();
void WinRT_SaveVideoMode(int w, int h);
float WinRT_GetDisplayDPI();
char* WinRT_GetUserName();

#ifdef __cplusplus
}
#endif
