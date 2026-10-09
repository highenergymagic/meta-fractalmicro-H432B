# Audio

## Availability

The normal NAND kernel provides an ALSA card named `OpenH432` through the
S5PV210 I2S controller and WM8983 codec. Playback and capture use
`hw:OpenH432,0`, with 44.1 kHz, stereo, S16_LE PCM. Concurrent streams share
the sample rate and sample width.

Speaker playback and internal microphone capture are hardware-qualified.
Jack reporting and speaker gating have been exercised using controlled GPIO
state changes, not physical plug insertion. Exact test scope is recorded in the
[validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md).
External microphone audio routing is not yet implemented.

## Playback and headphone handling

The `Internal Speaker Switch` enables the board playback path.
A detected headphone plug inhibits the external speaker amplifier while
leaving the headphone playback path available. Both codec output pairs remain
enabled by the board route because their physical wiring has not been isolated.

The `Headphone Playback Volume` and `Speaker Playback Volume` controls are
limited to 50/63 (approximately 80% of the numerical range). This is not a calibrated
sound-pressure or speaker-power limit.

The distribution layer owns sound assets, service enablement and mixer policy.
See [system sound services](https://github.com/highenergymagic/meta-fractalmicro-openh432/blob/main/docs/system-sounds.md).
Playback services are not an audio session manager.

## Microphone capture

`Internal Mic Switch` controls the internal microphone endpoint. DAPM enables
microphone bias only while the selected capture path requires it. The board
uses the factory bias-voltage selection and the codec's differential microphone
inputs. Capture does not enable speaker monitoring or digital loopback.

Applications must configure the ALSA capture mixer as well as opening the PCM
device. The relevant controls are `Capture Volume`, `Capture PGA Volume`,
`Capture PGA Boost Volume`, the left/right input mixers and `ALC Capture Function`.
The auxiliary inputs are distinct from the internal microphone; exposing codec
mixer controls does not establish support for the external microphone jack.

## Jack detection

ALSA reports separate `Headphone Jack` and `Microphone Jack` controls and input
switch events. Both GPIOs are active-low and debounced for 50 ms. The pinctrl
state disables inherited pull-downs, which otherwise falsely indicate inserted
plugs. Jack insertion and removal are not wake sources; their state is refreshed
after resume.
Microphone jack reporting does not yet select an external capture route.

## Clock and power management

The board driver holds the device-tree `dma-request` clock (PDMA0) for each
open PCM stream. Hardware testing established that PDMA1 I2S playback stalls
when this dependency is gated. Closing a stream releases its reference;
runtime power management remains enabled.

ALSA constraint helpers can return positive success values. Startup treats
only negative values as errors before acquiring the clock. Constraint failure,
clock failure and stream-close paths have compiled regression tests.

## Qualification limits

Speaker playback, sample timing, request-clock release and playback after
a power-button deep-sleep cycle have passed on the NAND runtime. The RAM-resident
startup sound has been confirmed audibly clean on normal NAND boots.

Capture has a start-of-stream settling transient lasting roughly the first
second. Applications requiring clean initial samples must accommodate this;
continuous recording and concurrent playback/capture do not imply calibrated
microphone gain or stereo separation.

Headphone listening, physical jack transitions, external microphone capture,
streams held open across suspend and formats beyond the fixed PCM format remain
unqualified. These checks do not establish Bluetooth or FM audio support.
