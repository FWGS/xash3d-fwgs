# Contributing to Xash3D FWGS

Third-party patches for Xash3D FWGS are great idea. 
But if you want your patch to be accepted, we can give you few guidelines.

## If you are reporting bugs

1. Check you are using latest version. You can build latest Xash3D FWGS for yourself, look to README.md.
2. Check open issues is your bug is already reported and closed issues if it reported and fixed. Don't send bug if it's already reported.
3. Now you can write bugreport.
4. Describe steps to reproduce bug.
5. Describe which OS and architecture you are using.
6. Re-run engine with `-dev 2 -log` arguments, reproduce bug and post engine.log which can be found in your working directory.
7. Attach screenshot if it will help clarify the situation.

## If you are contributing code

### Which branch?

* We recommend using `master` branch. But you still can use any of our branches, if they are up-to-date.

### Third-party libraries

* Philosophy of any Xash Project by Uncle Mike: don't be bloated. We follow it too.
* There is allowed only these libraries, which is used in Linux GoldSrc version or if there is a REAL reason to use library.

### Portability level

* Xash3D have it's own crt library. It's recommended to use it. It most cases it's just a wrappers around standart C library.
* If your feature need platform-specific code, try to implement to every supported OS and every supported compiler.
* You must put it under appopriate macro. It's a rule: Xash3D FWGS must at least compile everywhere.

| OS | Macro |
| -- | ----- |
| Linux | `defined(__linux__)` |
| FreeBSD | `defined(__FreeBSD__)` |
| NetBSD | `defined(__NetBSD__)` |
| OpenBSD | `defined(__OpenBSD__)` |
| OS X/iOS | `defined(__APPLE__)` and TargetConditionals macros |
| Windows | `defined(_WIN32)` |
| Android | `defined(__ANDROID__)` |
| Emscripten | `defined(__EMSCRIPTEN__)` |

### Code style

* This project uses mixed Quake's and HLSDK's C/C++ code style convention. 
* In short:
  * Use spaces in parenthesis. Even in empty.
  * Use tabs, not spaces, for indentation.
  * Any brace must have it's own line.
  * Short blocks, if statements and loops on single line are allowed.
  * Try to avoid magic numbers.
* If you unsure, try to mimic code style from anywhere else of engine source code.
