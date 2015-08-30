# dreavm

dreavm is a work in progress emulator for the SEGA Dreamcast.

<p align="center">
<a href="http://www.youtube.com/watch?v=kDBAweW9hD0"><img src="http://share.gifyoutube.com/vMZXGb.gif" /></a>
</p>

## Getting started

Start by cloning the repository and setting up a build directory.
```shell
git clone https://github.com/inolen/dreavm.git
mkdir dreavm_build
cd dreavm_build
```

Next, generate a makefile or project file for your IDE of choice. For more info on the supported IDEs, checkout the [CMake documentation](http://www.cmake.org/cmake/help/latest/manual/cmake-generators.7.html).
```shell
# Makefile
cmake ../dreavm

# Xcode project
cmake -G "Xcode" ../dreavm

# Visual Studio project
cmake -G "Visual Studio 14 Win64" ../dreavm
```

Finally, you can either run `make` from the command line if you've generated a Makefile or load up the project file and compile the code from inside of your IDE.

The build has been tested on OSX 10.10 with clang 3.6, Ubuntu 14.04 with GCC 4.9 and Windows 8.1 with Visual Studio 2015.

## Running
```
dreavm --bios=path/to/dc_bios.bin --flash=path/to/dc_flash.bin <bin or gdi file>
```

Command line flags are loaded from and saved to `$HOME/.dreavm/flags` each run. This means that bios and flash path, etc. only need to be set on the first run.

### All options
```
     --bios  Path to BIOS                 [default: dc_bios.bin]
    --flash  Path to flash ROM            [default: dc_flash.bin]
  --profile  Path to controller profile
```

## Running tests
```shell
dreavm_gtest
```
