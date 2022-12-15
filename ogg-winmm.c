/*
 * Copyright (c) 2012 Toni Spets <toni.spets@iki.fi>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Code revised by DD (2020) (v.0.2.0.2) */
// 2022 revisions by Y-B...

#include <windows.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include "player.h"

/* MCI Relay declarations: */
MCIERROR WINAPI relay_mciSendCommandA(MCIDEVICEID a0, UINT a1, DWORD a2, DWORD a3);
MCIERROR WINAPI relay_mciSendStringA(LPCSTR a0, LPSTR a1, UINT a2, HWND a3);

int MAGIC_DEVICEID = 48879; /* 48879 = 0xBEEF */
#define MAX_TRACKS 99

MCI_OPEN_PARMS mciOpenParms;

struct track_info
{
    char path[MAX_PATH];    /* full path to ogg */
    unsigned int length;    /* seconds */
    unsigned int position;  /* seconds */
};

static struct track_info tracks[MAX_TRACKS];

struct play_info
{
    int first;
    int last;
};

#ifdef _DEBUG
    #define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
    FILE *fh = NULL;
#else
    #define dprintf(...)
#endif

int FullNotify = 0;
int opened = 0;
int sendStringNotify = 0;
int ACCSeekOFF = 0;
int seek = 0;
int plrpos = 0;
int plrpos2 = -1;
int current  = 1;
int paused = 0;
int notify = 0;
int playing = 0;
HANDLE player = NULL;
int firstTrack = -1;
int lastTrack = 0;
int numTracks = 1; /* +1 for data track on mixed mode cd's */
char music_path[2048];
int time_format = MCI_FORMAT_MSF;
CRITICAL_SECTION cs;
char alias_s[100] = "cdaudio";
static struct play_info info = { -1, -1 };

int player_main(struct play_info *info)
{
    int first = info->first;
    int last = info->last -1; /* -1 for plr logic */
    if(last<first)last = first; /* manage plr logic */
    current = first;
    if(current<firstTrack)current = firstTrack;
    dprintf("OGG Player logic: %d to %d\r\n", first, last);

    while (current <= last && playing)
    {
        dprintf("Current track: %s\r\n", tracks[current].path);
        plr_play(tracks[current].path);

        while (1)
        {
            if(paused || seek){
                plr_seek(plrpos);
                paused = 0;
                seek = 0;
                plrpos = 0;
            }

            if(plr_tell() >= plrpos2 && plrpos2!=-1 && current == last){
                plrpos = plr_tell();
                plrpos2 = -1;
                paused = 1;
                playing = 0;
                if(notify){
                    notify = 0;
                    dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                    SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                }
                return 0;
            }

            if (plr_pump() == 0)
                break;

            if (!playing)
            {
                return 0;
            }
        }
        current++;
    }

    playing = 0;

    /* Sending notify successful message:*/
    if(notify && !paused)
    {
        notify = 0;
        dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
        SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
        /* NOTE: Notify message after successful playback is not working in Vista+.
        MCI_STATUS_MODE does not update to show that the track is no longer playing.
        Bug or broken design in mcicda.dll (also noted by the Wine team) */
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
#ifdef _DEBUG
        int bLog = GetPrivateProfileInt("winmm", "Log", 0, ".\\winmm.ini");
        if(bLog)fh = fopen("winmm.log", "w"); /* Renamed to .log*/
#endif
        GetModuleFileName(hinstDLL, music_path, sizeof music_path);

        memset(tracks, 0, sizeof tracks);

        InitializeCriticalSection(&cs);

        int bMCIDevID = GetPrivateProfileInt("winmm", "MCIDevID", 0, ".\\winmm.ini");
        if(bMCIDevID){
            mciOpenParms.lpstrDeviceType = "waveaudio";
            int MCIERRret = 0;
            if (MCIERRret = mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE, (DWORD)(LPVOID) &mciOpenParms)){
                // Failed to open wave device.
                MAGIC_DEVICEID = 48879;
                dprintf("Failed to open wave device! Using 0xBEEF as cdaudio id.\r\n");
            }
            else{
                MAGIC_DEVICEID = mciOpenParms.wDeviceID;
                dprintf("Wave device opened succesfully using cdaudio ID %d for emulation.\r\n",MAGIC_DEVICEID);
            }
        }

        int bACCSeekOFF = GetPrivateProfileInt("winmm", "ACCSeekOFF", 0, ".\\winmm.ini");
        if(bACCSeekOFF) ACCSeekOFF = 1;
        int bFullNotify = GetPrivateProfileInt("winmm", "FullNotify", 0, ".\\winmm.ini");
        if(bFullNotify) FullNotify = 1;

        char *last = strrchr(music_path, '\\');
        if (last)
        {
            *last = '\0';
        }
        strncat(music_path, "\\MUSIC", sizeof music_path - 1);

        dprintf("ogg-winmm music directory is %s\r\n", music_path);
        dprintf("ogg-winmm searching tracks...\r\n");

        unsigned int position = 0;

        for (int i = 1; i < MAX_TRACKS; i++) /* "Changed: int i = 0" to "1" we can skip track00.ogg" */
        {
            snprintf(tracks[i].path, sizeof tracks[i].path, "%s\\Track%02d.ogg", music_path, i);
            tracks[i].length = plr_length(tracks[i].path);
            tracks[i].position = position + 2; //2 second pre-gap

            if (tracks[i].length < 4)
            {
                tracks[i].path[0] = '\0';
                //position += 4; /* missing tracks are 4 second data tracks for us */
            }
            else
            {
                if (firstTrack == -1)
                {
                    firstTrack = i;
                }
                if(i == numTracks) numTracks -= 1; /* Take into account pure music cd's starting with track01.ogg */

                dprintf("Track %02d: %02d:%02d @ %d seconds\r\n", i, tracks[i].length / 60, tracks[i].length % 60, tracks[i].position);
                numTracks++;
                lastTrack = i;
                position += tracks[i].length;
            }
        }

        dprintf("Emulating total of %d CD tracks.\r\n\r\n", numTracks);
    }

#ifdef _DEBUG
    if (fdwReason == DLL_PROCESS_DETACH)
    {
        if (fh)
        {
            fclose(fh);
            fh = NULL;
        }
    }
#endif

    return TRUE;
}

