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

// sys.h -- non-portable functions

int	Sys_FileExists (char *path);
void Sys_mkdir (char *path);
void Sys_ThreadCatchup (void);

// system IO
void Sys_DebugLog (char *file, char *fmt, ...);

void Sys_Error (char *error, ...);
// an error will cause the entire program to exit

void Sys_Quit (int ExitCode);

double Sys_FloatTime (void);

void Sys_SendKeyEvents (void);
// Perform Key_Event () callbacks until the input que is empty

void Sys_LowFPPrecision (void);
void Sys_HighFPPrecision (void);
void Sys_SetFPCW (void);

extern SYSTEM_INFO SysInfo;
