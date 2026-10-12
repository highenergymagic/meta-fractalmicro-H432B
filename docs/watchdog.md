# Hardware watchdog

The S5PV210 watchdog uses the upstream `s3c2410_wdt` driver and the SoC's
`samsung,s3c6410-wdt` device-tree node. The integrated runtime kernel enables
the driver as built-in; no userspace register access is required.

## Ownership and power management

The `openh432-watchdog` package configures systemd to own the runtime watchdog
with a requested 30-second timeout. A separate watchdog daemon must not open
the same device. The effective timeout and driver identity are available under
`/sys/class/watchdog/watchdog0/`.

The upstream driver stops the counter during suspend and restores its state
on resume. Its shutdown callback stops the counter before final board power
handling. The userspace policy disables reboot/kexec watchdog ownership:
this is runtime recovery, not a guarantee against stalls during final shutdown.

## Qualification

A controlled no-keepalive test reset the board and returned to Linux. Runtime
feeding at 30 seconds and suspend/wake beyond the timeout were verified with
the same boot ID after resume. The standard NAND image has booted with systemd
automatically owning the active 30-second watchdog and both accessible-console
services running. It stayed in wakeable shutdown beyond the watchdog timeout,
then started normally from the power switch and automatically re-armed the
watchdog. The timeout and suspend tests used the integrated tracing-enabled
kernel; the automatic-start and shutdown/wake tests used the standard image.

These tests do not establish recovery from the intermittent whole-system
freeze under investigation. The counter depends on its clock and reset path;
some bus or power failures may prevent it from recovering the machine.
The driver reported zero `bootstatus` after the timeout test, so that attribute
must not be treated as proof that the previous reset was not watchdog-related.

The software soft-lockup detector is independent of this hardware watchdog.
A watchdog reset does not diagnose the underlying fault or ensure a crash
log survives. Forced timeout testing must be performed only after syncing
storage and with a known bootable fallback available.
