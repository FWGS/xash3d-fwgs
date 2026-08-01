## If you are reporting bugs

1. Check that you are using the latest version. To build the latest Xash3D FWGS, look at README.md.
2. Check the open issues to make sure your bug is not already reported and closed issues if it reported and fixed. Don't send a bug if it's already been reported.
3. Re-run engine with `-dev 2 -log` arguments, then reproduce the bug and post `engine.log` which can be found in your working directory.
3. Describe the steps to reproduce the bug.
4. Describe which OS and architecture you are using.
6. Attach a screenshot if it will help clarify the situation.

## If you are contributing code

### Which branch?

* We recommend using the `master` branch.

### Third-party libraries

* The philosophy of any Xash Project by Uncle Mike: don't be bloated. We follow it too.
* Adding new libraries are allowed only if there is a REAL reason to use it. It will be nice, if you leave a possibility to remove the new dependency at build-time.
* Adding new dependencies for the Waf Build System is not welcomed.

### Portability level

* Xash3D has it's own crt library. It's recommended to use it. In most cases it's just a wrapper around the standard C library.
* If your feature needs platform-specific code, move it to `engine/platform` and try to implement every supported OS and every supported compiler or at least leave a stub.
* You must put it under the appropriate macro. It's a rule: Xash3D FWGS must compile everywhere. For list of platforms we support, refer to `public/build.h`.

### Code style

* This project uses a mix of Quake's and HLSDK's C/C++ code style convention. 
* In short:
  * Use spaces in parenthesis, but not when two parentheses are consecutive.
  * Use only tabs for indentation.
  * Any brace must have it's own line.
  * Use short blocks, if statements and loops on single line are allowed.
  * Prefer generic utilities from libpublic over rolling your own.
  * Avoid magic numbers.
  * While macros are powerful, it's better to avoid overusing them.
  * If you are unsure, try to mimic the code style from anywhere else in the engine source code.
* **ANY** commit message should start from declaring  tags, in the format:
  
  `tag: added some bugs`
  
  `tag: subtag: fixed some features`
  
  The tags can be a:
  * subsystem
  * simple feature name or even just a filename, without the extension.
  Just always keep them the same because it helps keep the commit history clean and messages short.

## LLM-based tools usage.

While we wouldn't recommend using any LLM-based (also misleadingly called AI) tools, we understand that they are here to stay.

Whether you're reporting a bug or contributing to the code, you will take complete authorship and responsibility over the provided content, and the same rules will apply to you as for everybody else, so validate the bug report or the patch it before sending it.
