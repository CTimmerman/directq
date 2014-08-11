/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cvar.c -- dynamic variable tracking

#include "quakedef.h"

cvar_t	*cvar_vars = NULL;
char *cvar_null_string = "";

cvar_t *Cmd_FindCvar (char *name);

// poxy fucking nehahra cvars
// and there I was thinking that HIPNOTIC was a collection of vile hacks...
cvar_t nehx00 ("nehx00", "0", CVAR_NEHAHRA); cvar_t nehx01 ("nehx01", "0", CVAR_NEHAHRA);
cvar_t nehx02 ("nehx02", "0", CVAR_NEHAHRA); cvar_t nehx03 ("nehx03", "0", CVAR_NEHAHRA);
cvar_t nehx04 ("nehx04", "0", CVAR_NEHAHRA); cvar_t nehx05 ("nehx05", "0", CVAR_NEHAHRA);
cvar_t nehx06 ("nehx06", "0", CVAR_NEHAHRA); cvar_t nehx07 ("nehx07", "0", CVAR_NEHAHRA);
cvar_t nehx08 ("nehx08", "0", CVAR_NEHAHRA); cvar_t nehx09 ("nehx09", "0", CVAR_NEHAHRA); 
cvar_t nehx10 ("nehx10", "0", CVAR_NEHAHRA); cvar_t nehx11 ("nehx11", "0", CVAR_NEHAHRA);
cvar_t nehx12 ("nehx12", "0", CVAR_NEHAHRA); cvar_t nehx13 ("nehx13", "0", CVAR_NEHAHRA); 
cvar_t nehx14 ("nehx14", "0", CVAR_NEHAHRA); cvar_t nehx15 ("nehx15", "0", CVAR_NEHAHRA);
cvar_t nehx16 ("nehx16", "0", CVAR_NEHAHRA); cvar_t nehx17 ("nehx17", "0", CVAR_NEHAHRA); 
cvar_t nehx18 ("nehx18", "0", CVAR_NEHAHRA); cvar_t nehx19 ("nehx19", "0", CVAR_NEHAHRA);

