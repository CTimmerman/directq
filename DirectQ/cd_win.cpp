/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
// Quake is a trademark of Id Software, Inc., (c) 1996 Id Software, Inc. All
// rights reserved.

#include "quakedef.h"

extern	HWND	d3d_Window;
extern	cvar_t	bgmvolume;

static bool cdValid = false;
static bool	playing = false;
static bool	wasPlaying = false;
static bool	initialized = false;
static bool	enabled = false;
static bool playLooping = false;
static float	cdvolume;
static byte 	remap[100];
static byte		cdrom;
static byte		playTrack;
static byte		maxTrack;
static bool		using_directshow = false;

UINT	wDeviceID;


static void CDAudio_Eject (void)
{
	DWORD	dwReturn;

	if (dwReturn = mciSendCommand (wDeviceID, MCI_SET, MCI_SET_DOOR_OPEN, (DWORD) NULL))
		Con_DPrintf ("MCI_SET_DOOR_OPEN failed (%i)\n", dwReturn);
}


static void CDAudio_CloseDoor (void)
{
	DWORD	dwReturn;

	if (dwReturn = mciSendCommand (wDeviceID, MCI_SET, MCI_SET_DOOR_CLOSED, (DWORD) NULL))
		Con_DPrintf ("MCI_SET_DOOR_CLOSED failed (%i)\n", dwReturn);
}


static int CDAudio_GetAudioDiskInfo (void)
{
	DWORD				dwReturn;
	MCI_STATUS_PARMS	mciStatusParms;

	cdValid = false;

	mciStatusParms.dwItem = MCI_STATUS_READY;
	dwReturn = mciSendCommand (wDeviceID, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT, (DWORD) (LPVOID) &mciStatusParms);

	if (dwReturn)
	{
		Con_DPrintf ("CDAudio: drive ready test - get status failed\n");
		return -1;
	}

	if (!mciStatusParms.dwReturn)
	{
		Con_DPrintf ("CDAudio: drive not ready\n");
		return -1;
	}

	mciStatusParms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
	dwReturn = mciSendCommand (wDeviceID, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT, (DWORD) (LPVOID) &mciStatusParms);

	if (dwReturn)
	{
		Con_DPrintf ("CDAudio: get tracks - status failed\n");
		return -1;
	}

	if (mciStatusParms.dwReturn < 1)
	{
		Con_DPrintf ("CDAudio: no music tracks\n");
		return -1;
	}

	cdValid = true;
	maxTrack = mciStatusParms.dwReturn;

	return 0;
}


void CDAudio_Play (byte track, bool looping)
{
	// lotten flucking ruck
	if (nehahra) return;

	DWORD				dwReturn;
	MCI_PLAY_PARMS		mciPlayParms;
	MCI_STATUS_PARMS	mciStatusParms;

	using_directshow = false;

	if (!enabled)
	{
		// try directshow
		using_directshow = MediaPlayer_Play (track, looping);
		return;
	}

	if (!cdValid)
	{
		// try one more time
		CDAudio_GetAudioDiskInfo();

		// didn't work
		if (!cdValid)
		{
			// play with directshow instead
			using_directshow = MediaPlayer_Play (track, looping);
			return;
		}
	}

	track = remap[track];

	if (track < 1 || track > maxTrack)
	{
		Con_DPrintf ("CDAudio: Bad track number %u.\n", track);
		return;
	}

	// don't try to play a non-audio track
	mciStatusParms.dwItem = MCI_CDA_STATUS_TYPE_TRACK;
	mciStatusParms.dwTrack = track;
	dwReturn = mciSendCommand (wDeviceID, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK | MCI_WAIT, (DWORD) (LPVOID) &mciStatusParms);

	if (dwReturn)
	{
		Con_DPrintf ("MCI_STATUS failed (%i)\n", dwReturn);
		return;
	}

	if (mciStatusParms.dwReturn != MCI_CDA_TRACK_AUDIO)
	{
		Con_Printf ("CDAudio: track %i is not audio\n", track);
		return;
	}

	// get the length of the track to be played
	mciStatusParms.dwItem = MCI_STATUS_LENGTH;
	mciStatusParms.dwTrack = track;
	dwReturn = mciSendCommand (wDeviceID, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK | MCI_WAIT, (DWORD) (LPVOID) &mciStatusParms);

	if (dwReturn)
	{
		Con_DPrintf ("MCI_STATUS failed (%i)\n", dwReturn);
		return;
	}

	if (playing)
	{
		if (playTrack == track)
			return;

		CDAudio_Stop();
	}

	mciPlayParms.dwFrom = MCI_MAKE_TMSF (track, 0, 0, 0);
	mciPlayParms.dwTo = (mciStatusParms.dwReturn << 8) | track;
	mciPlayParms.dwCallback = (DWORD) d3d_Window;
	dwReturn = mciSendCommand (wDeviceID, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO, (DWORD) (LPVOID) &mciPlayParms);

	if (dwReturn)
	{
		Con_DPrintf ("CDAudio: MCI_PLAY failed (%i)\n", dwReturn);
		return;
	}

	playLooping = looping;
	playTrack = track;
	playing = true;

	if (cdvolume == 0.0)
		CDAudio_Pause ();
}


