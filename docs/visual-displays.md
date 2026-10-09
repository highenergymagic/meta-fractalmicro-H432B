# Visual displays

The built-in LCD and external VGA output are not enabled in the standard
Linux runtime. They are separate from the supported
[braille display](braille.md). The interfaces below are established from the
factory drivers; they are not qualified Linux display bindings.

## Built-in LCD

The factory `brllcd.dll` driver sends bitmap data through an external
byte-wide latch. Text rendering occurs before the display transfer; this
is not an HD44780-style character module or a UART peripheral.

| Resource | Factory-driver use |
| --- | --- |
| SROM bank 4, `0xa0000000` | Serial output latch, byte writes |
| SROM controller, `0xe8000000` | Bank width and access timing |
| MP0_1[4], function 2 | Bank-4 chip select |
| GPJ4[0] | Active-high backlight enable |

The latch carries clock (bit 0), command/data (bit 1), serial data (bit 2),
active-low chip select (bit 3) and active-low reset (bit 4). Bytes are shifted
most significant bit first.

The initialization and frame-write commands match the
[Sitronix ST7529 command set](https://support.newhavendisplay.com/hc/en-us/article_attachments/4414855857175),
including its two-byte/three-pixel packing. This identifies a compatible
protocol, not a positively read controller part number. Factory code accepts
a 32-row monochrome bitmap and transfers a padded controller window.
Visible geometry, orientation and grayscale behaviour require validation.

A Linux implementation needs ownership of the SROM bank and latch, a display
driver, backlight control and suspend/resume sequencing. Raw writes must not
compete with a bound driver. Panel voltage and contrast values must be based
on the board initialization, not another ST7529 panel's defaults.

## VGA

The factory display stack uses Samsung FIMD at `0xf8000000`, through the
`display.dll` and `video.dll` drivers. Its initialization contains a
1024-by-768 display size. The board-control driver separately enables
GPE0[5] and requests the display power domain and clock.

This is a raster framebuffer path, not a text-only or USB display interface.
The external analog conversion circuitry, complete timings, connector
detection and Linux display-controller integration remain unqualified.
The pinned kernel's Exynos DRM FIMD driver already includes the
`samsung,s5pv210-fimd` compatible. Board integration still needs the correct
clock, pinctrl, output graph and timing configuration; SoC support alone does
not establish a usable VGA output.

## Factory interface reference

The [manufacturer's U2 manual](https://fccid.io/QJCH432B/User-Manual/Manual-1627098.pdf)
documents independent LCD, backlight, font-size, rotation and external-video
settings. Those settings describe the original firmware, not features
currently implemented by OpenH432.
