---
title:  "Getting started contributing to an emulator"
date:   2016-12-05
---

I've been browsing [r/emulation](https://www.reddit.com/r/emulation) a lot lately, and a common question I see is ["I want to help a project, but I have no idea where to begin"](https://www.reddit.com/r/emulation/comments/5fafea/its_pretty_strange_how_dreamcast_emulation_on_pc/dalaywa/). With the holidays approaching, I wanted to provide an answer for any would-be emulator developer looking to hack on something during them.

It's important to start off by mentioning that it's not a requirement to possess excellent low-level programming skills or in-depth knowledge of the hardware being emulated in order to contribute to emulators. Emulator projects are like any other sufficiently complicated project, there is a ton of work to do for all skill levels.

With that said however, it can be overwhelming to look at an emulation project and find an appropriate place to start contributing. To help bridge that gap, I've been creating more-detailed-than-normal issues in [redream's issue queue](https://github.com/inolen/redream/issues) this past week. These issues are tagged by difficulty level (easy, medium and hard), and prefixed by the section of code they apply to.

## Documentation

* [docs: add initial compatibility chart (easy)](https://github.com/inolen/redream/issues/37)

## UI

* [ui: fullscreen elements don't scale properly as window size changes (easy)](https://github.com/inolen/redream/issues/34)

## Input

* [maple: uniquely name vmu save files (easy)](https://github.com/inolen/redream/issues/31)
* [maple: multiple controller profiles (easy)](https://github.com/inolen/redream/issues/32)
* [maple: add joystick connected / disconnected events (medium)](https://github.com/inolen/redream/issues/33)

## Graphics

* [pvr: add missing palette texture converters (easy)](https://github.com/inolen/redream/issues/30)
* [pvr: support shading instructions (medium)](https://github.com/inolen/redream/issues/35)
* [pvr: support alpha testing for punch-through polygons (medium)](https://github.com/inolen/redream/issues/36)

## JIT

* [jit: expession simplification pass (medium)](https://github.com/inolen/redream/issues/39)

If you would like to chat more about any of these issues you can find me on our [Slack group](http://slack.redream.io/) channel under `@inolen`.