void CDAudio_Stop (void)
{
	DWORD	dwReturn;

	if (using_directshow)
	{
		MediaPlayer_Stop ();
		return;
	}

	if (!enabled)
		return;

	if (!playing)
		return;

	if (dwReturn = mciSendCommand (wDeviceID, MCI_STOP, 0, (DWORD) NULL))
		Con_DPrintf ("MCI_STOP failed (%i)", dwReturn);

	wasPlaying = false;
	playing = false;
}


void CDAudio_Pause (void)
{
	if (using_directshow)
	{
		MediaPlayer_Pause ();
		return;
	}

	DWORD				dwReturn;
	MCI_GENERIC_PARMS	mciGenericParms;

	if (!enabled)
		return;

	if (!playing)
		return;

	mciGenericParms.dwCallback = (DWORD) d3d_Window;

	if (dwReturn = mciSendCommand (wDeviceID, MCI_PAUSE, 0, (DWORD) (LPVOID) &mciGenericParms))
		Con_DPrintf ("MCI_PAUSE failed (%i)", dwReturn);

	wasPlaying = playing;
	playing = false;
}


void CDAudio_Resume (void)
{
	if (using_directshow)
	{
		MediaPlayer_Resume ();
		return;
	}

	DWORD			dwReturn;
	MCI_PLAY_PARMS	mciPlayParms;

	if (!enabled)
		return;

	if (!cdValid)
		return;

	if (!wasPlaying)
		return;

	mciPlayParms.dwFrom = MCI_MAKE_TMSF (playTrack, 0, 0, 0);
	mciPlayParms.dwTo = MCI_MAKE_TMSF (playTrack + 1, 0, 0, 0);
	mciPlayParms.dwCallback = (DWORD) d3d_Window;
	dwReturn = mciSendCommand (wDeviceID, MCI_PLAY, MCI_TO | MCI_NOTIFY, (DWORD) (LPVOID) &mciPlayParms);

	if (dwReturn)
	{
		Con_DPrintf ("CDAudio: MCI_PLAY failed (%i)\n", dwReturn);
		return;
	}

	playing = true;
}


static void CD_f (void)
{
	char	*command;
	int		n;

	if (Cmd_Argc() < 2)
		return;

	command = Cmd_Argv (1);

	if (_stricmp (command, "on") == 0)
	{
		enabled = true;
		return;
	}

	if (_stricmp (command, "off") == 0)
	{
		if (playing)
			CDAudio_Stop();

		enabled = false;
		return;
	}

	if (_stricmp (command, "reset") == 0)
	{
		enabled = true;

		if (playing)
			CDAudio_Stop();

		for (n = 0; n < 100; n++)
			remap[n] = n;

		CDAudio_GetAudioDiskInfo();
		return;
	}

	if (_stricmp (command, "remap") == 0)
	{
		int ret = Cmd_Argc() - 2;

		if (ret <= 0)
		{
			for (n = 1; n < 100; n++)
				if (remap[n] != n)
					Con_Printf ("  %u -> %u\n", n, remap[n]);

			return;
		}

		for (n = 1; n <= ret; n++)
			remap[n] = atoi (Cmd_Argv (n + 1));

		return;
	}

	if (_stricmp (command, "close") == 0)
	{
		CDAudio_CloseDoor();
		return;
	}

	if (_stricmp (command, "play") == 0)
	{
		CDAudio_Play ((byte) atoi (Cmd_Argv (2)), false);
		return;
	}

	if (_stricmp (command, "loop") == 0)
	{
		CDAudio_Play ((byte) atoi (Cmd_Argv (2)), true);
		return;
	}

	if (_stricmp (command, "stop") == 0)
	{
		CDAudio_Stop();
		return;
	}

	if (_stricmp (command, "pause") == 0)
	{
		CDAudio_Pause();
		return;
	}

	if (_stricmp (command, "resume") == 0)
	{
		CDAudio_Resume();
		return;
	}

	if (_stricmp (command, "info") == 0)
	{
		Con_Printf ("%u tracks\n", maxTrack);

		if (playing)
			Con_Printf ("Currently %s track %u\n", playLooping ? "looping" : "playing", playTrack);
		else if (wasPlaying)
			Con_Printf ("Paused %s track %u\n", playLooping ? "looping" : "playing", playTrack);

		Con_Printf ("Volume is %f\n", cdvolume);
		return;
	}

	// the intention is that the "cd" command works with MP3 tracks too, so here we only check if there is a CD in the drive
	if (!cdValid)
	{
		CDAudio_GetAudioDiskInfo();

		if (!cdValid)
		{
			Con_Printf ("No CD in player.\n");
			return;
		}
	}

	if (_stricmp (command, "eject") == 0)
	{
		if (playing)
			CDAudio_Stop();

		CDAudio_Eject();
		cdValid = false;
		return;
	}
}


