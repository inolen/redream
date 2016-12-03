# redream

redream is a work in progress emulator for the SEGA Dreamcast.

<p align="center">
<a href="http://www.youtube.com/watch?v=kDBAweW9hD0"><img src="http://share.gifyoutube.com/vMZXGb.gif" /></a>
</p>

## Build status

[![Build Status](https://travis-ci.org/inolen/redream.svg?branch=master)](https://travis-ci.org/inolen/redream)

## Getting started

Start by cloning the repository and setting up a build directory.
```shell
git clone https://github.com/inolen/redream.git
mkdir redream_build
cd redream_build
```

Next, generate a makefile or project file for your IDE of choice. For more info on the supported IDEs, checkout the [CMake documentation](http://www.cmake.org/cmake/help/latest/manual/cmake-generators.7.html).
```shell
# Makefile
cmake -DCMAKE_BUILD_TYPE=RELEASE ../redream

# Xcode project
cmake -G "Xcode" ../redream

# Visual Studio project
cmake -G "Visual Studio 14 Win64" ../redream
```

Finally, you can either run `make` from the command line if you've generated a Makefile or load up the project file and compile the code from inside of your IDE.

The build has been tested on OSX 10.10 with clang 3.6, Ubuntu 14.04 with GCC 4.9 and Windows 8.1 with Visual Studio 2015.

## Running
```
redream --bios=path/to/dc_boot.bin --flash=path/to/dc_flash.bin <bin or gdi file>
```

Command line flags are loaded from and saved to `$HOME/.redream/flags` each run. This means that bios and flash path, etc. only need to be set on the first run.

### All options
```
     --bios  Path to BIOS                 [default: dc_boot.bin]
    --flash  Path to flash ROM            [default: dc_flash.bin]
    --debug  Start GDB debug server
     --perf  Write perf-compatible maps for generated code
  --profile  Path to controller profile
```

### Debugging

If ran with `--debug`, a server is setup on port `24690` for remote debugging of the guest SH4 code with GDB.

The server can be connected to with GDB like so:
```
set architecture sh4
target remote localhost:24690
```

## Running tests
```shell
retest
```
