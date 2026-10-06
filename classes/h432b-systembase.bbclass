# SPDX-License-Identifier: MIT
# Apply to any recipe emitting an H432B system-base SquashFS image.
python h432b_check_systembase_size() {
    from pathlib import Path
    limit = min(200 * 1024 * 1024, int(d.getVar("H432B_SYSTEMBASE_MAX_BYTES")))
    capacity = int(d.getVar("H432B_SYSTEMBASE_LEBS")) * int(d.getVar("H432B_UBI_LEB_BYTES"))
    if capacity > limit:
        bb.fatal("H432B base volume exceeds the 200 MiB hard limit")
    images = [p for p in Path(d.getVar("IMGDEPLOYDIR")).glob("*.squashfs*") if p.is_file()]
    if not images:
        bb.fatal("No H432B system-base SquashFS artifact found")
    for path in images:
        if path.stat().st_size > capacity:
            bb.fatal("System base exceeds its bounded UBI volume (%d bytes): %s" % (capacity, path))
}
IMAGE_POSTPROCESS_COMMAND += "h432b_check_systembase_size; "