LONG CDAudio_MessageHandler (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (lParam != wDeviceID)
		return 1;

	// don't handle CD messages if using directshow
	if (using_directshow) return 0;

	switch (wParam)
	{
	case MCI_NOTIFY_SUCCESSFUL:

		if (playing)
		{
			playing = false;

			if (playLooping)
				CDAudio_Play (playTrack, true);
		}

		break;

	case MCI_NOTIFY_ABORTED:
	case MCI_NOTIFY_SUPERSEDED:
		break;

	case MCI_NOTIFY_FAILURE:
		Con_DPrintf ("MCI_NOTIFY_FAILURE\n");
		CDAudio_Stop ();
		cdValid = false;
		break;

	default:
		Con_DPrintf ("Unexpected MM_MCINOTIFY type (%i)\n", wParam);
		return 1;
	}

	return 0;
}


void CDAudio_Update (void)
{
	if (!enabled)
		return;

	if (bgmvolume.value != cdvolume)
	{
		if (using_directshow)
		{
			MediaPlayer_ChangeVolume ();
			cdvolume = bgmvolume.value;
			return;
		}

		if (cdvolume)
		{
			cdvolume = bgmvolume.value;
			CDAudio_Pause ();
		}
		else
		{
			cdvolume = bgmvolume.value;
			CDAudio_Resume ();
		}
	}
}


int CDAudio_Init (void)
{
	DWORD	dwReturn;
	MCI_OPEN_PARMS	mciOpenParms;
	MCI_SET_PARMS	mciSetParms;
	int				n;

	if (COM_CheckParm ("-nocdaudio")) return -1;

	mciOpenParms.lpstrDeviceType = "cdaudio";

	if (dwReturn = mciSendCommand (0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_SHAREABLE, (DWORD) (LPVOID) &mciOpenParms))
	{
		Con_Printf ("CDAudio_Init: MCI_OPEN failed (%i)\n", dwReturn);
		return -1;
	}

	wDeviceID = mciOpenParms.wDeviceID;

	// Set the time format to track/minute/second/frame (TMSF).
	mciSetParms.dwTimeFormat = MCI_FORMAT_TMSF;

	if (dwReturn = mciSendCommand (wDeviceID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD) (LPVOID) &mciSetParms))
	{
		Con_Printf ("MCI_SET_TIME_FORMAT failed (%i)\n", dwReturn);
		mciSendCommand (wDeviceID, MCI_CLOSE, 0, (DWORD) NULL);
		return -1;
	}

	for (n = 0; n < 100; n++)
		remap[n] = n;

	initialized = true;
	enabled = true;

	if (CDAudio_GetAudioDiskInfo())
	{
		Con_Printf ("CDAudio_Init: No CD in player.\n");
		cdValid = false;
	}

	Con_Printf ("CD Audio Initialized\n");

	return 0;
}

cmd_t CD_f_Cmd ("cd", CD_f);
cmd_t MP3_f_Cmd ("mp3", CD_f);

void CDAudio_Shutdown (void)
{
	// directshow has it's own shutdown
	if (!initialized)
		return;

	CDAudio_Stop();

	if (mciSendCommand (wDeviceID, MCI_CLOSE, MCI_WAIT, (DWORD) NULL))
		Con_DPrintf ("CDAudio_Shutdown: MCI_CLOSE failed\n");
}