/* MCI commands */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-commands */
MCIERROR WINAPI fake_mciSendCommandA(MCIDEVICEID IDDevice, UINT uMsg, DWORD_PTR fdwCommand, DWORD_PTR dwParam)
{
    char cmdbuf[1024];

    dprintf("mciSendCommandA(IDDevice=%p, uMsg=%p, fdwCommand=%p, dwParam=%p)\r\n", IDDevice, uMsg, fdwCommand, dwParam);

    if (uMsg == MCI_OPEN)
    {
        LPMCI_OPEN_PARMS parms = (LPVOID)dwParam;

        dprintf("  MCI_OPEN\r\n");

        if (fdwCommand & MCI_OPEN_ALIAS)
        {
            dprintf("    MCI_OPEN_ALIAS\r\n");
            dprintf("        -> %s\r\n", parms->lpstrAlias);
        }

        if (fdwCommand & MCI_OPEN_SHAREABLE)
        {
            dprintf("    MCI_OPEN_SHAREABLE\r\n");
        }

        if (fdwCommand & MCI_OPEN_TYPE_ID)
        {
            dprintf("    MCI_OPEN_TYPE_ID\r\n");

            if (LOWORD(parms->lpstrDeviceType) == MCI_DEVTYPE_CD_AUDIO)
            {
                dprintf("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\r\n");
                parms->wDeviceID = MAGIC_DEVICEID;
                if (fdwCommand & MCI_NOTIFY)
                {
                    if (FullNotify && !opened){
                        dprintf("  MCI_NOTIFY\r\n");
                        dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                        SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                        Sleep(50);
                    }
                }
                opened = 1;
                return 0;
            }
            else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam); /* Added MCI relay */
        }

        if (fdwCommand & MCI_OPEN_TYPE && !(fdwCommand & MCI_OPEN_TYPE_ID))
        {
            dprintf("    MCI_OPEN_TYPE\r\n");
            dprintf("        -> %s\r\n", parms->lpstrDeviceType);

            if (strcmp(parms->lpstrDeviceType, "cdaudio") == 0)
            {
                dprintf("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\r\n");
                parms->wDeviceID = MAGIC_DEVICEID;
                if (fdwCommand & MCI_NOTIFY)
                {
                    if (FullNotify && !opened){
                        dprintf("  MCI_NOTIFY\r\n");
                        dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                        SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                        Sleep(50);
                    }
                }
                opened = 1;
                return 0;
            }
            else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam); /* Added MCI relay */
        }

    }

    if (IDDevice == MAGIC_DEVICEID || IDDevice == 0 || IDDevice == 0xFFFFFFFF)
    {
        if (fdwCommand & MCI_WAIT)
        {
            dprintf("  MCI_WAIT\r\n");
        }
        
        if (uMsg == MCI_GETDEVCAPS)
        {
            LPMCI_GETDEVCAPS_PARMS parms = (LPVOID)dwParam;

            dprintf("  MCI_GETDEVCAPS\r\n");

            parms->dwReturn = 0;

            if (fdwCommand & MCI_GETDEVCAPS_ITEM)
            {
                dprintf("  MCI_GETDEVCAPS_ITEM\r\n");
                
                if (parms->dwItem == MCI_GETDEVCAPS_CAN_PLAY || parms->dwItem == MCI_GETDEVCAPS_CAN_EJECT || parms->dwItem == MCI_GETDEVCAPS_HAS_AUDIO)
                {
                    parms->dwReturn = TRUE;
                }
                else if (parms->dwItem == MCI_GETDEVCAPS_DEVICE_TYPE)
                {
                    parms->dwReturn = MCI_DEVTYPE_CD_AUDIO;
                }
                else
                {
                    parms->dwReturn = FALSE;
                }
            }
            if (fdwCommand & MCI_NOTIFY)
            {
                if (FullNotify && opened){
                    dprintf("  MCI_NOTIFY\r\n");
                    notify = 0;
                    dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                    // Note that MCI_NOTIFY_SUPERSEDED would be sent before MCI_NOTIFY_SUCCESSFUL if track was playing, but this is not emulated.
                    SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                    Sleep(50);
                }
            }
        }

        if (uMsg == MCI_SET)
        {
            LPMCI_SET_PARMS parms = (LPVOID)dwParam;

            dprintf("  MCI_SET\r\n");

            if (fdwCommand & MCI_SET_TIME_FORMAT)
            {
                dprintf("    MCI_SET_TIME_FORMAT\r\n");

                time_format = parms->dwTimeFormat;

                if (parms->dwTimeFormat == MCI_FORMAT_BYTES)
                {
                    dprintf("      MCI_FORMAT_BYTES\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_FRAMES)
                {
                    dprintf("      MCI_FORMAT_FRAMES\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_HMS)
                {
                    dprintf("      MCI_FORMAT_HMS\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_MILLISECONDS)
                {
                    dprintf("      MCI_FORMAT_MILLISECONDS\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_MSF)
                {
                    dprintf("      MCI_FORMAT_MSF\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_SAMPLES)
                {
                    dprintf("      MCI_FORMAT_SAMPLES\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_TMSF)
                {
                    dprintf("      MCI_FORMAT_TMSF\r\n");
                }
            }
            if (fdwCommand & MCI_SET_AUDIO)
            {
                if (parms->dwAudio == MCI_SET_AUDIO_ALL)
                {
                    dprintf("      MCI_SET_AUDIO_ALL\r\n");
                    if (fdwCommand & MCI_SET_ON)  plr_volume(100);
                    if (fdwCommand & MCI_SET_OFF) plr_volume(0);
                }
                if (parms->dwAudio == MCI_SET_AUDIO_LEFT)
                {
                    dprintf("      MCI_SET_AUDIO_LEFT\r\n");
                }
                if (parms->dwAudio == MCI_SET_AUDIO_RIGHT)
                {
                    dprintf("      MCI_SET_AUDIO_RIGHT\r\n");
                }
            }
            if (fdwCommand & MCI_NOTIFY)
            {
                if (FullNotify && opened){
                    dprintf("  MCI_NOTIFY\r\n");
                    notify = 0;
                    dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                    // Note that MCI_NOTIFY_SUPERSEDED would be sent before MCI_NOTIFY_SUCCESSFUL if track was playing, but this is not emulated.
                    SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                    Sleep(50);
                }
            }
        }

        // MCI_SEEK implementation. Note that seeking stops playback. MCI_PLAY NULL or MCI_PLAY+MCI_TO starts from seeked position...
        if (uMsg == MCI_SEEK)
        {
        
            if(notify){
                notify = 0;
                dprintf("  Sending MCI_NOTIFY_ABORTED message...\r\n");
                SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_ABORTED, MAGIC_DEVICEID);
            }
        
            LPMCI_SEEK_PARMS parms = (LPVOID)dwParam;
            
            dprintf("  MCI_SEEK\r\n");

            if (fdwCommand & MCI_SEEK_TO_START)
            {
                dprintf("    Seek to firstTrack %d\r\n",firstTrack);
                current = info.first = firstTrack;
                info.last = lastTrack;
                plr_stop();
                playing = 0;
                plrpos = 0;
                paused = 0;
            }

            if (fdwCommand & MCI_SEEK_TO_END)
            {
                dprintf("    Seek to end of disc\r\n");
                // Not very useful as a real disc can not play from this position
                plr_stop();
                playing = 0;
                plrpos = 0;
                paused = 0;
            }
            
            if (fdwCommand & MCI_TO)
            {
                dprintf("    dwTo:   %d\r\n", parms->dwTo);

                if (time_format == MCI_FORMAT_TMSF)
                {
                    current = info.first = MCI_TMSF_TRACK(parms->dwTo);
                    info.last = lastTrack;

                    dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
                    dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
                    dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
                    dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));

                    int msf_min = MCI_TMSF_MINUTE(parms->dwTo) * 60;
                    int msf_sec = MCI_TMSF_SECOND(parms->dwTo);
                    
                    if((!ACCSeekOFF && msf_min != 0) || (!ACCSeekOFF && msf_sec != 0)){
                        seek = 1;
                        plrpos = msf_min+msf_sec;
                        dprintf("seek to plrpos %d\n",plrpos);
                        paused = 1;
                    }
                    else{
                        paused = 0;
                        plrpos = 0;
                    }
                }
                else if (time_format == MCI_FORMAT_MILLISECONDS)
                {
                    int target = (parms->dwTo / 1000)+1; //+1 needed for matching logic
                    int i = firstTrack;
                    int match = 0, comp_a = 0, comp_b = 0;

                    // Find the match in a range:
                    while(i < numTracks+1){
                        comp_a = abs(tracks[i].position);
                        comp_b = abs(tracks[i].position + tracks[i].length);
                        if((target - comp_a)*(target - comp_b) <= 0){
                            match=i;
                            break;
                        }
                        i++;
                    }

                    current = info.first = match+1;
                    info.last = lastTrack;

                    if(!ACCSeekOFF && parms->dwTo / 1000 != tracks[match].position){
                        seek = 1;
                        plrpos = (parms->dwTo / 1000) - tracks[match].position;
                        dprintf("seek to plrpos %d\n",plrpos);
                        paused = 1;
                    }
                    else{
                        paused = 0;
                        plrpos = 0;
                    }

                    dprintf("      mapped milliseconds to %d\n", info.last);
                }
                else // MCI_FORMAT_MSF
                {
                    dprintf("      MINUTE %d\n", MCI_MSF_MINUTE(parms->dwTo));
                    dprintf("      SECOND %d\n", MCI_MSF_SECOND(parms->dwTo));
                    dprintf("      FRAME  %d\n", MCI_MSF_FRAME(parms->dwTo));

                    // Convert minutes and seconds to milliseconds (ignore frames)
                    int msf_min = MCI_MSF_MINUTE(parms->dwTo) * 60;
                    int msf_sec = MCI_MSF_SECOND(parms->dwTo);

                    int target = msf_min + msf_sec +1; //+1 needed for matching logic
                    int i = firstTrack;
                    int match = 0, comp_a = 0, comp_b = 0;

                    // Find the match in a range:
                    while(i < numTracks+1){
                        comp_a = abs(tracks[i].position);
                        comp_b = abs(tracks[i].position + tracks[i].length);
                        if((target - comp_a)*(target - comp_b) <= 0){
                            match=i;
                            break;
                        }
                        i++;
                    }
                    
                    current = info.first = match;
                    info.last = lastTrack;
                    
                    if(!ACCSeekOFF && msf_min+msf_sec != tracks[match].position){
                        seek = 1;
                        plrpos = (msf_min + msf_sec) - tracks[match].position;
                        dprintf("seek to plrpos %d\n",plrpos);
                        paused = 1;
                    }
                    else{
                        paused = 0;
                        plrpos = 0;
                    }
                }
                if(playing)plr_stop();
                playing = 0;
            }
            if ((fdwCommand & MCI_NOTIFY) || sendStringNotify)
            {
                sendStringNotify = 0;
                if (FullNotify && opened){
                    dprintf("  MCI_NOTIFY\r\n");
                    dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                    SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                    Sleep(50);
                }
            }
        }

        if (uMsg == MCI_CLOSE)
        {
            dprintf("  MCI_CLOSE\r\n");
            time_format = MCI_FORMAT_MSF;
            if (fdwCommand & MCI_NOTIFY)
            {
                if (FullNotify && opened){
                    notify = 0;
                    dprintf("  MCI_NOTIFY\r\n");
                    dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                    SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                    Sleep(50);
                }
            }
            opened = 0;
            /* NOTE: MCI_CLOSE does stop the music in Vista+ but the original behaviour did not
               it only closed the handle to the opened device. You could still send MCI commands
               to a default cdaudio device but if you had used an alias you needed to re-open it.
               In addition WinXP had a bug where after MCI_CLOSE the device would be unresponsive. */
        }

        if (uMsg == MCI_PLAY)
        {
            if(notify){
                notify = 0;
                dprintf("  Sending MCI_NOTIFY_ABORTED message...\r\n");
                SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_ABORTED, MAGIC_DEVICEID);
            }

            LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;

            int ignore = 0; // To deal with MCI_PLAY NULL while track is playing
            if(playing) ignore = 1;
            if(!playing)info.last = lastTrack+1; /* default MCI_TO */
            if(!playing)info.first = current; // default MCI_FROM
            plrpos2 = -1;
            
            dprintf("  MCI_PLAY\r\n");
            
            if ((fdwCommand & MCI_NOTIFY) || sendStringNotify)
            {
                dprintf("  MCI_NOTIFY\r\n");
                notify = 1; /* storing the notify request */
                sendStringNotify = 0;
            }

            if (fdwCommand & MCI_FROM)
            {
                dprintf("    dwFrom: %d\r\n", parms->dwFrom);

                if (time_format == MCI_FORMAT_TMSF)
                {
                    info.first = MCI_TMSF_TRACK(parms->dwFrom);

                    dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwFrom));
                    dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwFrom));
                    dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwFrom));
                    dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwFrom));
                    
                    int msf_min = MCI_TMSF_MINUTE(parms->dwFrom) * 60;
                    int msf_sec = MCI_TMSF_SECOND(parms->dwFrom);
                    
                    //If minutes or seconds are not zero -> seek
                    if((!ACCSeekOFF && msf_min != 0) || (!ACCSeekOFF && msf_sec != 0)){
                        seek = 1;
                        plrpos = msf_min+msf_sec;
                        dprintf("seek to plrpos %d\n",plrpos);
                    }
                }
                else if (time_format == MCI_FORMAT_MILLISECONDS)
                {
                    info.first = 0;
                    
                    int target = (parms->dwFrom / 1000)+1; //+1 needed for matching logic
                    int i = firstTrack;
                    int match = 0, comp_a = 0, comp_b = 0;

                    // Find the match in a range:
                    while(i < numTracks+1){
                        comp_a = abs(tracks[i].position);
                        comp_b = abs(tracks[i].position + tracks[i].length);
                        if((target - comp_a)*(target - comp_b) <= 0){
                            match=i;
                            break;
                        }
                        i++;
                    }

                    info.first = match;
                    dprintf("match from track: %d\n",match);
                    //If mci_from does not match track starting position seek to it.
                    if(!ACCSeekOFF && parms->dwFrom / 1000 != tracks[match].position){
                        seek = 1;
                        plrpos = (parms->dwFrom / 1000) - tracks[match].position;
                        dprintf("seek to plrpos %d\n",plrpos);
                    }

                    dprintf("      mapped milliseconds from %d\n", info.first);
                }
                else // MCI_FORMAT_MSF
                {
                    dprintf("      MINUTE %d\n", MCI_MSF_MINUTE(parms->dwFrom));
                    dprintf("      SECOND %d\n", MCI_MSF_SECOND(parms->dwFrom));
                    dprintf("      FRAME  %d\n", MCI_MSF_FRAME(parms->dwFrom));
                    
                    info.first = 0;
                    
                    int msf_min = MCI_MSF_MINUTE(parms->dwFrom) * 60;
                    int msf_sec = MCI_MSF_SECOND(parms->dwFrom);

                    int target = msf_min + msf_sec +1; //+1 needed for matching logic
                    int i = firstTrack;
                    int match = 0, comp_a = 0, comp_b = 0;

                    // Find the match in a range:
                    while(i < numTracks+1){
                        comp_a = abs(tracks[i].position);
                        comp_b = abs(tracks[i].position + tracks[i].length);
                        if((target - comp_a)*(target - comp_b) <= 0){
                            match=i;
                            break;
                        }
                        i++;
                    }

                    info.first = match;
                    dprintf("match from track: %d\n",match);
                    //If mci_from does not match track starting position seek to it.
                    if(!ACCSeekOFF && msf_min+msf_sec != tracks[match].position){
                        seek = 1;
                        plrpos = (msf_min + msf_sec) - tracks[match].position;
                        dprintf("seek to plrpos %d\n",plrpos);
                    }
                }

                if (info.first < firstTrack)
                    info.first = firstTrack;

                if (info.first > lastTrack)
                    info.first = lastTrack;

                paused = 0;
                info.last = lastTrack+1; /* default MCI_TO */
                ignore = 0;
            }

            if (fdwCommand & MCI_TO)
            {
                dprintf("    dwTo:   %d\r\n", parms->dwTo);

                if (time_format == MCI_FORMAT_TMSF)
                {
                    info.last = MCI_TMSF_TRACK(parms->dwTo);

                    dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
                    dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
                    dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
                    dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
                    
                    int msf_min = MCI_TMSF_MINUTE(parms->dwTo) * 60;
                    int msf_sec = MCI_TMSF_SECOND(parms->dwTo);
                    
                    //If minutes or seconds are not zero add to end pos
                    if((!ACCSeekOFF && msf_min != 0) || (!ACCSeekOFF && msf_sec != 0)){
                        if(info.last != info.first)info.last = MCI_TMSF_TRACK(parms->dwTo)+1;
                        plrpos2 = msf_min+msf_sec;
                        dprintf("seek to plrpos2 %d\n",plrpos2);
                    }
                }
                else if (time_format == MCI_FORMAT_MILLISECONDS)
                {
                    info.last = info.first;

                    int target = (parms->dwTo / 1000);
                    int i = firstTrack;
                    int match = 0, comp_a = 0, comp_b = 0;

                    // Find the match in a range:
                    while(i < numTracks+1){
                        comp_a = abs(tracks[i].position);
                        comp_b = abs(tracks[i].position + tracks[i].length);
                        if((target - comp_a)*(target - comp_b) <= 0){
                            match=i;
                            break;
                        }
                        i++;
                    }
                    
                    info.last = match;
                    if(info.last != info.first)info.last = match+1;
                    dprintf("match to track: %d\n",match);
                    //If mci_to does not match track start store the end as plrpos2
                    if(!ACCSeekOFF && parms->dwTo / 1000 != tracks[match].position){
                        plrpos2 = (parms->dwTo / 1000) - tracks[match].position;
                        dprintf("seek to plrpos2 %d\n",plrpos2);
                    }

                    dprintf("      mapped milliseconds to %d\n", info.last);
                }
                else // MCI_FORMAT_MSF
                {
                    dprintf("      MINUTE %d\n", MCI_MSF_MINUTE(parms->dwTo));
                    dprintf("      SECOND %d\n", MCI_MSF_SECOND(parms->dwTo));
                    dprintf("      FRAME  %d\n", MCI_MSF_FRAME(parms->dwTo));
                    
                    info.last = info.first;
                    // Convert minutes and seconds to milliseconds (ignore frames)
                    int msf_min = MCI_MSF_MINUTE(parms->dwTo) * 60;
                    int msf_sec = MCI_MSF_SECOND(parms->dwTo);

                    int target = msf_min + msf_sec;
                    int i = firstTrack;
                    int match = 0, comp_a = 0, comp_b = 0;

                    // Find the match in a range:
                    while(i < numTracks+1){
                        comp_a = abs(tracks[i].position);
                        comp_b = abs(tracks[i].position + tracks[i].length);
                        if((target - comp_a)*(target - comp_b) <= 0){
                            match=i;
                            break;
                        }
                        i++;
                    }
                    
                    info.last = match;
                    if(info.last != info.first)info.last = match+1;
                    dprintf("match to track: %d\n",match);
                    //If mci_to does not match track start store the end as plrpos2
                    if(!ACCSeekOFF && msf_min+msf_sec != tracks[match].position){
                        plrpos2 = (msf_min + msf_sec) - tracks[match].position;
                        dprintf("end at plrpos2 %d\n",plrpos2);
                    }
                }

                if (info.last < info.first)
                    info.last = info.first;

                if (info.last > lastTrack)
                    info.last = lastTrack;
                
                if(ignore){
                    seek = 1;
                    plrpos = plr_tell();
                    ignore = 0;
                }
            }

            if(!ignore){
                if (player)
                {
                    TerminateThread(player, 0);
                }

                playing = 1;
                player = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)player_main, (void *)&info, 0, NULL);
            }

        }

        // MCI_STOP and MCI_PAUSE do the same on win9x
        if (uMsg == MCI_PAUSE || uMsg == MCI_STOP)
        {
            if(uMsg == MCI_STOP)dprintf("  MCI_STOP\r\n");
            if(uMsg == MCI_PAUSE)dprintf("  MCI_PAUSE\r\n");
            if(playing){
                plrpos = plr_tell(); // save current position of ogg player
                dprintf("stop/pause plrpos %d\n",plrpos);
                paused = 1;
            }
            playing = 0;
            plr_stop();
            if(notify){
                notify = 0;
                dprintf("  Sending MCI_NOTIFY_ABORTED message...\r\n");
                SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_ABORTED, MAGIC_DEVICEID);
            }
            if ((fdwCommand & MCI_NOTIFY) || sendStringNotify)
            {
                sendStringNotify = 0;
                if (FullNotify && opened){
                    dprintf("  MCI_NOTIFY\r\n");
                    dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                    SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                    Sleep(50);
                }
            }
        }

        if (uMsg == MCI_INFO)
        {
            dprintf("  MCI_INFO\n");
            LPMCI_INFO_PARMS parms = (LPVOID)dwParam;

            if(fdwCommand & MCI_INFO_PRODUCT)
            {
                dprintf("    MCI_INFO_PRODUCT\n");
                memcpy((LPVOID)(parms->lpstrReturn), (LPVOID)&"CD Audio", 9);
                dprintf("        Return: %s\r\n", parms->lpstrReturn);
            }

            if(fdwCommand & MCI_INFO_MEDIA_IDENTITY)
            {
                dprintf("    MCI_INFO_MEDIA_IDENTITY\n");
                memcpy((LPVOID)(parms->lpstrReturn), (LPVOID)&"ABCD1234", 9);
                dprintf("        Return: %s\r\n", parms->lpstrReturn);
            }
            if (fdwCommand & MCI_NOTIFY)
            {
                if (FullNotify && opened){
                    dprintf("  MCI_NOTIFY\r\n");
                    notify = 0;
                    dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                    // Note that MCI_NOTIFY_SUPERSEDED would be sent before MCI_NOTIFY_SUCCESSFUL if track was playing, but this is not emulated.
                    SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                    Sleep(50);
                }
            }
        }

        /* Handling of MCI_SYSINFO (Heavy Gear, Battlezone2, Interstate 76) */
        if (uMsg == MCI_SYSINFO)
        {
            dprintf("  MCI_SYSINFO\r\n");
            LPMCI_SYSINFO_PARMSA parms = (LPVOID)dwParam;

            if(fdwCommand & MCI_SYSINFO_QUANTITY)
            {
                dprintf("    MCI_SYSINFO_QUANTITY\r\n");
                memcpy((LPVOID)(parms->lpstrReturn), (LPVOID)&"1", 2); /* quantity = 1 */
                //parms->dwRetSize = sizeof(DWORD);
                //parms->dwNumber = MAGIC_DEVICEID;
                dprintf("        Return: %s\r\n", parms->lpstrReturn);
            }

            if(fdwCommand & MCI_SYSINFO_NAME || fdwCommand & MCI_SYSINFO_INSTALLNAME)
            {
                dprintf("    MCI_SYSINFO_NAME\r\n");
                memcpy((LPVOID)(parms->lpstrReturn), (LPVOID)&"cdaudio", 8); /* name = cdaudio */
                //parms->dwRetSize = sizeof(DWORD);
                //parms->dwNumber = MAGIC_DEVICEID;
                dprintf("        Return: %s\r\n", parms->lpstrReturn);
            }
        }

        if (uMsg == MCI_STATUS)
        {
            LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

            dprintf("  MCI_STATUS\r\n");

            parms->dwReturn = 0;

            if (fdwCommand & MCI_TRACK)
            {
                dprintf("    MCI_TRACK\r\n");
                dprintf("      dwTrack = %d\r\n", parms->dwTrack);
            }

            if (fdwCommand & MCI_STATUS_ITEM)
            {
                dprintf("    MCI_STATUS_ITEM\r\n");

                if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
                {
                    dprintf("      MCI_STATUS_CURRENT_TRACK\r\n");
                    parms->dwReturn = current;
                }

                if (parms->dwItem == MCI_STATUS_LENGTH)
                {
                    dprintf("      MCI_STATUS_LENGTH\r\n");

                    /* Get track length */
                    if(fdwCommand & MCI_TRACK)
                    {
                        int seconds = tracks[parms->dwTrack].length;
                        if (time_format == MCI_FORMAT_MILLISECONDS)
                        {
                            parms->dwReturn = seconds * 1000;
                        }
                        else
                        {
                            parms->dwReturn = MCI_MAKE_MSF(seconds / 60, seconds % 60, 0);
                        }
                    }
                    /* Get full length */
                    else
                    {
                        if (time_format == MCI_FORMAT_MILLISECONDS)
                        {
                            parms->dwReturn = (tracks[lastTrack].position + tracks[lastTrack].length) * 1000;
                        }
                        else
                        {
                            int seconds = 0;
                            int countTracks = 1;
                            while(countTracks < numTracks){
                                seconds += tracks[countTracks].length;
                                countTracks++;
                            }
                            parms->dwReturn = MCI_MAKE_MSF(seconds / 60, seconds % 60, 0);
                        }
                    }
                }

                if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
                {
                    dprintf("      MCI_CDA_STATUS_TYPE_TRACK\r\n");
                    /*Fix from the Dxwnd project*/
                    /* ref. by WinQuake */
                    if((parms->dwTrack > 0) &&  (parms->dwTrack , MAX_TRACKS)){
                        if(tracks[parms->dwTrack].length > 0)
                            parms->dwReturn = MCI_CDA_TRACK_AUDIO;
                        else parms->dwReturn = MCI_CDA_TRACK_OTHER;
                    }
                }

                if (parms->dwItem == MCI_STATUS_MEDIA_PRESENT)
                {
                    dprintf("      MCI_STATUS_MEDIA_PRESENT\r\n");
                    parms->dwReturn = TRUE;
                }

                if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
                {
                    dprintf("      MCI_STATUS_NUMBER_OF_TRACKS\r\n");
                    parms->dwReturn = numTracks;
                }

                if (parms->dwItem == MCI_STATUS_POSITION)
                {
                    /* Track position */
                    dprintf("      MCI_STATUS_POSITION\r\n");

                    if (fdwCommand & MCI_TRACK)
                    {
                        if (time_format == MCI_FORMAT_MILLISECONDS)
                            parms->dwReturn = tracks[parms->dwTrack].position * 1000;
                        else if (time_format == MCI_FORMAT_MSF)
                            parms->dwReturn = MCI_MAKE_MSF(tracks[parms->dwTrack].position / 60, tracks[parms->dwTrack].position % 60, 0);
                        else //TMSF
                            parms->dwReturn = MCI_MAKE_TMSF(parms->dwTrack, 0, 0, 0);
                    }
                    else {
                        /* Current position */
                        int track = current % 0xFF;
                        if (time_format == MCI_FORMAT_MILLISECONDS){
                            if(!playing && !paused)parms->dwReturn = tracks[track].position * 1000;
                            else if(!playing && paused)parms->dwReturn = tracks[track].position * 1000 + plrpos * 1000;
                            else parms->dwReturn = tracks[track].position * 1000 + plr_tell() * 1000;
                        }
                        else if (time_format == MCI_FORMAT_MSF){
                            if(!playing && !paused)parms->dwReturn = MCI_MAKE_MSF(tracks[track].position / 60, tracks[track].position % 60, 0);
                            else if(!playing && paused)parms->dwReturn = MCI_MAKE_MSF((tracks[track].position + plrpos) / 60, (tracks[track].position + plrpos) % 60, 0);
                            else parms->dwReturn = MCI_MAKE_MSF((tracks[track].position + plr_tell()) / 60, (tracks[track].position + plr_tell()) % 60, 0);
                        }
                        else /* TMSF */ {
                            if(!playing && !paused)parms->dwReturn = MCI_MAKE_TMSF(track, 0, 0, 0);
                            else if(!playing && paused)parms->dwReturn = MCI_MAKE_TMSF(track, plrpos / 60, plrpos % 60, 0);
                            else parms->dwReturn = MCI_MAKE_TMSF(track, plr_tell() / 60, plr_tell() % 60, 0);
                        }
                    }
                }

                if (parms->dwItem == MCI_STATUS_MODE)
                {
                    dprintf("      MCI_STATUS_MODE\r\n");
                    
                    if(paused){ /* Handle paused state (actually the same as stopped)*/
                        dprintf("        we are paused\r\n");
                        parms->dwReturn = MCI_MODE_STOP;
                        }
                    else{
                        dprintf("        we are %s\r\n", playing ? "playing" : "NOT playing");
                        parms->dwReturn = playing ? MCI_MODE_PLAY : MCI_MODE_STOP;
                    }
                }

                if (parms->dwItem == MCI_STATUS_READY)
                {
                    dprintf("      MCI_STATUS_READY\r\n");
                    /*Fix from the Dxwnd project*/
                    /* referenced by Quake/cd_win.c */
                    parms->dwReturn = TRUE; /* TRUE=ready, FALSE=not ready */
                }

                if (parms->dwItem == MCI_STATUS_TIME_FORMAT)
                {
                    dprintf("      MCI_STATUS_TIME_FORMAT\r\n");
                    parms->dwReturn = time_format;
                }

                if (parms->dwItem == MCI_STATUS_START)
                {
                    dprintf("      MCI_STATUS_START\r\n");
                    if (time_format == MCI_FORMAT_MILLISECONDS)
                        parms->dwReturn = tracks[firstTrack].position * 1000;
                    else if (time_format == MCI_FORMAT_MSF)
                        parms->dwReturn = MCI_MAKE_MSF(tracks[firstTrack].position / 60, tracks[parms->dwTrack].position % 60, 0);
                    else //TMSF
                        parms->dwReturn = MCI_MAKE_TMSF(1, 0, 0, 0);
                }
            }

            dprintf("  dwReturn %d\n", parms->dwReturn);

            if (fdwCommand & MCI_NOTIFY)
            {
                if (FullNotify && opened){
                    dprintf("  MCI_NOTIFY\r\n");
                    notify = 0;
                    dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                    // Note that MCI_NOTIFY_SUPERSEDED would be sent before MCI_NOTIFY_SUCCESSFUL if track was playing, but this is not emulated.
                    SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                    Sleep(50);
                }
            }
        }

        return 0;
    }

    /* fallback */
    //return MCIERR_UNRECOGNIZED_COMMAND;
    else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam); /* Added MCI relay */
}

