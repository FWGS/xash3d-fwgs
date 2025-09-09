Xash3D FWGS is intended to be easily portable for various platforms, however main issue is maintaining such ports. 

This page is about merged ports to main source tree and responsible for it developers.

For porting guidelines, read engine-porting-guide.md.

Status: 
* **Supported**: active, confirmed to be fully functional, gets built on CI.
* **Orphaned**: some work was done but not finished or actively tested due to lack of human resources.
* **In progress**: active, under development.
* **Old Engine**: port was for old engine fork.
* **Deprecated**: not supported anymore.

Table is sorted by status and platform.

| Platform        | Status                     | Maintainer               | Note
| --------        | ------                     | ----------               | ----
| Android         | Supported                  | @Velaron                 |
| *BSD            | Supported                  | @nekonomicon             |
| GNU/Linux       | Supported                  | @a1batross, @mittorn     |
| macOS           | Supported                  | @sofakng                 | 
| PSVita          | Supported                  | @fgsfdsfgs               |
| Switch          | Supported                  | @fgsfdsfgs               |
| Windows         | Supported                  | @a1batross, @SNMetamorph |
| DOS4GW          | Orphaned                   | N/A                      | Haven't been confirmed to work for a very long time
| Haiku           | Orphaned                   | N/A                      | Was added by #478 and #483
| IRIX            | Orphaned                   | N/A                      | Undone, compiles but requires big endian port
| MotoMAGX        | Orphaned                   | N/A                      | Should work but the compiler used for this platform is very unstable and easy to crash (it's GCC 3.4)
| SerenityOS      | Orphaned                   | N/A                      | Works but not throughly tested
| Solaris         | Orphaned                   | N/A                      | Works but not throughly tested
| WebAssembly System Interface | Orphaned      | N/A                      | Undone, WASI is missing a lot of APIs we want to use
| Dreamcast       | In progress                | @maximqaxd               | [GitHub Repository](https://github.com/maximqaxd/xash3d-fwgs_dc/)
| PSP             | In progress                | @Crow_bar, @Velaron      | [GitHub Repository](https://github.com/Crow-bar/xash3d-fwgs)
| Wii             | In progress                | Collaborative effort     | [GitHub Repository](https://github.com/saucesaft/xash3d-wii)
| Emscripten      | Old Engine                 | N/A                      | 
| 3DS             | Old Engine fork            | N/A                      | [GitHub Repository](https://github.com/masterfeizz/Xash3DS)
| Oculus Quest    | Old Engine fork            | N/A                      | [GitHub Repository](https://github.com/DrBeef/Lambda1VR)
| iOS             | Deprecated                 | N/A                      | See GitHub issue #61
