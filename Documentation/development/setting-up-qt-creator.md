## Setting up Xash3D FWGS in Qt Creator

### Intro

Most versions of Qt Creator support using `compile_commands.json` as a project
file and Waf supports generating this file in a compliant format. In this
tutorial we will leverage that features.

The tutorial is written using Qt Creator 18.0, but I urge you to use whatever
latest Qt Creator version is available, as it's often has important bug fixes.

### Step 1. Configure the build.

`compile_commands.json` file, as it can be guessed from it's name, contains
every command used to invoke compiler during build. As the build process
depends on the build configuration, we have to configure it first. I'm not
going to get into details here, as basics of compiling Xash3D FWGS already
explained in `README.md`, located in repository root.

Save the configure step command somewhere, as it's going to be used later.

### Step 2. Generate `compile_commands.json` file.

Usually, this file is located in `build` directory after a successful build.
However, it's also possible to generate it manually by running follow command:

On *nix systems:

```
$ ./waf clangdb
```

Or on Windows:

```
> waf.bat clangdb
```

### Step 3. Load the project in Qt Creator.

To do this, you need to be sure that `Compilation Databases` plugin is enabled.
As it's considered experimental, it probably needs to be enabled manually. It
can be done in `Help` -> `About Plugins...`. Locate it in `Build Systems`
category, put the checkbox and press `OK`. The IDE will ask for restart, so do
this.

After IDE has been restarted, you can use `File` -> `Open File or Project...`.
Navigate to Xash3D FWGS directory, then to `build` directory and select
`compile_commands.json` file. If everything is done correctly, IDE will ask to
set up the project. Press `Configure Project` button, as most options there are
usually unapplicable to us.

Next IDE will show project tree, however you might not see any files. This is
happens as Qt Creator "thinks" our project root located in `build` directory.
Thankfully it provides a way to override this. To fix it, click with right
mouse button on the root of project tree (it's usually called something like
`build [master]`) and select `Change Root Directory`. In opened window choose
Xash3D FWGS repository root and press `OK`.

### Step 4. Tell IDE how to build this project.

Open `Projects` on the left menu and choose `Build Settings` tab. You should
see an empty configuration for build and clean steps. Press on `Add Build Step`
button and choose `Custom Process Step`.

In created step you should see `Command`, `Arguments` and `Working Directory`.

For the command, choose the Python executable. For arguments, use configure
command for Waf from the first step. Working directory is usually set by IDE
to `%{buildDir}`. You need to select the upper directory, where the `waf` is
located. To save some time, it can be specified as `%{buildDir}/../`.

Add another custom step, fill it with the same command and working directory.
For arguments use `waf build`.

Below you should see `Clean steps`. Create a clean step the same way, fill the
arguments and working directory as usual, for arguments use `waf clean`.

### Step 5. Tell IDE how to copy built binaries.

Now select `Deploy Settings` tab. Press `Add Deploy Step` -> `Custom Process
Step`. Again, same command and working directory. In arguments field type:
`waf install --destdir="<here put where Xash binaries should be copied to>"`.

### Step 6. Finally, set up Qt Creator to run the engine.

Select the `Run Settings` tab. Below you might see familiar text fields:
`Executable`, `Command line arguments` and `Working directory`.

For working directory click on `Browse...` button and select the folder where
Xash3D executable will be copied to, from previous step.

For executable type `./xash3d` if you're on *nix or `xash3d.exe` if you're on
Windows. The command line arguments are the same you use to run the engine. Can
be empty as well, but I personally use `-dev 2 -log` for extra verbosity.

### Outro

At this moment, you should be able to press `Run` button, wait until build
finishes and see running Xash3D FWGS. On Windows to use the debugger you might
need the `CDB` debugger and appropriate `qtcreatorcdbext` binary for debugging
32-bit applications and a separate for 64-bit, but that's left as an exercise
for the reader and a potential improvement for this document.