/* MCI command strings */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-command-strings */
MCIERROR WINAPI fake_mciSendStringA(LPCTSTR cmd, LPTSTR ret, UINT cchReturn, HANDLE hwndCallback)
{
    char cmdbuf[1024];
    char cmp_str[1024];

    dprintf("[MCI String = %s]\n", cmd);

    /* copy cmd into cmdbuf */
    strcpy (cmdbuf,cmd);
    /* change cmdbuf into lower case */
    for (int i = 0; cmdbuf[i]; i++)
    {
        cmdbuf[i] = tolower(cmdbuf[i]);
    }

    // handle info
    sprintf(cmp_str, "info %s", alias_s);
    if (strstr(cmdbuf, cmp_str))
    {
        if ((strstr(cmdbuf, "notify")) && FullNotify && opened){
            notify = 0;
            dprintf("  MCI_NOTIFY\r\n");
            dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
            SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
            Sleep(50);
        }
        if (strstr(cmdbuf, "identity"))
        {
            dprintf("  Returning identity: 1\r\n");
            strcpy(ret, "ABCD1234");
            return 0;
        }

        if (strstr(cmdbuf, "product"))
        {
            dprintf("  Returning product: 1\r\n");
            strcpy(ret, "CD Audio");
            return 0;
        }
    }

    // MCI_GETDEVCAPS SendString equivalent 
    sprintf(cmp_str, "capability %s", alias_s);
    if (strstr(cmdbuf, cmp_str))
    {
        if ((strstr(cmdbuf, "notify")) && FullNotify && opened){
            notify = 0;
            dprintf("  MCI_NOTIFY\r\n");
            dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
            SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
            Sleep(50);
        }
        if (strstr(cmdbuf, "device type")){
            strcpy(ret, "cdaudio");
        }
        else if (strstr(cmdbuf, "can eject")){
            strcpy(ret, "true");
        }
        else if (strstr(cmdbuf, "can play")){
            strcpy(ret, "true");
        }
        else if (strstr(cmdbuf, "has audio")){
            strcpy(ret, "true");
        }
        else{
            strcpy(ret, "false");
        }
        return 0;
    }

    // Handle sysinfo (does not use alias!)
    if (strstr(cmdbuf, "sysinfo cdaudio")){
        if (strstr(cmdbuf, "quantity"))
        {
           dprintf("  Returning quantity: 1\r\n");
           strcpy(ret, "1");
           return 0;
        }
        /* Example: "sysinfo cdaudio name 1 open" returns "cdaudio" or the alias.*/
        if (strstr(cmdbuf, "name"))
        {
            if (strstr(cmdbuf, "open")){
                dprintf("  Returning alias name: %s\r\n",alias_s);
                sprintf(ret, "%s", alias_s);
                return 0;
            }
        }
        if (strstr(cmdbuf, "name") || strstr(cmdbuf, "installname"))
        {
            dprintf("  Returning name: cdaudio\r\n");
            strcpy(ret, "cdaudio");
            return 0;
        }
    }

    /* Handle "stop cdaudio/alias" */
    sprintf(cmp_str, "stop %s", alias_s);
    if (strstr(cmdbuf, cmp_str))
    {
        if (strstr(cmdbuf, "notify")){
            if(FullNotify && opened)sendStringNotify = 1; /* storing the notify request */
        }
        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
        return 0;
    }

    /* Handle "pause cdaudio/alias" */
    sprintf(cmp_str, "pause %s", alias_s);
    if (strstr(cmdbuf, cmp_str))
    {
        if (strstr(cmdbuf, "notify")){
            if(FullNotify && opened)sendStringNotify = 1; /* storing the notify request */
        }
        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PAUSE, 0, (DWORD_PTR)NULL);
        return 0;
    }

    // Handle "open"
    if (strstr(cmdbuf, "open")){
        /* Look for the use of an alias */
        /* Example: "open d: type cdaudio alias cd1" */
        if (strstr(cmdbuf, "type cdaudio alias"))
        {
            char *tmp_s = strrchr(cmdbuf, ' ');
            if (tmp_s && *(tmp_s +1))
            {
                sprintf(alias_s, "%s", tmp_s +1);
            }
            char devid_str[100];
            sprintf(devid_str, "%d", MAGIC_DEVICEID);
            strcpy(ret, devid_str);
            if ((strstr(cmdbuf, "notify")) && FullNotify && !opened){
                dprintf("  MCI_NOTIFY\r\n");
                dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                Sleep(50);
            }
            opened = 1;
            return 0;
        }
        /* Look for the use of an alias */
        /* Example: "open cdaudio alias cd1" */
        if (strstr(cmdbuf, "open cdaudio alias"))
        {
            char *tmp_s = strrchr(cmdbuf, ' ');
            if (tmp_s && *(tmp_s +1))
            {
                sprintf(alias_s, "%s", tmp_s +1);
                dprintf("alias is: %s\n",alias_s);
            }
            char devid_str[100];
            sprintf(devid_str, "%d", MAGIC_DEVICEID);
            strcpy(ret, devid_str);
            if ((strstr(cmdbuf, "notify")) && FullNotify && !opened){
                dprintf("  MCI_NOTIFY\r\n");
                dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                Sleep(50);
            }
            opened = 1;
            return 0;
        }
        // Normal open cdaudio
        if (strstr(cmdbuf, "open cdaudio"))
        {
            char devid_str[100];
            sprintf(devid_str, "%d", MAGIC_DEVICEID);
            strcpy(ret, devid_str);
            if ((strstr(cmdbuf, "notify")) && FullNotify && !opened){
                dprintf("  MCI_NOTIFY\r\n");
                dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
                SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
                Sleep(50);
            }
            opened = 1;
            return 0;
        }
    }

    /* reset alias with "close alias" string */
    sprintf(cmp_str, "close %s", alias_s);
    if (strstr(cmdbuf, cmp_str))
    {
        sprintf(alias_s, "cdaudio");
        time_format = MCI_FORMAT_MSF; // reset time format
        if ((strstr(cmdbuf, "notify")) && FullNotify && opened){
            notify = 0;
            dprintf("  MCI_NOTIFY\r\n");
            dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
            SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
            Sleep(50);
        }
        opened = 0;
        return 0;
    }

    /* Handle "set cdaudio/alias" */
    sprintf(cmp_str, "set %s", alias_s);
    if (strstr(cmdbuf, cmp_str)){
        if ((strstr(cmdbuf, "notify")) && FullNotify && opened){
            notify = 0;
            dprintf("  MCI_NOTIFY\r\n");
            dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
            SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
            Sleep(50);
        }
        if (strstr(cmdbuf, "milliseconds"))
        {
            static MCI_SET_PARMS parms;
            parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
            return 0;
        }
        if (strstr(cmdbuf, "tmsf"))
        {
            static MCI_SET_PARMS parms;
            parms.dwTimeFormat = MCI_FORMAT_TMSF;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
            return 0;
        }
        if (strstr(cmdbuf, "msf"))
        {
            static MCI_SET_PARMS parms;
            parms.dwTimeFormat = MCI_FORMAT_MSF;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
            return 0;
        }
        if (strstr(cmdbuf, "ms")) // Another accepted string for milliseconds
        {
            static MCI_SET_PARMS parms;
            parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
            return 0;
        }
        if (strstr(cmdbuf, "audio all off"))
        {
            plr_volume(0);
            return 0;
        }
        if (strstr(cmdbuf, "audio all on"))
        {
            plr_volume(100);
            return 0;
        }
        if (strstr(cmdbuf, "audio left off") || strstr(cmdbuf, "audio left on") || strstr(cmdbuf, "audio right off") || strstr(cmdbuf, "audio right on"))
        {
            // No handling for left / right channel
            return 0;
        }
    }

    /* Handle "status cdaudio/alias" */
    sprintf(cmp_str, "status %s", alias_s);
    if (strstr(cmdbuf, cmp_str)){
        if ((strstr(cmdbuf, "notify")) && FullNotify && opened){
            notify = 0;
            dprintf("  MCI_NOTIFY\r\n");
            dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
            SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
            Sleep(50);
        }
        if (strstr(cmdbuf, "time format"))
        {
            if(time_format==MCI_FORMAT_MILLISECONDS){
                strcpy(ret, "milliseconds");
                return 0;
            }
            if(time_format==MCI_FORMAT_TMSF){
                strcpy(ret, "tmsf");
                return 0;
            }
            if(time_format==MCI_FORMAT_MSF){
                strcpy(ret, "msf");
                return 0;
            }
        }
        if (strstr(cmdbuf, "number of tracks"))
        {
            dprintf("  Returning number of tracks (%d)\r\n", numTracks);
            sprintf(ret, "%d", numTracks);
            return 0;
        }
        if (strstr(cmdbuf, "current track"))
        {
            dprintf("  Current track is (%d)\r\n", current);
            sprintf(ret, "%d", current);
            return 0;
        }
        int track = 0;
        if (sscanf(cmdbuf, "status %*s type track %d", &track) == 1)
        {
            if((track > 0) &&  (track , MAX_TRACKS)){
                if(tracks[track].length > 0)
                    strcpy(ret, "audio");
                else strcpy(ret, "other");
            }
            return 0;
        }
        if (sscanf(cmdbuf, "status %*s length track %d", &track) == 1)
        {
            static MCI_STATUS_PARMS parms;
            parms.dwItem = MCI_STATUS_LENGTH;
            parms.dwTrack = track;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
            if(time_format == MCI_FORMAT_MILLISECONDS){
                sprintf(ret, "%d", parms.dwReturn);
            }
            if(time_format == MCI_FORMAT_MSF || time_format == MCI_FORMAT_TMSF){
                sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
            }
            return 0;
        }
        if (strstr(cmdbuf, "length"))
        {
            static MCI_STATUS_PARMS parms;
            parms.dwItem = MCI_STATUS_LENGTH;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
            if(time_format == MCI_FORMAT_MILLISECONDS){
                sprintf(ret, "%d", parms.dwReturn);
            }
            if(time_format == MCI_FORMAT_MSF || time_format == MCI_FORMAT_TMSF){
                sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
            }
            return 0;
        }
        if (sscanf(cmdbuf, "status %*s position track %d", &track) == 1)
        {
            static MCI_STATUS_PARMS parms;
            parms.dwItem = MCI_STATUS_POSITION;
            parms.dwTrack = track;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
            if(time_format == MCI_FORMAT_MILLISECONDS){
                sprintf(ret, "%d", parms.dwReturn);
            }
            if(time_format == MCI_FORMAT_MSF){
                sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
            }
            if(time_format == MCI_FORMAT_TMSF){
                sprintf(ret, "%02d:%02d:%02d:%02d", MCI_TMSF_TRACK(parms.dwReturn), MCI_TMSF_MINUTE(parms.dwReturn), MCI_TMSF_SECOND(parms.dwReturn), MCI_TMSF_FRAME(parms.dwReturn));
            }
            return 0;
        }
        if (strstr(cmdbuf, "start position"))
        {
            static MCI_STATUS_PARMS parms;
            parms.dwItem = MCI_STATUS_POSITION;
            parms.dwTrack = firstTrack;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
            if(time_format == MCI_FORMAT_MILLISECONDS){
                sprintf(ret, "%d", parms.dwReturn);
            }
            if(time_format == MCI_FORMAT_MSF){
                sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
            }
            if(time_format == MCI_FORMAT_TMSF){
                parms.dwTrack = 1;
                sprintf(ret, "%02d:%02d:%02d:%02d", MCI_TMSF_TRACK(parms.dwReturn), MCI_TMSF_MINUTE(parms.dwReturn), MCI_TMSF_SECOND(parms.dwReturn), MCI_TMSF_FRAME(parms.dwReturn));
            }
            return 0;
        }
        if (strstr(cmdbuf, "position"))
        {
            static MCI_STATUS_PARMS parms;
            parms.dwItem = MCI_STATUS_POSITION;
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
            if(time_format == MCI_FORMAT_MILLISECONDS){
                sprintf(ret, "%d", parms.dwReturn);
            }
            if(time_format == MCI_FORMAT_MSF){
                sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
            }
            if(time_format == MCI_FORMAT_TMSF){
                sprintf(ret, "%02d:%02d:%02d:%02d", MCI_TMSF_TRACK(parms.dwReturn), MCI_TMSF_MINUTE(parms.dwReturn), MCI_TMSF_SECOND(parms.dwReturn), MCI_TMSF_FRAME(parms.dwReturn));
            }
            return 0;
        }
        if (strstr(cmdbuf, "media present"))
        {
            strcpy(ret, "TRUE");
            return 0;
        }
        /* Add: Mode handling */
        if (strstr(cmdbuf, "mode"))
        {
            if(paused || !playing){
                dprintf("   -> stopped\r\n");
                strcpy(ret, "stopped");
                }
            else{
                dprintf("   -> playing\r\n");
                strcpy(ret, "playing");
            }
            return 0;
        }
    }

    // Handle Seek cdaudio
    sprintf(cmp_str, "seek %s", alias_s);
    if (strstr(cmdbuf, cmp_str)){
        if (strstr(cmdbuf, "notify")){
            if(FullNotify && opened)sendStringNotify = 1; /* storing the notify request */
        }
        if (strstr(cmdbuf, "to start")){
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_SEEK_TO_START, (DWORD_PTR)NULL);
            return 0;
        }
        if (strstr(cmdbuf, "to end")){
            fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_SEEK_TO_END, (DWORD_PTR)NULL);
            return 0;
        }
        if(time_format == MCI_FORMAT_MSF){
            int seek_min = -1, seek_sec = -1, seek_frm = -1;
            if (sscanf(cmdbuf, "seek %*s to %d:%d:%d", &seek_min, &seek_sec, &seek_frm) == 3)
            {
                dprintf("MSF seek to x:x:x\n");
                static MCI_SEEK_PARMS parms;
                parms.dwTo = MCI_MAKE_MSF(seek_min, seek_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "seek %*s to %d:%d", &seek_min, &seek_sec) == 2)
            {
                dprintf("MSF seek to x:x\n");
                static MCI_SEEK_PARMS parms;
                parms.dwTo = MCI_MAKE_MSF(seek_min, seek_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "seek %*s to %d", &seek_min) == 1)
            {
                dprintf("MSF seek to x\n");
                static MCI_SEEK_PARMS parms;
                parms.dwTo = MCI_MAKE_MSF(seek_min, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
        }
        else if(time_format == MCI_FORMAT_TMSF){
            int seek_track = -1, seek_min = -1, seek_sec = -1, seek_frm = -1;
            if (sscanf(cmdbuf, "seek %*s to %d:%d:%d:%d", &seek_track, &seek_min, &seek_sec, &seek_frm) == 4)
            {
                dprintf("TMSF seek to x:x:x:x\n");
                static MCI_SEEK_PARMS parms;
                parms.dwTo = MCI_MAKE_TMSF(seek_track, seek_min, seek_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "seek %*s to %d:%d:%d", &seek_track, &seek_min, &seek_sec) == 3)
            {
                dprintf("TMSF seek to x:x:x\n");
                static MCI_SEEK_PARMS parms;
                parms.dwTo = MCI_MAKE_TMSF(seek_track, seek_min, seek_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "seek %*s to %d:%d", &seek_track, &seek_min) == 2)
            {
                dprintf("TMSF seek to x:x\n");
                static MCI_SEEK_PARMS parms;
                parms.dwTo = MCI_MAKE_TMSF(seek_track, seek_min, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "seek %*s to %d", &seek_track) == 1)
            {
                dprintf("TMSF seek to x\n");
                static MCI_SEEK_PARMS parms;
                parms.dwTo = MCI_MAKE_TMSF(seek_track, 0, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
        }
        // Milliseconds
        else{
            int seek_ms = -1;
            if (sscanf(cmdbuf, "seek %*s to %d", &seek_ms) == 1)
            {
                static MCI_SEEK_PARMS parms;
                parms.dwTo = seek_ms;
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SEEK, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
        }
    }

    /* Handle "play cdaudio/alias" */
    int from = -1, to = -1;
    sprintf(cmp_str, "play %s", alias_s);
    if (strstr(cmdbuf, cmp_str)){
        if (strstr(cmdbuf, "notify")){
        sendStringNotify = 1; /* storing the notify request */
        }
        if(time_format == MCI_FORMAT_MSF){
            int from_sec = -1, to_sec = -1; // seconds
            int from_frm = -1, to_frm = -1; // frames (ignored for now)
            if (sscanf(cmdbuf, "play %*s from %d:%d:%d to %d:%d:%d", &from, &from_sec, &from_frm, &to, &to_sec, &to_frm) == 6)
            {
                dprintf("MSF play from x:x:x to x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_MSF(from, from_sec, 0);
                parms.dwTo = MCI_MAKE_MSF(to, to_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d:%d:%d", &from, &from_sec, &from_frm) == 3)
            {
                dprintf("MSF play from x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_MSF(from, from_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s to %d:%d:%d", &to, &to_sec, &to_frm) == 3)
            {
                dprintf("MSF play to x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwTo = MCI_MAKE_MSF(to, to_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d:%d to %d:%d", &from, &from_sec, &to, &to_sec) == 4)
            {
                dprintf("MSF play from x:x to x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_MSF(from, from_sec, 0);
                parms.dwTo = MCI_MAKE_MSF(to, to_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d:%d", &from, &from_sec) == 2)
            {
                dprintf("MSF play from x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_MSF(from, from_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s to %d:%d", &to, &to_sec) == 2)
            {
                dprintf("MSF play to x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwTo = MCI_MAKE_MSF(to, to_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d to %d", &from, &to) == 2)
            {
                dprintf("MSF play from x to x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_MSF(from, 0, 0);
                parms.dwTo = MCI_MAKE_MSF(to, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d", &from) == 1)
            {
                dprintf("MSF play from x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_MSF(from, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s to %d", &to) == 1)
            {
                dprintf("MSF play to x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwTo = MCI_MAKE_MSF(to, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
        }
        else if(time_format == MCI_FORMAT_TMSF){
            int from_min = -1, to_min = -1; // minutes
            int from_sec = -1, to_sec = -1; // seconds
            int from_frm = -1, to_frm = -1; // frames (ignored for now)
            if (sscanf(cmdbuf, "play %*s from %d:%d:%d:%d to %d:%d:%d:%d", &from, &from_min, &from_sec, &from_frm, &to, &to_min, &to_sec, &to_frm) == 8)
            {
                dprintf("TMSF play from x:x:x:x to x:x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_TMSF(from, from_min, from_sec, 0);
                parms.dwTo = MCI_MAKE_TMSF(to, to_min, to_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d:%d:%d:%d", &from, &from_min, &from_sec, &from_frm) == 4)
            {
                dprintf("TMSF play from x:x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_TMSF(from, from_min, from_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s to %d:%d:%d:%d", &to, &to_min, &to_sec, &to_frm) == 4)
            {
                dprintf("TMSF play to x:x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwTo = MCI_MAKE_TMSF(to, to_min, to_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d:%d:%d to %d:%d:%d", &from, &from_min, &from_sec, &to, &to_min, &to_sec) == 6)
            {
                dprintf("TMSF play from x:x:x to x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_TMSF(from, from_min, from_sec, 0);
                parms.dwTo = MCI_MAKE_TMSF(to, to_min, to_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d:%d:%d", &from, &from_min, &from_sec) == 3)
            {
                dprintf("TMSF play from x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_TMSF(from, from_min, from_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s to %d:%d:%d", &to, &to_min, &to_sec) == 3)
            {
                dprintf("TMSF play to x:x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwTo = MCI_MAKE_TMSF(to, to_min, to_sec, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d:%d to %d:%d", &from, &from_min, &to, &to_min) == 4)
            {
                dprintf("TMSF play from x:x to x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_TMSF(from, from_min, 0, 0);
                parms.dwTo = MCI_MAKE_TMSF(to, to_min, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d:%d", &from, &from_min) == 2)
            {
                dprintf("TMSF play from x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_TMSF(from, from_min, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s to %d:%d", &to, &to_min) == 2)
            {
                dprintf("TMSF play to x:x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwTo = MCI_MAKE_TMSF(to, to_min, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d to %d", &from, &to) == 2)
            {
                dprintf("TMSF play from x to x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_TMSF(from, 0, 0, 0);
                parms.dwTo = MCI_MAKE_TMSF(to, 0, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d", &from) == 1)
            {
                dprintf("TMSF play from x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = MCI_MAKE_TMSF(from, 0, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s to %d", &to) == 1)
            {
                dprintf("TMSF play to x\n");
                static MCI_PLAY_PARMS parms;
                parms.dwTo = MCI_MAKE_TMSF(to, 0, 0, 0);
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
        }
        // Milliseconds
        else{
            if (sscanf(cmdbuf, "play %*s from %d to %d", &from, &to) == 2)
            {
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = from;
                parms.dwTo = to;
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s from %d", &from) == 1)
            {
                static MCI_PLAY_PARMS parms;
                parms.dwFrom = from;
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                return 0;
            }
            if (sscanf(cmdbuf, "play %*s to %d", &to) == 1)
            {
                static MCI_PLAY_PARMS parms;
                parms.dwTo = to;
                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
                return 0;
            }
        }
    }
    // Handle play cdaudio null
    if (strstr(cmdbuf, cmp_str)){
        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, 0, (DWORD_PTR)NULL);
        return 0;
    }

    //return 0;
    return relay_mciSendStringA(cmd, ret, cchReturn, hwndCallback); /* Added MCI relay */
}

UINT WINAPI fake_auxGetNumDevs()
{
    dprintf("fake_auxGetNumDevs()\r\n");
    return 1;
}

MMRESULT WINAPI fake_auxGetDevCapsA(UINT_PTR uDeviceID, LPAUXCAPS lpCaps, UINT cbCaps)
{
    dprintf("fake_auxGetDevCapsA(uDeviceID=%08X, lpCaps=%p, cbCaps=%08X\n", uDeviceID, lpCaps, cbCaps);

    lpCaps->wMid = 2 /*MM_CREATIVE*/;
    lpCaps->wPid = 401 /*MM_CREATIVE_AUX_CD*/;
    lpCaps->vDriverVersion = 1;
    strcpy(lpCaps->szPname, "ogg-winmm virtual CD");
    lpCaps->wTechnology = AUXCAPS_CDAUDIO;
    lpCaps->dwSupport = AUXCAPS_VOLUME;

    return MMSYSERR_NOERROR;
}


MMRESULT WINAPI fake_auxGetVolume(UINT uDeviceID, LPDWORD lpdwVolume)
{
    dprintf("fake_auxGetVolume(uDeviceId=%08X, lpdwVolume=%p)\r\n", uDeviceID, lpdwVolume);
    *lpdwVolume = 0x00000000;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI fake_auxSetVolume(UINT uDeviceID, DWORD dwVolume)
{
    static DWORD oldVolume = -1;
    char cmdbuf[256];

    dprintf("fake_auxSetVolume(uDeviceId=%08X, dwVolume=%08X)\r\n", uDeviceID, dwVolume);

    if (dwVolume == oldVolume)
    {
        return MMSYSERR_NOERROR;
    }

    oldVolume = dwVolume;

    unsigned short left = LOWORD(dwVolume);
    unsigned short right = HIWORD(dwVolume);

    dprintf("    left : %ud (%04X)\n", left, left);
    dprintf("    right: %ud (%04X)\n", right, right);

    plr_volume((left / 65535.0f) * 100);

    return MMSYSERR_NOERROR;
}
