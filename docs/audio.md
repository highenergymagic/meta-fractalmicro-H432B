# Speaker audio

## Availability

The normal NAND kernel provides ALSA playback through the S5PV210 I2S controller
and WM8983 codec. The supported PCM format is 44.1 kHz, stereo, S16_LE.
The ALSA card is `OpenH432`; direct playback uses `hw:OpenH432,0`.

The distribution layer owns sound assets, service enablement and mixer policy.
See [system sound services](https://github.com/highenergymagic/meta-fractalmicro-openh432/blob/main/docs/system-sounds.md)
for startup/shutdown cues and output-volume limits. Playback services are not
an audio session manager.

## Clock management

The board machine driver acquires the device-tree `dma-request` clock
(PDMA0) while a playback stream is open. Hardware testing established that
PDMA1 I2S transfers stall when this dependency is gated. Closing the stream
releases the reference; runtime power management remains enabled.

ALSA constraint helpers can return positive success values. Startup treats
only negative values as errors, then enables the clock. Failure paths and
stream close are covered by compiled clock-lifetime tests.

## Qualification and limits

Full PCM playback, an advancing sample pointer and clock release after close
have passed on the NAND runtime. Unsupported-format rejection leaves the clock
reference balanced. Playback started after a power-button deep-sleep cycle
completed successfully and was confirmed audibly at the correct speed.

Boot-time playback has exhibited underruns under system load. Gap-free startup,
a stream held open across suspend, capture, and broad format/routing support
remain unqualified. These checks do not establish Bluetooth or FM audio support.
See the [validation record](https://github.com/highenergymagic/openh432-build/blob/main/docs/hardware-validation.md#audio-request-clock-lifetime)
for exact artifacts and scope.
