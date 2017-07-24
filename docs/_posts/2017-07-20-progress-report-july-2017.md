---
title:  "Progress Report July 2017"
date:   2017-07-20
---

After a slow period at the beginning of the year, the pace has started picking up the past few months on redream's development.

I wanted to take some time to bring everyone up to date on where the development is at, and where it's planning to go by the end of the year.

# Where Development Is At

My focus with respect to development mostly centralizes around stability and usability. There's been a lot of work on getting games running, making them performant and making sure they're easy to play.

Many grahpical bells and whistles from the PowerVR are still unsupported, and similarly, quite a few audio features are still missing from the AICA emulation code. However, in the past few months tens of new games are running, and the graphics and audio emulation is enough such that many of the games are fun to play through.

## Audio Improvements

Audio in redream has been lacking for a _long_ time. Up until 9 months ago, there was absolutely zero audio support.

In around November, there was a major push to write the initial interpreter for the Dreamcast's audio processor, wire it up and get 8-bit / 16-bit PCM audio working. Most background music used this format, which was a step forward. However, almost all of the sound effects used the ADPCM format, which meant most games were still not very immersive to play.

Finally, in March support for [decoding the ADPCM data was added](https://github.com/inolen/redream/commit/b95d67b39f6a19e6a57ba06d8290fe710cd87561) as well, [support for the master and per-channel volume registers was added](https://github.com/inolen/redream/commit/02470dc3d3dcacf240e2056f951a1e77f7747675).

While many major features of the AICA chip (e.g. the DSP) are still missing, many games are playable now that these featues have landed.

## High-level BIOS and Flash Rom Emulation

In order to simplify getting up and running with redream, extensive work was done in May to [high-level emulate the Flash rom operations](https://github.com/inolen/redream/commit/acd9e6c4a9cd6ca596a9d52dbbefb05c194e5776) as well as [the syscalls provided by the BIOS](https://github.com/inolen/redream/commit/fae06b247cfe1f59ac92c43a6477db305684e128).

By doing so, users are no longer required to source the BIOS and flash roms from a real system in order to run games.

In addition to the new HLE functionality, new options have been added to override the system's region, language and broadcast settings, eliminating the need to swap out roms if still using an original BIOS and flash rom.

![BIOS settings]({{ site.github.url }}/assets/2017-07-20-progress-report-july-2017/bios.png)

## Depth Buffer Fixes

On the Dreamcast, vertices are submitted for rendering in screen space, with the z component being equal to `1/w`. These values are not normalized to any particular range, which is fine for the PowerVR's 32-bit floating-point depth buffer.

When rendering in OpenGL, these vertices must be converted back to normalized device coordinates. While unprojecting the x and y components is trivial, getting a z value that maintains the same depth ordering is not.

Originally, redream linearly scaled the z-component to the range of `[0.0, 1.0]` with the equation `z = (z-zmin)/(zmax-min)` and passed it off to the depth buffer. This worked ok for many games, but some games had an extremely large z range (e.g. zmin of `0.000001` and zmax of `100000.0`) which caused a serious precision loss when normalizing, especially after the value was quantized to a 24-bit integer to be written to OpenGL's depth buffer.

After [writing a small tool](https://github.com/inolen/redream/blob/master/tools/retrace/depth.c) to measure the accuracy of the results of different normalization methods, the previous linear scaling was replaced with the logarithmic equation `z = log2(1.0 + w) / 17.0`. Using this method, the accuracy of the depth ordering went from as low as 30% to 99% in every problematic scene I could get my hands on.

![Dynamite Cop]({{ site.github.url }}/assets/2017-07-20-progress-report-july-2017/dynamite-cop.png)
*Dynamite Cop before and after*

## Widescreen patching

Piggy backing on the work being done by Esppiral on the [AssemblerGames forums](https://assemblergames.com/threads/dreamcast-widescreen-hacks.58620), support has been added to automatically apply these widescreen patches at runtime.

![Widescreen patches]({{ site.github.url }}/assets/2017-07-20-progress-report-july-2017/widescreen.png)

Only patches for Sonic Adventure and Dynamite Cop have been added for now - contributions would be welcome to finish bringing the known patches in.

<center>
<iframe width="853" height="480" src="https://www.youtube.com/embed/NcSa2B3pCR8" frameborder="0" allowfullscreen></iframe>
</center>

## Gamepad support

Support for SDL2's Gamepad API has landed, meaning most controllers are now plug and play, with no configuration required.

## Performance Improvements

In the past month, redream has gotten a 2-3x performance boost.

The primary contributors:

 * [more aggressively batch polygons](https://github.com/inolen/redream/commit/7e16a14c85cc9ed503ce716eb397c0af49376291) before rendering
 * [idle loop detector](https://github.com/inolen/redream/commit/d73c4cd2ee5cc1c7f6ab99de31bf95142fa4a25a) which speeds up their execution to raise interrupts faster
 * [reworked video rendering](https://github.com/inolen/redream/commit/37bb1137b6a0a845165715d42c4a4358815c618f) to lessen the amount of thread synchronization required

## Notable new games

### Cannon Spike

Cannon Spike would hang during the bootup process, spinning inside the game code waiting for the GD-ROM to be in the paused state. The problem being that, there was no code that would ever put it into the pause state. By more accurately [emulating the drive state when processing SPI commands](https://github.com/inolen/redream/commit/044a9f6a67cf1b03202d8cc242140861ebf0bb46), Cannon Spike now runs great.

<center>
<iframe width="853" height="480" src="https://www.youtube.com/embed/5zVdEiWsEWs" frameborder="0" allowfullscreen></iframe>
</center>

### Conflict Zone

Conflict Zone would boot, but then curiously hang at the controller select screen complaining about an unsupported device, "Dreamcast Controller."

After double-checking dumps of an actual controller communicating over the Maple bus, it was discovered that strings in the messages [should not be null-terminated, but padded with spaces](https://github.com/inolen/redream/commit/9067186c9fa31c7de64bbf28254034883cfbc155).

### Ikaruga

Ikaruga relied on a limited subset of MMU functionality for performing store queue writes.

Thanks to [patches provided by skmp](https://github.com/inolen/redream/commit/3fade7f1fb0cdd5f599ca90773d4202ee6cd4202), support for this limited functionality has been added an Ikaruga now runs.

### Ready 2 Rumble Boxing

Ready 2 Rumble Boxing would end up in an infinite loop waiting for an interrupt during boot.

In order to take a closer look, the [gdb-based debugger was revived](https://github.com/inolen/redream/commit/fe43c2415af73145315b76ddf02060d301fd2acc), and the issue was narrowed down to the SH4's negc instruction [not correctly calculating the carry flag](https://github.com/inolen/redream/commit/fe43c2415af73145315b76ddf02060d301fd2acc).

### Sturmwind

After adding [support for loading CDI images](https://github.com/inolen/redream/commit/78cf487c1913a7fa5ac4e00a59c25e61817da9e7), implementing [support for the SH4 sleep instruction](https://github.com/inolen/redream/commit/dc698f8d67df9ed914ab8f0027cc30001bd2baa0) and [fixing a bug in the rte instruction](https://github.com/inolen/redream/commit/2994a4eb97933e89a50d069db149a6a4ba3686b7), this indie favorite finally booted.

# Where Development Is Going

During the next 2 months, the main priority is to improve the accuracy of the CPU emulation through more extensive unit testing, and automated video-based regression testing to catch newly introduced bugs fast.

Additionally, an Android / AArch64 port is actively being worked on - news should be available for that soon.

As always, if you have any questions or are interested in contributing, drop by the [Slack group](http://slack.redream.io/).

