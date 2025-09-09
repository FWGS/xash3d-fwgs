## If you are reporting bugs

1. Check you are using latest version. You can build latest Xash3D FWGS for yourself, look to README.md.
2. Check open issues is your bug is already reported and closed issues if it reported and fixed. Don't send bug if it's already reported.
3. Re-run engine with `-dev 2 -log` arguments, reproduce bug and post engine.log which can be found in your working directory.
3. Describe steps to reproduce bug.
4. Describe which OS and architecture you are using.
6. Attach screenshot if it will help clarify the situation.

## If you are contributing code

### Which branch?

* We recommend using `master` branch.

### Third-party libraries

* Philosophy of any Xash Project by Uncle Mike: don't be bloated. We follow it too.
* Adding new library is allowed only if there is a REAL reason to use it. It's will be nice, if you will leave a possibility to remove new dependency at build-time.
* Adding new dependencies for Waf Build System is not welcomed.

### Portability level

* Xash3D have it's own crt library. It's recommended to use it. It most cases it's just a wrappers around standart C library.
* If your feature need platform-specific code, move it to `engine/platform` and try to implement to every supported OS and every supported compiler or at least leave a stubs.
* You must put it under appopriate macro. It's a rule: Xash3D FWGS must compile everywhere. For list of platforms we support, refer to public/build.h file.

### Code style

* This project uses mixed Quake's and HLSDK's C/C++ code style convention. 
* In short:
  * Use spaces in parenthesis.
  * Only tabs for indentation.
  * Any brace must have it's own line.
  * Short blocks, if statements and loops on single line are allowed.
  * Avoid magic numbers.
  * While macros are powerful, it's better to avoid overusing them.
  * If you unsure, try to mimic code style from anywhere else of engine source code.
* **ANY** commit message should start from declaring a tags, in format:
  
  `tag: added some bugs`
  
  `tag: subtag: fixed some features`
  
  Tags can be any: subsystem, simple feature name or even just a filename, without extension.
  Just keep them always same, it helps keep history clean and commit messages short.

## LLM-based tools usage.

While we wouldn't recommend using any LLM-based (also misleadingly called AI) tools, we understand that they are here to stay.

Whether you're reporting bug or contributing the code, you take complete authorship and responsibility over provided content and the same rules will apply to you as for everybody else, so validate the bug report or the patch before sending it.
