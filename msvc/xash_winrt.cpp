/*
xash_winrt.cpp - Xash3D UWP main func
Copyright (C) 2019 MoeMod Hymei

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <SDL.h>
#include <Windows.h>
#include <vector>

typedef void(*pfnChangeGame)(const char *progname);
typedef int(*pfnInit)(int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func);

extern "C" int Host_Main(int szArgc, char** szArgv, const char* szGameDir, int chg, void* callback);

int main(int argc, char **argv)
{
	std::vector<char*> av{ "-game", "valve", "-console", "-developer" };
	std::copy_n(argv, argc, std::back_inserter(av));
	
	Host_Main(av.size(), av.data(), "valve", 0, NULL);

	return 0;
}
