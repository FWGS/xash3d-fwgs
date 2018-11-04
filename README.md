# Xash3D FWGS Engine

Xash3D FWGS is a fork of Xash3D Engine by Unkle Mike.

```
Xash3D is a game engine, aimed to provide compatibility with Half-Life Engine, 
as well as to give game developers well known workflow and extend it.
Read more about Xash3D on ModDB: https://www.moddb.com/engines/xash3d-engine
```

## Fork features
* HLSDK 2.4 support.
* Crossplatform: officially supported x86 and ARM on Windows/Linux/BSD/macOS/Android/iOS/Haiku.
* Modern compilers support, say no more to VC6.
* Better multiplayer support: multiple master servers, headless dedicated server.
* Mobility API, which allows better game integration on mobile devices(vibration, touch controls)
* Different input methods: touch, gamepad and classic mouse & keyboard.
* TrueType font rendering, as a part of mainui_cpp.
* A set of small improvements, without broken compatibility.

## Planned fork features
* Voice support
* Multiple renderers support(OpenGL, GLES, Vulkan, software)

## Contributing
* Before sending an issue, check if someone already reported your issue. Make sure you're following "How To Ask Questions The Smart Way" guide by Eric Steven Raymond. Read more: http://www.catb.org/~esr/faqs/smart-questions.html
* Before sending a PR, check if you followed our coding guide in CODING_STYLE.md file.

## Build instructions
We are using Waf build system. If you have some Waf-related questions, I recommend you to read https://waf.io/book/

1) Clone this repository: `git clone --recursive https://github.com/FWGS/xash3d-fwgs`
2) Examine which build options are available: `waf --help`
3) Configure build: `waf configure`
4) Compile: `waf build`
5) Install(optional): `waf install`

## Running
1) Copy libraries and main executable somewhere, if you're skipped installation stage.
2) Copy game files to same directory
3) Run `xash3d.exe`/`xash3d.sh`/`xash3d` depending on which platform you're using.

For additional info, run Xash3D with `-help` command line key.
