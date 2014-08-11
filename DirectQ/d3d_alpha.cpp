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
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "particles.h"


/*
============================================================================================================

		ALPHA SORTING

============================================================================================================
*/

#define MAX_ALPHA_ITEMS		65536

// list of alpha items
typedef struct d3d_alphalist_s
{
	int Type;

	// our qsort for dist is always going to return ints so we just store dist as an int
	int Dist;

	union
	{
		entity_t *Entity;
		particle_type_t *Particle;
		struct d3d_modelsurf_s *ModelSurf;
		void *data;
	};
} d3d_alphalist_t;


#define D3D_ALPHATYPE_ENTITY		1
#define D3D_ALPHATYPE_PARTICLE		2
#define D3D_ALPHATYPE_WATERWARP		3
#define D3D_ALPHATYPE_SURFACE		4
#define D3D_ALPHATYPE_FENCE			5

d3d_alphalist_t **d3d_AlphaList = NULL;
int d3d_NumAlphaList = 0;


void D3DAlpha_NewMap (void)
{
	d3d_AlphaList = (d3d_alphalist_t **) MainHunk->Alloc (MAX_ALPHA_ITEMS * sizeof (d3d_alphalist_t *));
}


float D3DAlpha_GetDist (float *origin)
{
	// no need to sqrt these as all we're concerned about is relative distances
	// (if x < y then sqrt (x) is also < sqrt (y))
	// our qsort for dist is always going to return ints so we just store dist as an int
	return (int)
	(
		(origin[0] - r_origin[0]) * (origin[0] - r_origin[0]) +
		(origin[1] - r_origin[1]) * (origin[1] - r_origin[1]) +
		(origin[2] - r_origin[2]) * (origin[2] - r_origin[2])
	);
}


void D3DAlpha_AddToList (int type, void *data, float dist)
{
	if (d3d_NumAlphaList == MAX_ALPHA_ITEMS) return;
	if (!d3d_AlphaList[d3d_NumAlphaList]) d3d_AlphaList[d3d_NumAlphaList] = (d3d_alphalist_t *) MainHunk->Alloc (sizeof (d3d_alphalist_t));

	d3d_AlphaList[d3d_NumAlphaList]->Type = type;
	d3d_AlphaList[d3d_NumAlphaList]->data = data;
	d3d_AlphaList[d3d_NumAlphaList]->Dist = dist;

	d3d_NumAlphaList++;
}


void D3DAlpha_AddToList (entity_t *ent)
{
	D3DAlpha_AddToList (D3D_ALPHATYPE_ENTITY, ent, D3DAlpha_GetDist (ent->origin));
}


void D3DAlpha_AddToList (d3d_modelsurf_t *modelsurf)
{
	// we only support turb surfaces for now
	if (modelsurf->surf->flags & SURF_DRAWTURB)
		D3DAlpha_AddToList (D3D_ALPHATYPE_WATERWARP, modelsurf, D3DAlpha_GetDist (modelsurf->surf->midpoint));
	else if (modelsurf->surf->flags & SURF_DRAWFENCE)
		D3DAlpha_AddToList (D3D_ALPHATYPE_FENCE, modelsurf, D3DAlpha_GetDist (modelsurf->surf->midpoint));
	else D3DAlpha_AddToList (D3D_ALPHATYPE_SURFACE, modelsurf, D3DAlpha_GetDist (modelsurf->surf->midpoint));
}


void D3DAlpha_AddToList (particle_type_t *particle)
{
	D3DAlpha_AddToList (D3D_ALPHATYPE_PARTICLE, particle, D3DAlpha_GetDist (particle->spawnorg));
}


int D3DAlpha_SortFunc (const void *a, const void *b)
{
	d3d_alphalist_t *al1 = * (d3d_alphalist_t **) a;
	d3d_alphalist_t *al2 = * (d3d_alphalist_t **) b;

	// back to front ordering
	return (al2->Dist - al1->Dist);
}


void D3DAlias_SetupAliasModel (entity_t *e);
void D3DAlias_DrawAliasBatch (entity_t **ents, int numents);
void D3D_SetupSpriteModel (entity_t *ent);
void R_AddParticleTypeToRender (particle_type_t *pt);

void D3DWarp_SetupTurbState (void);
void D3DWarp_TakeDownTurbState (void);
void D3DWarp_DrawSurface (d3d_modelsurf_t *modelsurf);

void D3DPart_BeginParticles (void);
void D3DPart_EndParticles (void);

void D3DSprite_Begin (void);
void D3DSprite_End (void);

void D3DBrush_Begin (void);
void D3DBrush_End (void);
void D3DBrush_EmitSurface (d3d_modelsurf_t *ms);


void D3DAlpha_Cull (void)
{
	D3D_BackfaceCull (D3DCULL_CCW);
}


void D3DAlpha_NoCull (void)
{
	D3D_BackfaceCull (D3DCULL_NONE);
}


