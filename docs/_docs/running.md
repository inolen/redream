---
title: Running
---

```
redream <cdi, chd or gdi file>
```

Command line options are loaded from and saved to `$HOME/.redream/config` each run.

### All options

```
      --debug  Show debug menu                                     [default: 1]
     --aspect  Video aspect ratio                                  [default: stretch, valid: stretch, 16:9, 4:3]
     --region  System region                                       [default: auto, valid: japan, usa, europe, auto]
   --language  System language                                     [default: english, valid: japanese, english, german, french, spanish, italian]
  --broadcast  System broadcast mode                               [default: ntsc,    valid: ntsc, pal, pal_m, pal_n]
      --audio  Enable audio                                        [default: 1]
    --latency  Preferred audio latency in MS                       [default: 100]
 --fullscreen  Start window fullscreen                             [default: 0]
       --perf  Write perf-compatible maps for generated code       [default: 0]
```

### Boot ROM

The Dreamcast had a boot ROM which provided various system calls for games to use.

By default, redream will attempt to high-level emulate this boot ROM. However, this process is currently not perfect, resulting in some games failing to boot. If you have a real boot ROM you'd like to use instead, it can be placed in `$HOME/.redream/boot.bin` and will take priority over redream's HLE implementation.

Please note, the HLE implementation only works with `.gdi` discs at the moment.
