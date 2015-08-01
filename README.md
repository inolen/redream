# dreavm

dreavm is a work in progress emulator for the SEGA Dreamcast.

![Main menu](http://i.imgur.com/gkbNJnE.png)

## Building

```shell
git clone https://github.com/inolen/dreavm.git
mkdir dreavm_build
cd dreavm_build
cmake ../dreavm
cmake --build .
```

The build has been tested on OSX 10.10 with clang 3.6 and Ubuntu 14.04 with GCC 4.9. GCC 4.8 had C++11 related issues preventing it from compiling.

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
