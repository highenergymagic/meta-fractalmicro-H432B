# Source and licensing record

The initial import consists of project-developed Linux/U-Boot patch series,
kernel config fragments and DTS from the OpenH432 hardware bring-up. Imported
patches retain upstream context and notices. New metadata is MIT; Linux
changes remain GPL-2.0-only unless their file says otherwise, and U-Boot
changes remain under its GPL terms. The Braille helper explicitly states
GPL-2.0-or-later. The MIT root license does not override component licenses.

U-Boot's compiler-gcc14.h compatibility file is derived from its existing
compiler-gcc4.h, not newly authored compiler code. The Braille ASCII mapping
is the standardized display encoding checked against liblouis en-us-brf.dis;
it is not a translation engine. Boot greeting text is project configuration, separate from the encoding table.

Hardware register addresses, GPIO roles, transport ordering and image-format
facts were established through device observation, public upstream code and
analysis of locally supplied firmware. The public input set contains no
vendor binary, extracted code listing, decompilation, proprietary header
bundle, or Wi-Fi firmware. Comments retain evidence references so the origin
of hardware facts is not obscured. This source review is not a legal opinion
or a claim of an independently staffed clean-room process.

The development USB identities are inherited bring-up values, not an asserted
VID/PID allocation for a shipping Fractal Microsystems product. USB access is
privileged debug access, not authenticated access. The port includes experimental interfaces; publication does not certify
production security or support for other boards.
