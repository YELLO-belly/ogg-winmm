# ogg-winmm CD Audio Emulator (2020 revision by DD)
Based on the original "hifi" release of ogg-winmm with the following changes:

- Win8.1/10 support: stubs.c - taken from "bangstk" fork.
- fix to Make STOP command instant - from "bangstk" fork.
- WinQuake support - from Dxwnd source.
- int "numTracks = 1" - to fix issues with last track playback.
- Added ogg music volume control by "winmm.ini" (use a value between 0-100).
- Fix to repeat track instead of advancing to the next track.
- Commented out problematic MCI_CLOSE code.
- Sysinfo return value now "cdaudio" instead of "cd".
- Removed an unnecessary duplicate free buffer command from player.
- Logs now saved to winmm.log instead of winmm.txt.
- MCI_NOTIFY message handling. (fixes Civ2 - Test of time tracks not changing)
- Added make.cmd and renamed source files to "ogg-" instead of "wav-".
- Added rudimentary MCI_PAUSE support.
- Ignore Track00.ogg.
- Accounted for the possibility of pure music cd's.
- MCI send string implementation of aliases.

TODO:
- Try to closer match the excellent cdaudio emulation of DxWnd and it's stand alone [CDAudio proxy.](https://sourceforge.net/projects/cdaudio-proxy/)
