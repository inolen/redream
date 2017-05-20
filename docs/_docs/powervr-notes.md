---
title: PowerVR Notes
---

The Dreamcast's rendering is powered by a PowerVR chip with a hardware 3D engine called the Tile Accelerator.

Games directly submit primitive data, texture data and rendering parameters to this Tile Accelerator, it in turn [renders them](http://mc.pp.se/dc/pvr.html), writes the rasterized output to video ram and finally raises an external interrupt signalling when the rendering is complete. Many details are left out here because, for the most part, how the Tile Accelerator works internally is unimportant to our cause.

For our purpose, this input data needs to be translated into equivalent OpenGL commands and rendered, and the same interrupt must be generated when once this process is done - the internals of the tile-based rendering can be ignored.

# Receiving the data

Data is submitted to the TA through a DMA transfer to its internal FIFO buffer. On the hardware, this data is not immediately rendered, but instead generates a display list in memory which is rendered at later time through the [STARTRENDER](https://github.com/inolen/redream/blob/master/src/hw/pvr/ta.c#L995) register.

As mentioned above, it's safe to ignore generating the actual display list used internally by the TA, however the data still must be written out somewhere and rendered in a deferred fashion when `STARTRENDER` is written to. This data is received in [ta_poly_fifo_write](https://github.com/inolen/redream/blob/master/src/hw/pvr/ta.c#L787) and in turn written to an internal [tile_context](https://github.com/inolen/redream/blob/master/src/hw/pvr/ta_types.h#L440) structure.

This `tile_context` structure not only store the raw data stream, but all state necessary to render the data. When a display list is told to start rendering, various registers are read that can control the final output. By capturing this state as well, the data can be rendered completely offline in the [tracer tool](running).

# Converting the data

As mentioned above, all of the raw data relative to a given display list is written to our own `tile_context` structure. This structure is meant to only hold the raw data stream and register state needed to render the display list, it's not processed in any way.

Before this raw context can be rendered, it must be converted into a [tile_render_context](https://github.com/inolen/redream/blob/master/src/hw/pvr/tr.h#L62) structure by [tr_parse_context](https://github.com/inolen/redream/blob/master/src/hw/pvr/tr.c#L953). The purpose of this separation is so that raw `tile_context` structures can be dumped while running a game and viewed offline in the [tracer tool](running). This makes it fast-er to iterate on bugs where a context is being converted or rendered incorrectly in some way.

For the most part, converting the data is straight forward. There are numerous polygon and vertex types, each which have different attributes (blend mode, xyz, color, uv, etc.) specified in different formats (packed, unpacked, quantized, etc.). Each polygon is converted into a [surface instance](https://github.com/inolen/redream/blob/master/src/render/render_backend.h#L89) and each vertex is converted into a [vertex instance](https://github.com/inolen/redream/blob/master/src/render/render_backend.h#L81) which is used by the vertex buffers in the rendering backend. For more information on this, please see [tr_parse_poly_param](https://github.com/inolen/redream/blob/master/src/hw/pvr/tr.c#L517) and [tr_parse_vert_param](https://github.com/inolen/redream/blob/master/src/hw/pvr/tr.c#L651).

In addition to converting the actual data, some additional operations need to be performed like [sorting translucent polygons](https://github.com/inolen/redream/blob/master/src/hw/pvr/tr.c#L1013). This process currently isn't emulated perfectly, as the real hardware supported per-pixel sorting, where as they're currently only being sorted per-polygon.

# Rendering the data

The TA supported various per-pixel effects, all of which are implemented through a single [mega-shader](https://github.com/inolen/redream/blob/master/src/render/ta.glsl).

The real problem of note when rendering the data, is that the XYZ / UV coordinates are not in a state friendly to the OpenGL pipeline. The X and Y cooordinates are in screen space, with the Z component being 1/W. The UV coordinates however have not had perspective division applied yet.

