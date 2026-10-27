# Android report review, 27 October 2026

Reviewed the 18:03 and 18:09 gameplay reports (build 126), the 18:02 bridge
recording, and the 18:15 Mental Omega report (build 127), from ra2yrlogs.

The Mental Omega report contains `mod-inspection.txt`: 1,959 files, seven
archives, and blocking findings for Windows extensions, missing profile
metadata, renamed configuration files and unsupported extension keys. The
import/inspection path worked; Mental Omega gameplay is not supported yet.
That engine log ends normally in the lobby, rather than reporting a new crash.
The included inspection describes the last inspected folder, not necessarily
the active game profile.

The earlier gameplay report records a Genetic Converter placement after a
save load, followed by a still-ready charge. Restore missing BRUTE payload
registration after loading a saved registry, refresh Yuri power effect settings
without discarding charge, and log whether each placement was accepted. This
addresses a plausible saved-state cause; phone reproduction remains required.

Settings already stopped local simulation updates, but the independent audio
callback continued consuming voices and music. Pausing the mixer now returns
silence without advancing either source, and leaving the game releases pause.
Network matches retain their existing shared simulation behavior.

Bullets and launcher rockets now retain a launch terrain origin. Targets,
ground collision, rendering, trails and impact height use that same reference,
instead of adding whichever cell height happens to be below the flight.
World save format 61 appends flight origins while retaining the older inline
layouts and reading versions 55 through 60. Old in-flight saves start from
their saved terrain reference; their original launch origin cannot be recovered.

Rocket launch wait uses the actual V3, Dreadnought or Boomer block. Launcher
smoke also covers the pause phase, and display history interpolates rocket
pitch as well as position. Native regression tests and an Android ARM64 library
build check these changes; they do not replace an on-device visual check.

The short video shows bridge traffic bunching, not missile flight. The earlier
structural bridge-cell correction is present in those builds. Ramp connections,
destroyed spans and remaining bridge traffic/presentation issues need further
reproduction; this change does not claim to resolve them.

Verification: 1,355 native cases / 280,592 assertions passed, excluding the
unavailable landmark fixture. After the final defense-launch adjustment,
144 projectile/defense/building/save cases / 6,660 assertions passed.
Android ARM64 `libmain.so` compiled and linked. No APK was packaged or tested
on the phone in this run.
