# Design

### audio

Audio backend implementation. Responsible for reading fully mixed data from the Dreamcast and playing it.

### core

Asserts, logging and data structures.

### hw

Contains subfolders for each of the major hardware components of the Dreamcast:

* `aica` audio chip with its own dsp and cpu to synthesize and mix audio data
* `arm7` audio cpu, part of the aica chip
* `gdrom` optical disc drive
* `holly` interface between the sh4 cpu and the pvr chip / maple bus
* `maple` interface between holly and external peripherals
* `pvr` graphics chip. processes and renders texture / polygon data
* `rom` boot and flash rom chips
* `sh4` main cpu

### jit

Contains the frontend, backend, ir and optimization passes used by the just-in-time compiler.

### sys

Cross-platform abstractions for signal handling, paths, virtual memory, threads and time.

### ui

Window creation and user interface code.

### video

Video backend implementation. Responsible for rendering parsed texture / polygon data from the Dreamcast.
