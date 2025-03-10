# What is it?

ogg-winmm is a CD-audio emulator implemented using a winmm.dll wrapper file. It is useful for old games that used cdaudio tracks for music playback. The wrapper allows music to be played from the hard drive instead of the CD. It is also useful in fixing various issues that the mcicda driver has starting with Windows Vista. This includes broken track repeats and various other smaller issues.  

<sub>There is also the [cdaudio-winmm](https://github.com/YELLO-belly/cdaudio-winmm) project which concentrates on only fixing the various mcicda issues while still maintaining playback from the CD.<sub>

# Usage:

Place the *winmm.dll* in the main game folder. (Do not put it into a system folder it is just a wrapper!)

Place the .ogg music files in a "Music" sub-folder with the following naming convention:
*Track02.ogg, Track03.ogg ...*
Note that numbering usually starts at 02 since the first track is a data track on mixed mode CD's.
However some games may use a pure music CD with no data tracks in which case you should start numbering from Track01.ogg ...

Winmm.ini options:
- Music volume can be adjusted by changing the value between 0 - 100. Useful when the games internal music slider does not function properly. **NOTE:** When set to 100 the in-game music sliders can be used to adjust the volume (does not work with all games).
- **MCIDevID = 0** Set this to 1 to enable more accurate MCI device enumeration. Some games will not repeat music tracks without setting this.
- **ACCSeekOFF = 0** Set this to 1 to disable accurate seeking of music tracks. This will disable the new track seeking code and use the older less accurate method of simply playing single tracks instead of being able to seek to a specific position.
- **FullNotify = 0** Set this to 1 to try and simulate MCI notify messages more accurately. Some games might need this option to play cdaudio.
- **Log = 0** Set this to 1 to write winmm.log files in the game folder. Log files may be helpful in troubleshooting.
  
# How to rip music from a CD and convert it to the .ogg file format:

There is a program called [fre:ac](https://github.com/enzo1982/freac) which should be able to both rip and convert the music into .ogg files.  

You can also rip the music from your game CD using Windows Media Player as .wav files ```format WAV (lossless)``` and then convert them to .ogg using oggenc2 from:
https://rarewares.org/ogg-oggenc.php
(Note: The new Media Player on Windows 11 seems to no longer support ripping to .wav files...)

The cmd prompt command:
```for %%a in (*.wav) do oggenc2 "%%a"```
converts all .wav files into .ogg format. You can put this command into a .bat or .cmd file in the same folder with oggenc2 and the wav files. Then run the batch file and it should encode all the wave files into ogg format.

**Alternatively you can use the Audacity program with batch conversion for wav to ogg converison:**  
File->Edit Chains...  

From the left side choose  
Add -> and give the name "OGG Conversion". click OK.  

With OGG Conversion selected from the right click on "Insert".  
Double-click on "ExportOGG" and click OK.  

Now that you have an OGG conversion chain in Audacity go to.  
File->Apply Chain... and select "OGG Conversion".  
Then click on "Apply to Files..." navigating and choosing all the files you want to convert to ogg format.  
Audacity should then convert all the files that were selected into the ogg format.  


# Extra note:
- Apparently on some machines the local winmm.dll wrapper is ignored and the real system dll is used instead. This may be because some other program has already loaded the winmm.dll library or some system setting forces the use of the real dll. (For example if the program has an ACT Database entry.) Some possible solutions:

1. Easiest trick is to rename the game executable in order to disable any Microsoft ACT Fixes (or shims) that might prevent loading the warapper dll.

2. The wrapper can also be forced to load by renaming it to for example to winm2.dll and hex editing the program executable to point to this renamed winmm.dll instead. There is now also a PowerShell script available in the sources to help alleviate issues where the wrapper is ignored by Windows.
See: https://github.com/YELLO-belly/ogg-winmm/tree/master/PS-Script  

# ogg-winmm CD Audio Emulator (2023 revision)

v.2.2.0.7
- Fixed a regression that could cause an error when loading the winmm wrapper on some systems. (DllMain function)
- Some code cleanup.

v.2.2.0.6
- mciSendString open device fixes. (trim "wait" to avoid using it as the alias and remove the devid from return.)
- Change CD identity to be numeric instead of alphanumeric.
- CreateThread using a fixed stack size.
- Do not attempt to play from track 0.

v.2.2.0.5
- Fix case sensitive mciSendCommand DeviceType. Some games can use CDAudio instead of cdaudio, etc... These would previously fail because the logic did not take this into account.

v.2.2.0.4 (second attempt)
- Specifying MCIDevID = 1 in winmm.ini now opens a waveaudio device to lock a real MCI device ID for use with the cdaudio emulator (similar to what Dxwnd does). This should please all games but the old imaginary 0xBEEF ID is still left as the default behaviour just in case opening of the waveaudio device fails or MCIDevID is not set.


v.2.2.0.3
- New winmm.ini option "FullNotify" to enable the handling of notify success messages for all MCI commands. This is disabled by default because most games do not need it and also because our emulation is always sending the message to the topmost window (HWND)0xffff which might not be ideal in some situations.
- MCI_GETDEVCAPS improvements
- Some fixes to MciSendString "open" and alias handling
- MCI_SYSINFO fixes
- MCI_CLOSE now resets time format to MSF
- Handle MCI_SET_AUDIO
- Handle MCI_PLAY NULL & MCI_TO while playing
- MCI_NOTIFY_ABORTED for MCI_SEEK
- Fix MciSendString open command returned device id

The cdaudio emulation should be fairly complete now. Though there is always the possibility that some game is looking for a specific return value that has been missed. In addition if a program uses invalid commands the emulator does not enforce the MCI ERROR code rules. However in terms of MCI commands and Notify messages this version should be fairly close to the real winmm behaviour following the working win9x/winXP implementation as a template.


v.2.2.0.2
- Improved play logic with proper resume from a MCI_STOP/MCI_PAUSE as per win9x behaviour.  
  <sub>Tools used for win9x testing: https://github.com/YELLO-belly/mciSendCmd-CDDA-tester</sub>
- Interrupt play with MCI_STOP & MCI_PAUSE now causes MCI_NOTIFY_ABORTED to be sent if notify flag has been set as per win9x behaviour.
- Various other small fixes and tweaks for Length and position calculations. 
- Improved MciSendString handling.
- Improved millisecond and MSF format handling.
- Implemented MCI_SEEK.
- Implemented ability to play tracks from and to arbitrary positions. (winmm.ini ACCSeekOFF = 1 to disable)
- Use alternate MCI device ID(1) for some games that need it. (winmm.ini MCIDevID = 1 to enable)

Known limitations:
- Some games rely on accurate track length as a form of copy protection. A real CDAudio track can be read in milliseconds or minutes/seconds/frames (1sec = 75 frames). The currently implemented ogg music player logic can only handle lengths with 1 second accuracy. In short this means that if a game has track length based copy protection it will likely fail to run with the ogg-winmm wrapper.


# ogg-winmm CD Audio Emulator (2020 revision by DD)

v.0.2.0.2 rev3:
- Implemented mciSendCommand MCI_SYSINFO to support Interstate76, HeavyGear and Battlezone2.
- MCI_STATUS_MODE now takes into account the paused state.
- Improved MCI_STATUS_POSITION handling.
- Added Notify message handling to mciSendString.
- Improvements to milliseconds handling. (Battlezone2)
- MCI_STATUS_LENGTH improvements.
- Implemented MCI_TO logic when no MCI_FROM is given.
- Removed the Sleep logic from pause command. (issues with crackling sound when resuming)
- Re-enabled track advance. Fixed track playback and last track logic in play loop.
- Removed forced track repeat. (Notify message should handle track repeat.)

v.0.1.0.1 rev2:
- Fixed an error in the logic which meant in-game music volume sliders were disabled. (winmm.ini now works as a hard override with values 0-99 and 100 means in-game volume adjustment is used)

Based on the original "hifi" release of ogg-winmm with the following changes:

- Win8.1/10 support: stubs.c - taken from "bangstk" fork.
- fix to Make STOP command instant.
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

# TODO:
- ~~Try to closer match the excellent cdaudio emulation of DxWnd and it's stand alone [CDAudio proxy.](https://sourceforge.net/projects/cdaudio-proxy/)~~ (achieved?)
- Add support for FLAC and possibly other audio formats in player.c (https://github.com/mackron/dr_libs)

# Building:

- Use MinGW 6.3.0-1 or later.
- Dependencies: libogg, libvorbis