/*
============
Cvar_FindVar

used only for cases where the full completion list can't be relied on to be up yet; use Cmd_FindCvar otherwise
============
*/
cvar_t *Cvar_FindVar (char *var_name)
{
	cvar_t	*var;

	for (var = cvar_vars; var; var = var->next)
	{
		// skip nehahra cvars
		if (!nehahra && (var->usage & CVAR_NEHAHRA)) continue;

		if (!stricmp (var_name, var->name))
		{
			return var;
		}
	}

	return NULL;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue (char *var_name)
{
	cvar_t	*var;

	var = Cvar_FindVar (var_name);

	if (!var) return 0;

	return atof (var->string);
}


/*
============
Cvar_VariableString
============
*/
char *Cvar_VariableString (char *var_name)
{
	cvar_t *var;
	
	var = Cvar_FindVar (var_name);
	if (!var)
		return cvar_null_string;
	return var->string;
}


bool Cvar_SetPrevalidate (cvar_t *var)
{
	if (!var)
	{
		// there is an error in C code if this happens
		Con_Printf ("Cvar_Set: variable %s not found\n", var->name);
		return false;
	}

	// reject set attempt
	if (var->usage & CVAR_READONLY)
	{
		Con_Printf ("Cvar_Set: var->usage & CVAR_READONLY\n");
		return false;
	}

	return true;
}


void Cvar_SetBroadcast (cvar_t *var)
{
	if (!(var->usage & CVAR_SERVER)) return;
	if (!sv.active) return;

	// add the name of the person who changed it to the message
	SV_BroadcastPrintf ("\"%s\" was changed to \"%s\" by \"%s\"\n", var->name, var->string, cl_name.string);
}


/*
============
Cvar_Set
============
*/
void Cvar_Set (cvar_t *var, char *value)
{
	if (!Cvar_SetPrevalidate (var)) return;

	if (strcmp (var->string, value))
	{
		Zone_Free (var->string);
		var->string = (char *) Zone_Alloc (strlen (value) + 1);
		strcpy (var->string, value);
		var->value = atof (var->string);
		var->integer = (int) var->value;

		Cvar_SetBroadcast (var);
	}
}


void Cvar_Set (cvar_t *var, float value)
{
	if (!Cvar_SetPrevalidate (var)) return;

	if (var->value != value)
	{
		// store back to the cvar
		var->value = value;
		var->integer = (int) var->value;

		// copy out the value to a temp buffer
		char valbuf[32];
		_snprintf (valbuf, 32, "%g", var->value);

		Zone_Free (var->string);
		var->string = (char *) Zone_Alloc (strlen (valbuf) + 1);

		strcpy (var->string, valbuf);

		Cvar_SetBroadcast (var);
	}
}


void Cvar_Set (char *var_name, float value)
{
	Cvar_Set (Cmd_FindCvar (var_name), value);
}


void Cvar_Set (char *var_name, char *value)
{
	Cvar_Set (Cmd_FindCvar (var_name), value);
}


/*
============
Cvar_Register

Adds a freestanding variable to the variable list.
============
*/
static void Cvar_Register (cvar_t *variable)
{
	// these should never go through here but let's just be certain
	// (edit - actually it does - recursively - when setting up shadows; see below)
	if (variable->usage & CVAR_DUMMY) return;

	// hack to prevent double-definition of nehahra cvars
	bool oldneh = nehahra;
	nehahra = true;

	// first check to see if it has already been defined
	if (Cvar_FindVar (variable->name)) return;

	// unhack (note: this is not actually necessary as the game isn't up yet, but for the
	// sake of correctness we do it anyway)
	nehahra = oldneh;

	// check for overlap with a command
	if (Cmd_Exists (variable->name)) return;

	// store the value off
	variable->value = atof (variable->string);
	variable->integer = (int) variable->value;

	// link the variable in
	variable->next = cvar_vars;
	cvar_vars = variable;
}


/*
============
Cvar_WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables (FILE *f)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
		if (var->usage & CVAR_ARCHIVE)
			fprintf (f, "%s \"%s\"\n", var->name, var->string);
}


cvar_t::cvar_t (char *cvarname, char *initialval, int useflags)
{
	// alloc space
	this->name = (char *) Zone_Alloc (strlen (cvarname) + 1);
	this->string = (char *) Zone_Alloc (strlen (initialval) + 1);

	// copy in the data
	strcpy (this->name, cvarname);
	strcpy (this->string, initialval);
	this->usage = useflags;

	// self-register the cvar at construction time
	Cvar_Register (this);
}


cvar_t::cvar_t (char *cvarname, float initialval, int useflags)
{
	// alloc space
	this->name = (char *) Zone_Alloc (strlen (cvarname) + 1);

	// copy out the value to a temp buffer
	char valbuf[32];
	_snprintf (valbuf, 32, "%g", initialval);

	// alloc space for the string
	this->string = (char *) Zone_Alloc (strlen (valbuf) + 1);

	// copy in the data
	strcpy (this->name, cvarname);
	strcpy (this->string, valbuf);
	this->usage = useflags;

	// self-register the cvar at construction time
	Cvar_Register (this);
}


cvar_t::cvar_t (void)
{
	// dummy cvar for temp usage; not registered
	this->name = (char *) Zone_Alloc (2);
	this->string = (char *) Zone_Alloc (2);

	this->name[0] = 0;
	this->string[0] = 0;
	this->value = 0;
	this->usage = CVAR_DUMMY;
	this->integer = 0;
	this->next = NULL;
}


cvar_t::~cvar_t (void)
{
	// protect the zone from overflowing if cvars are declared in function scope
//	Zone_Free (this->name);
//	Zone_Free (this->string);
}


