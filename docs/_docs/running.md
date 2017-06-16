---
title: Running
---

```
redream <cdi, gdi or bin file>
```

Command line options are loaded from and saved to `$HOME/.redream/config` each run.

### All options

```
     --region  System region                                       [default: america, valid: japan, america, europe]
   --language  System language                                     [default: english, valid: japanese, english, german, french, spanish, italian]
  --broadcast  System broadcast mode                               [default: ntsc,    valid: ntsc, pal, pal_m, pal_n]
      --audio  Enable audio                                        [default: 1]
    --latency  Preferred audio latency in MS                       [default: 100]
    --verbose  Enable debug logging                                [default: 0]
       --perf  Write perf-compatible maps for generated code       [default: 0]
```

### Boot ROM

The Dreamcast had a boot ROM which provided various system calls for games to use.

By default, redream will attempt to high-level emulate this boot ROM. However, this process is currently not perfect, resulting in some games failing to boot. If you have a real boot ROM you'd like to use instead, it can be placed in `$HOME/.redream/boot.bin` and will take priority over redream's HLE implementation.
