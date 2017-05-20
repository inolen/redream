---
title: Running
---

```
redream --bios=path/to/dc_boot.bin --flash=path/to/dc_flash.bin <bin or gdi file>
```

Command line flags are loaded from and saved to `$HOME/.redream/flags` each run. This means that bios and flash path, etc. only need to be set on the first run.

### All options

```
       --bios  Path to BIOS                                            [default: dc_boot.bin]
      --flash  Path to flash ROM                                       [default: dc_flash.bin]
      --audio  Enable audio                                            [default: 1]
    --latency  Preferred audio latency in MS                           [default: 100]
    --verbose  Enable debug logging                                    [default: 0]
       --perf  Write perf-compatible maps for generated code           [default: 0]
```
