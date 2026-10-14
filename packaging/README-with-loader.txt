SOMA.FPSFix
===========

High-frame-rate fix for SOMA (64-bit Windows).

SOMA simulates the game 60 times a second and normally draws at the same
rate. This fix lets the game draw at your display's rate and smooths what you
see in between simulation steps, without speeding up physics, scripts, AI,
animation, or audio.

What it smooths
---------------
- Camera movement, including scripted and seated camera sequences
- Mouse look, read at the display rate instead of 60 Hz
- First-person hands and held tools
- Props, doors, drawers, debris, readables, and animated props
- Character and creature animation
- Physics-driven skeletons such as hanging cables
- Menu, pause-menu, and terminal mouse cursors

Requirements
------------
- SOMA for Windows. Developed and tested on the GOG build only. At startup
  the fix checks the game code it depends on and switches itself off if
  anything does not match, so an unsupported build runs as if it were not
  installed.
- VSync or an external frame limiter. The fix does not cap the frame rate.
- For display-rate mouse look, turn Smooth Mouse off in the game's input
  options. With it on, mouse look stays smoothed at 60 Hz.

Installation
------------
Place all files beside Soma.exe. The included version.dll is the x64 Ultimate
ASI Loader v9.7.4, which loads SOMA.FPSFix.asi when the game starts.

Settings
--------
SOMA.FPSFix.ini lists every feature with a short description. Set
Enabled = false under [General] to switch the fix off without removing it.

Not covered
-----------
- Particles and screen effects still update at 60 Hz.
- Mods and custom stories are untested.

Troubleshooting
---------------
SOMA.FPSFix.log is written beside the plugin and replaced on each launch; the
previous session is kept as SOMA.FPSFix.previous.log. If the fix switches
itself off, the reason is at the end of the log.

Removal
-------
Remove SOMA.FPSFix.asi, SOMA.FPSFix.ini, SOMA.FPSFix.log,
SOMA.FPSFix.previous.log, and version.dll. Do not remove version.dll if
another installed plugin depends on it.

Misc
-------
For more game fixes, visit https://slop-blog.enkhayzomachines.net/fixes :)
