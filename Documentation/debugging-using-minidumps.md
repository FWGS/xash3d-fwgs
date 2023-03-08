# Debugging your mod using minidump files (Windows only)
Minidump files is awesome instrument for debugging your mod after it's being released, or for catch specific crashes which are presented only in one specific configuration, but doesn't happens in other configurations or even on developer machine. It contains a lot of information useful for debugging, and therefore it size is not so small: around hundreds or even thousands of megabytes. But this is not a problem, since minidump files compresses very effectively using common algorithms.
There are short algorithm, explaining how to use this debugging instrument:

1. User starts your mod with `-minidumps` startup parameter
2. Finally, crash happened on user's machine, and minidump file being written. This file has `.mdmp` extension and located in same folder, where mod folder located
3. User packs this minidump file to .zip/.7z archive, and somehow sends it to developer
4. Developer just opens minidump file in Visual Studio, then a virtual debugging session is opened and now reasons of crash is pretty easy to detect: you can see call stack, local function variables and global variables, information about exception and a lot of other information

You can find more information about minidumps in this [awesome article](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/crash-dump-analysis?source=recommendations#writing-a-minidump).
