---
title: Running
---

```
redream --bios=path/to/dc_boot.bin <cdi, gdi or bin file>
```

Command line flags are loaded from and saved to `$HOME/.redream/flags` each run. This means that bios path, etc. only need to be set on the first run.

### All options

```
       --bios  Path to BIOS                                        [default: dc_boot.bin]
     --region  System region                                       [default: america, valid: japan, america, europe]
   --language  System language                                     [default: english, valid: japanese, english, german, french, spanish, italian]
  --broadcast  System broadcast mode                               [default: ntsc,    valid: ntsc, pal, pal_m, pal_n]
      --audio  Enable audio                                        [default: 1]
    --latency  Preferred audio latency in MS                       [default: 100]
    --verbose  Enable debug logging                                [default: 0]
       --perf  Write perf-compatible maps for generated code       [default: 0]
```
