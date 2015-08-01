# dreavm

dreavm is a work in progress emulator for the SEGA Dreamcast.

## Building

```shell
git clone https://github.com/inolen/dreavm.git
mkdir dreavm_build
cd dreavm_build
cmake ../dreavm
cmake --build .
```

## Running
```
dreavm --bios=path/to/dc_bios.bin --flash=path/to/dc_flash.bin <bin or gdi file>
```

Command line flags are loaded from and saved to `$HOME/.dreavm/flags` between runs. This means that bios and flash path, etc. only need to be set on the first run.

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