void D3DAlpha_StageChange (d3d_alphalist_t *oldone, d3d_alphalist_t *newone)
{
	extern LPDIRECT3DTEXTURE9 cachedparticletexture;

	// fixme - these should be callback functions
	if (oldone)
	{
		switch (oldone->Type)
		{
		case D3D_ALPHATYPE_PARTICLE:
			D3DPart_EndParticles ();
			break;

		case D3D_ALPHATYPE_ENTITY:
			D3DSprite_End ();
			D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			break;

		case D3D_ALPHATYPE_SURFACE:
			D3DBrush_End ();
			break;

		case D3D_ALPHATYPE_FENCE:
			D3DBrush_End ();
			D3D_SetRenderState (D3DRS_ALPHAREF, (DWORD) 0x00000001);
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3DWarp_TakeDownTurbState ();
			D3DAlpha_Cull ();

			break;

		default:
			break;
		}
	}

	if (newone)
	{
		switch (newone->Type)
		{
		case D3D_ALPHATYPE_ENTITY:
			D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
			D3DSprite_Begin ();
			break;

		case D3D_ALPHATYPE_FENCE:
			// hack - solid edges
			D3D_SetRenderState (D3DRS_ALPHAREF, (DWORD) 0x000000aa);
			D3DBrush_Begin ();
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3DWarp_SetupTurbState ();
			D3DAlpha_NoCull ();
			break;

		case D3D_ALPHATYPE_PARTICLE:
			D3DPart_BeginParticles ();
			break;

		case D3D_ALPHATYPE_SURFACE:
			D3DBrush_Begin ();
			break;

		default:
			break;
		}
	}
}


void D3DAlpha_Setup (void)
{
	// enable blending
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
	D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
	D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	// don't write anything with 0 alpha
	D3D_SetRenderState (D3DRS_ALPHAREF, (DWORD) 0x00000001);
	D3D_SetRenderState (D3DRS_ALPHATESTENABLE, TRUE);
	D3D_SetRenderState (D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
}


void D3DAlpha_Takedown (void)
{
	// disable blending (done)
	// the cull type may have been modified going through here so put it back the way it was
	D3D_BackfaceCull (D3DCULL_CCW);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	D3D_SetRenderState (D3DRS_ALPHATESTENABLE, FALSE);
}


void D3DAlpha_RenderList (void)
{
	// nothing to add
	if (!d3d_NumAlphaList) return;

	// sort the alpha list
	if (d3d_NumAlphaList == 1)
		; // no need to sort
	else if (d3d_NumAlphaList == 2)
	{
		// exchange if necessary
		if (d3d_AlphaList[1]->Dist > d3d_AlphaList[0]->Dist)
		{
			// exchange - this was [1] and [2] - how come it never crashed?
			d3d_alphalist_t *Temp = d3d_AlphaList[0];
			d3d_AlphaList[0] = d3d_AlphaList[1];
			d3d_AlphaList[1] = Temp;
		}
	}
	else
	{
		// sort fully
		qsort
		(
			d3d_AlphaList,
			d3d_NumAlphaList,
			sizeof (d3d_alphalist_t *),
			D3DAlpha_SortFunc
		);
	}

	d3d_alphalist_t *previous = NULL;
	extern LPDIRECT3DTEXTURE9 cachedparticletexture;

	D3DAlpha_Setup ();
	cachedparticletexture = NULL;

	// now add all the items in it to the alpha buffer
	for (int i = 0; i < d3d_NumAlphaList; i++)
	{
		// check for state change
		if (previous)
		{
			// fixme - these should be callbacks...
			if (d3d_AlphaList[i]->Type != previous->Type)
				D3DAlpha_StageChange (previous, d3d_AlphaList[i]);
		}
		else D3DAlpha_StageChange (previous, d3d_AlphaList[i]);

		previous = d3d_AlphaList[i];

		switch (d3d_AlphaList[i]->Type)
		{
		case D3D_ALPHATYPE_ENTITY:
			if (d3d_AlphaList[i]->Entity->model->type == mod_alias)
				D3DAlias_DrawAliasBatch (&d3d_AlphaList[i]->Entity, 1);
			else if (d3d_AlphaList[i]->Entity->model->type == mod_sprite)
				D3D_SetupSpriteModel (d3d_AlphaList[i]->Entity);

			break;

		case D3D_ALPHATYPE_PARTICLE:
			R_AddParticleTypeToRender (d3d_AlphaList[i]->Particle);
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3DWarp_DrawSurface (d3d_AlphaList[i]->ModelSurf);
			break;

		case D3D_ALPHATYPE_SURFACE:
		case D3D_ALPHATYPE_FENCE:
			D3DBrush_EmitSurface (d3d_AlphaList[i]->ModelSurf);
			break;

		default:
			// nothing to add
			break;
		}
	}

	// take down the final state used (in case it was a HLSL state)
	// (should this be previous???)
	D3DAlpha_StageChange (d3d_AlphaList[d3d_NumAlphaList - 1], NULL);

	D3DAlpha_Takedown ();

	// reset alpha list
	// Con_Printf ("%i items in alpha list\n", d3d_NumAlphaList);
	d3d_NumAlphaList = 0;
}

