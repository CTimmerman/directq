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
// r_light.c

#include "quakedef.h"
#include "d3d_quake.h"

// never create lightmaps smaller than this
#define MINIMUM_LIGHTMAP_SIZE	64

// main lightmap object
CD3DLightmap *d3d_Lightmaps = NULL;

cvar_t r_lerplightstyle ("r_lerplightstyle", "1", CVAR_ARCHIVE);
cvar_t r_coloredlight ("r_coloredlight", "1", CVAR_ARCHIVE);
cvar_t r_overbright ("gl_overbright", "1", CVAR_ARCHIVE);


// unbounded
unsigned int **d3d_blocklights;

void D3D_CreateBlockLights (void)
{
	extern int MaxExtents[];

	// get max blocklight size
	int blsize = ((MaxExtents[0] >> 4) + 1) * ((MaxExtents[1] >> 4) + 1);

	// rgb array
	d3d_blocklights = (unsigned int **) Pool_Alloc (POOL_MAP, sizeof (unsigned int *) * 3);

	// each component
	d3d_blocklights[0] = (unsigned int *) Pool_Alloc (POOL_MAP, sizeof (unsigned int) * blsize);
	d3d_blocklights[1] = (unsigned int *) Pool_Alloc (POOL_MAP, sizeof (unsigned int) * blsize);
	d3d_blocklights[2] = (unsigned int *) Pool_Alloc (POOL_MAP, sizeof (unsigned int) * blsize);
}


/*
===============
D3D_AddDynamicLights
===============
*/
void D3D_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	mtexinfo_t	*tex = surf->texinfo;

	if (!r_dynamic.value) return;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		// light is dead
		if (cl_dlights[lnum].die < cl.time) continue;

		// not hit by this light
		if (!(surf->dlightbits[lnum >> 5] & (1 << (lnum & 31)))) continue;

		rad = cl_dlights[lnum].radius;

		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;

		rad -= fabs(dist);
		minlight = cl_dlights[lnum].minlight;

		if (rad < minlight) continue;

		minlight = rad - minlight;

		for (i = 0; i < 3; i++)
			impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i] * dist;

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];
		
		for (t = 0; t < surf->tmax; t++)
		{
			td = local[1] - t * 16;
			int tsmax = (int) surf->smax * t;

			if (td < 0) td = -td;

			for (s = 0; s < surf->smax; s++, tsmax++)
			{
				sd = local[0] - s * 16;

				if (sd < 0) sd = -sd;

				if (sd > td)
					dist = sd + (td >> 1);
				else
					dist = td + (sd >> 1);

				if (dist < minlight)
				{
					d3d_blocklights[0][tsmax] += (rad - dist) * cl_dlights[lnum].rgb[0] * r_dynamic.value;
					d3d_blocklights[1][tsmax] += (rad - dist) * cl_dlights[lnum].rgb[1] * r_dynamic.value;
					d3d_blocklights[2][tsmax] += (rad - dist) * cl_dlights[lnum].rgb[2] * r_dynamic.value;
				}
			}
		}
	}
}


void D3D_FillLightmap (msurface_t *surf, byte *dest, int stride)
{
	int t;
	int shift = 7 + r_overbright.integer;

	// retrieve pointers to the blocklights
	unsigned int *blr = d3d_blocklights[0];
	unsigned int *blg = d3d_blocklights[1];
	unsigned int *blb = d3d_blocklights[2];

	for (int i = 0; i < surf->tmax; i++, dest += stride)
	{
		for (int j = 0; j < surf->smax; j++)
		{
			// note - this is the opposite to what you think it should be.  ARGB format = upload in BGRA
			t = *blr++ >> shift;
			dest[2] = vid.lightmap[BYTE_CLAMP (t)];

			t = *blg++ >> shift;
			dest[1] = vid.lightmap[BYTE_CLAMP (t)];

			t = *blb++ >> shift;
			dest[0] = vid.lightmap[BYTE_CLAMP (t)];

			// no need for dest[3] cos i switched it to xrgb
			dest += 4;
		}
	}
}


void D3D_BuildLightmap (msurface_t *surf)
{
	// cache the light
	surf->cached_dlight = (surf->dlightframe == d3d_RenderDef.framecount);

	// cache these at the levels they were created at
	surf->overbright = r_overbright.integer;
	surf->fullbright = r_fullbright.integer;

	int size = surf->smax * surf->tmax;
	byte *lightmap = surf->samples;

	// set to full bright if no light data
	if (r_fullbright.integer || !cl.worldbrush->lightdata)
	{
		for (int i = 0; i < size; i++)
		{
			// ensure correct scale for overbrighting
			d3d_blocklights[0][i] = 255 * (256 >> r_overbright.integer);
			d3d_blocklights[1][i] = 255 * (256 >> r_overbright.integer);
			d3d_blocklights[2][i] = 255 * (256 >> r_overbright.integer);
		}
	}
	else
	{
		// clear to no light
		for (int i = 0; i < size; i++)
		{
			d3d_blocklights[0][i] = 0;
			d3d_blocklights[1][i] = 0;
			d3d_blocklights[2][i] = 0;
		}

		// add all the dynamic lights first
		if (surf->dlightframe == d3d_RenderDef.framecount)
			D3D_AddDynamicLights (surf);

		// add all the lightmaps
		if (lightmap)
		{
			for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
			{
				unsigned int scale = d_lightstylevalue[surf->styles[maps]];
				surf->cached_light[maps] = scale;

				for (int i = 0; i < size; i++)
				{
					d3d_blocklights[0][i] += *lightmap++ * scale;
					d3d_blocklights[1][i] += *lightmap++ * scale;
					d3d_blocklights[2][i] += *lightmap++ * scale;
				}
			}
		}
	}
}


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (void)
{
	// made this cvar-controllable!
	if (r_lerplightstyle.value)
	{
		// interpolated light animations
		int			j, k;
		float		l;
		int			flight;
		int			clight;
		float		lerpfrac;
		float		backlerp;

		// light animations
		// 'm' is normal light, 'a' is no light, 'z' is double bright
		flight = (int) floor (cl.time * 10);
		clight = (int) ceil (cl.time * 10);
		lerpfrac = (cl.time * 10) - flight;
		backlerp = 1.0f - lerpfrac;

		for (j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				d_lightstylevalue[j] = 256;
				continue;
			}
			else if (cl_lightstyle[j].length == 1)
			{
				// single length style so don't bother interpolating
				d_lightstylevalue[j] = 22 * (cl_lightstyle[j].map[0] - 'a');
				continue;
			}

			// interpolate animating light
			// frame just gone
			k = flight % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
			l = (float) (k * 22) * backlerp;

			// upcoming frame
			k = clight % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
			l += (float) (k * 22) * lerpfrac;

			d_lightstylevalue[j] = (int) l;
		}
	}
	else
	{
		// old light animation
		int i = (int) (cl.time * 10);

		for (int j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				d_lightstylevalue[j] = 256;
				continue;
			}

			int k = i % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
			k = k * 22;
			d_lightstylevalue[j] = k;
		}
	}
}


void R_ColourDLight (dlight_t *dl, unsigned short r, unsigned short g, unsigned short b)
{
	// leave dlight with white value it had at allocation
	if (!r_coloredlight.value) return;

	dl->rgb[0] = r;
	dl->rgb[1] = g;
	dl->rgb[2] = b;
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int num, mnode_t *node)
{
	mplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	int			sidebit;

	// hey!  no goto!!!
	while (1)
	{
		if (node->contents < 0) return;

		splitplane = node->plane;
		dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

		if (dist > light->radius)
		{
			node = node->children[0];
			continue;
		}

		if (dist < -light->radius)
		{
			node = node->children[1];
			continue;
		}

		break;
	}

	// mark the polygons
	surf = cl.worldbrush->surfaces + node->firstsurface;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		// no lights on these
		if (surf->flags & SURF_DRAWTURB) continue;
		if (surf->flags & SURF_DRAWSKY) continue;

		if (surf->dlightframe != d3d_RenderDef.framecount)
		{
			// first time hit
			surf->dlightbits[0] = surf->dlightbits[1] = surf->dlightbits[2] = surf->dlightbits[3] = 0;
			surf->dlightframe = d3d_RenderDef.framecount;
		}

		// mark the surf for this dlight
		surf->dlightbits[num >> 5] |= 1 << (num & 31);
	}

	if (node->children[0]->contents >= 0) R_MarkLights (light, num, node->children[0]);
	if (node->children[1]->contents >= 0) R_MarkLights (light, num, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (mnode_t *headnode)
{
	if (!r_dynamic.value) return;

	dlight_t *l = cl_dlights;

	for (int i = 0; i < MAX_DLIGHTS; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;

		R_MarkLights (l, i, headnode);
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

mplane_t		*lightplane;
vec3_t			lightspot;

bool R_RecursiveLightPoint (vec3_t color, mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	vec3_t		mid;

loc0:
	// didn't hit anything
	if (node->contents < 0) return false;

	// calculate mid point
	if (node->plane->type < 3)
	{
		front = start[node->plane->type] - node->plane->dist;
		back = end[node->plane->type] - node->plane->dist;
	}
	else
	{
		front = DotProduct(start, node->plane->normal) - node->plane->dist;
		back = DotProduct(end, node->plane->normal) - node->plane->dist;
	}

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
	{
		node = node->children[front < 0];
		goto loc0;
	}

	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	// go down front side
	if (R_RecursiveLightPoint (color, node->children[front < 0], start, mid))
	{
		// hit something
		return true;
	}
	else
	{
		int i, ds, dt;
		msurface_t *surf;

		// check for impact on this node
		VectorCopy (mid, lightspot);
		lightplane = node->plane;

		surf = cl.worldbrush->surfaces + node->firstsurface;

		for (i = 0; i < node->numsurfaces; i++, surf++)
		{
			// no lightmaps
			if (surf->flags & SURF_DRAWTILED) continue;

			ds = (int) ((float) DotProduct (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
			dt = (int) ((float) DotProduct (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

			// out of range
			if (ds < surf->texturemins[0] || dt < surf->texturemins[1]) continue;

			ds -= surf->texturemins[0];
			dt -= surf->texturemins[1];

			// out of range
			if (ds > surf->extents[0] || dt > surf->extents[1]) continue;

			if (surf->samples)
			{
				// LordHavoc: enhanced to interpolate lighting
				byte *lightmap;
				int maps,
					line3,
					dsfrac = ds & 15,
					dtfrac = dt & 15,
					r00 = 0, 
					g00 = 0,
					b00 = 0,
					r01 = 0,
					g01 = 0,
					b01 = 0,
					r10 = 0,
					g10 = 0,
					b10 = 0,
					r11 = 0,
					g11 = 0,
					b11 = 0;
				float scale;

				line3 = ((surf->extents[0] >> 4) + 1) * 3;

				// LordHavoc: *3 for color
				lightmap = surf->samples + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3;

				for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
				{
					scale = (float) d_lightstylevalue[surf->styles[maps]] * 1.0 / 256.0;

					r00 += (float) lightmap[0] * scale;
					g00 += (float) lightmap[1] * scale;
					b00 += (float) lightmap[2] * scale;

					r01 += (float) lightmap[3] * scale;
					g01 += (float) lightmap[4] * scale;
					b01 += (float) lightmap[5] * scale;

					r10 += (float) lightmap[line3 + 0] * scale;
					g10 += (float) lightmap[line3 + 1] * scale;
					b10 += (float) lightmap[line3 + 2] * scale;

					r11 += (float) lightmap[line3 + 3] * scale;
					g11 += (float) lightmap[line3 + 4] * scale;
					b11 += (float) lightmap[line3 + 5] * scale;

					// LordHavoc: *3 for colored lighting
					lightmap += ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1) * 3;
				}

				color[0] += (float) ((int) ((((((((r11 - r10) * dsfrac) >> 4) + r10) - 
					((((r01 - r00) * dsfrac) >> 4) + r00)) * dtfrac) >> 4) + ((((r01 - r00) * dsfrac) >> 4) + r00)));

				color[1] += (float) ((int) ((((((((g11 - g10) * dsfrac) >> 4) + g10) -
					((((g01 - g00) * dsfrac) >> 4) + g00)) * dtfrac) >> 4) + ((((g01 - g00) * dsfrac) >> 4) + g00)));

				color[2] += (float) ((int) ((((((((b11 - b10) * dsfrac) >> 4) + b10) -
					((((b01 - b00) * dsfrac) >> 4) + b00)) * dtfrac) >> 4) + ((((b01 - b00) * dsfrac) >> 4) + b00)));
			}

			// success
			return true;
		}

		// go down back side
		return R_RecursiveLightPoint (color, node->children[front >= 0], mid, end);
	}
}


void R_MinimumLight (float *c, float factor)
{
	float add = factor - (c[0] + c[1] + c[2]);

	if (add > 0.0f)
	{
		c[0] += add / 3.0f;
		c[1] += add / 3.0f;
		c[2] += add / 3.0f;
	}
}


void R_LightPoint (entity_t *e, float *c)
{
	vec3_t		start;
	vec3_t		end;
	float		add;
	vec3_t		dist;

	if (!cl.worldbrush->lightdata)
	{
		// no light data
		if (e->model->type == mod_brush)
			c[0] = c[1] = c[2] = 1.0f;
		else c[0] = c[1] = c[2] = 255.0f;

		return;
	}

	// set start point
	if (e->model->type == mod_brush)
	{
		// pick top-center point as these can have their origins at one bottom corner
		start[0] = ((e->origin[0] + e->model->mins[0]) + (e->origin[0] + e->model->maxs[0])) / 2;
		start[1] = ((e->origin[1] + e->model->mins[1]) + (e->origin[1] + e->model->maxs[1])) / 2;
		start[2] = e->origin[2] + e->model->maxs[2];
	}
	else
	{
		// same as entity origin
		start[0] = e->origin[0];
		start[1] = e->origin[1];
		start[2] = e->origin[2];
	}

	// set end point
	end[0] = e->origin[0];
	end[1] = e->origin[1];
	end[2] = e->origin[2] - 8192;

	// initially nothing
	c[0] = c[1] = c[2] = 0;
	lightplane = NULL;

	// get lighting
	R_RecursiveLightPoint (c, cl.worldbrush->nodes, start, end);

	// rescale to ~classic Q1 range for multiplayer
	// done before dynamic lights so that they don't overbright the model too much
	// also before minimum values to retain them as a true minimum (not double the minimum)
	if (cl.maxclients > 1) VectorScale (c, 2.0f, c);

	// minimum light values
	if (e == &cl.viewent) R_MinimumLight (c, 72);
	if (e->entnum >= 1 && e->entnum <= cl.maxclients) R_MinimumLight (c, 24);
	if (e->model->flags & EF_ROTATE) R_MinimumLight (c, 72);
	if (e->model->type == mod_brush) R_MinimumLight (c, 18);

	// add dynamic lights
	if (r_dynamic.value)
	{
		float dlscale = (1.0f / 255.0f) * r_dynamic.value;

		for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++)
		{
			if (cl_dlights[lnum].die >= cl.time)
			{
				VectorSubtract (e->origin, cl_dlights[lnum].origin, dist);

				add = (cl_dlights[lnum].radius - Length (dist));

				if (add > 0)
				{
					c[0] += (add * cl_dlights[lnum].rgb[0]) * dlscale;
					c[1] += (add * cl_dlights[lnum].rgb[1]) * dlscale;
					c[2] += (add * cl_dlights[lnum].rgb[2]) * dlscale;
				}
			}
		}
	}

	// rescale for values of r_overbright
	if (r_overbright.integer < 1)
		VectorScale (c, 2.0f, c);
	else if (r_overbright.integer > 1)
		VectorScale (c, 0.5f, c);

	// scale for overbrighting
	if (e->model->type != mod_brush) VectorScale (c, 0.5f, c);
}



/*
==============================================================================================================================

				LIGHTMAP CLASS

==============================================================================================================================
*/

void CD3DLightmap::CheckSurfaceForModification (msurface_t *surf)
{
	// no lightmaps
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// check for overbright or fullbright modification
	if (surf->overbright != r_overbright.integer) goto ModifyLightmap;
	if (surf->fullbright != r_fullbright.integer) goto ModifyLightmap;

	// no lightmap modifications
	if (!r_dynamic.value) return;

	// cached lightstyle change
	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		if (d_lightstylevalue[surf->styles[maps]] != surf->cached_light[maps])
			goto ModifyLightmap;

	// dynamic this frame || dynamic previous frame
	if (surf->dlightframe == d3d_RenderDef.framecount || surf->cached_dlight) goto ModifyLightmap;

	// no changes
	return;

ModifyLightmap:;
	// get the region to make dirty for this update
	// notice how D3D uses top as 0, and right and bottom rather than width and height, making this so much cleaner (== less bug-prone)
	if (surf->light_l < this->DirtyRect.left) this->DirtyRect.left = surf->light_l;
	if (surf->light_t < this->DirtyRect.top) this->DirtyRect.top = surf->light_t;
	if (surf->light_r > this->DirtyRect.right) this->DirtyRect.right = surf->light_r;
	if (surf->light_b > this->DirtyRect.bottom) this->DirtyRect.bottom = surf->light_b;

	// lock the full texture rect (only if not already locked!!!)
	if (!this->modified)
	{
		hr = this->d3d_Texture->LockRect (0, &this->LockedRect, NULL, D3DLOCK_NO_DIRTY_UPDATE);
		if (FAILED (hr)) return;
	}

	if (!this->LockedRect.pBits) return;

	// rebuild the lightmap
	D3D_BuildLightmap (surf);

	// and fill it in
	D3D_FillLightmap
	(
		surf,
		((byte *) this->LockedRect.pBits) + ((surf->light_t * this->size + surf->light_l) * 4),
		(this->size * 4) - (surf->smax << 2)
	);

	// flag as modified
	this->modified = true;
}


void CD3DLightmap::CalcLightmapTexCoords (msurface_t *surf, float *v, float *st)
{
	st[0] = DotProduct (v, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
	st[0] -= surf->texturemins[0];
	st[0] += (int) surf->light_l * 16;
	st[0] += 8;
	st[0] /= (float) (this->size * 16);

	st[1] = DotProduct (v, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
	st[1] -= surf->texturemins[1];
	st[1] += (int) surf->light_t * 16;
	st[1] += 8;
	st[1] /= (float) (this->size * 16);
}


void CD3DLightmap::Upload (void)
{
	if (this->next) this->next->Upload ();

	// mark as dirty do that the changes go through to D3D
	if (this->modified)
	{
		this->d3d_Texture->AddDirtyRect (NULL);
		this->d3d_Texture->UnlockRect (0);
	}

	// tell D3D we're going to need this managed resource shortly
	this->d3d_Texture->PreLoad ();

	// dirty region is nothing at the start
	this->DirtyRect.left = this->size;
	this->DirtyRect.right = 0;
	this->DirtyRect.top = this->size;
	this->DirtyRect.bottom = 0;

	// not modified by default
	this->modified = false;
}


void CD3DLightmap::UploadModified (void)
{
	if (this->next) this->next->UploadModified ();

	// invalid dirty rect
	if (this->DirtyRect.left > this->DirtyRect.right) return;
	if (this->DirtyRect.top > this->DirtyRect.top) return;

	// see was it modified
	if (this->modified)
	{
		this->d3d_Texture->AddDirtyRect (&this->DirtyRect);
		this->d3d_Texture->UnlockRect (0);

		// flag as unmodified and clear the dirty region
		this->modified = false;
		this->DirtyRect.left = this->size;
		this->DirtyRect.right = 0;
		this->DirtyRect.top = this->size;
		this->DirtyRect.bottom = 0;
	}
}


CD3DLightmap::CD3DLightmap (msurface_t *surf)
{
	this->d3d_Texture = NULL;

	// link it in
	this->next = d3d_Lightmaps;
	d3d_Lightmaps = this;

	// size needs to be set before creating the texture so that texture creation knows what size to create it at
	// never create < MINIMUM_LIGHTMAP_SIZE
	for (int i = MINIMUM_LIGHTMAP_SIZE; ; i *= 2)
	{
		if (i > d3d_DeviceCaps.MaxTextureWidth || i > d3d_DeviceCaps.MaxTextureHeight)
		{
			// oh crap...
			// note - even on 3DFX this gives a max surf extents of 4080
			Host_Error ("CD3DLightmap::CD3DLightmap: i > d3d_DeviceCaps.MaxTextureWidth || i > d3d_DeviceCaps.MaxTextureHeight");
			return;
		}

		if (i >= surf->maxextent)
		{
			Con_DPrintf ("Creating lightmap at %i\n", i);
			this->size = i;
			break;
		}
	}

	// now attempt to create the lightmap texture
	// (fixme - allow single component here where the source data is also single component)
	hr = d3d_Device->CreateTexture
	(
		this->size,
		this->size,
		1,
		0,
		D3DFMT_X8R8G8B8,
		D3DPOOL_MANAGED,
		&this->d3d_Texture,
		NULL
	);

	if (FAILED (hr))
	{
		// would a host error be enough here as this failure is most likely to be out of memory...
		Sys_Error ("CD3DLightmap::CD3DLightmap: Failed to create a Lightmap Texture");
		return;
	}

	// assign priority
	this->d3d_Texture->SetPriority (16384);

	// set up the data
	this->d3d_Texture->LockRect (0, &this->LockedRect, NULL, D3DLOCK_NO_DIRTY_UPDATE);

	// texture is locked
	this->modified = true;
	this->allocated = new int[this->size]; //(int *) Pool_Alloc (POOL_MAP, sizeof (int) * this->size);

	// clear allocations
	memset (this->allocated, 0, sizeof (int) * this->size);
}


CD3DLightmap::~CD3DLightmap (void)
{
	// cascade destructors
	SAFE_DELETE (this->next);

	// ensure
	if (this->modified)
		this->d3d_Texture->UnlockRect (0);

	// release the texture
	SAFE_RELEASE (this->d3d_Texture);
	delete[] this->allocated;
}


bool CD3DLightmap::AllocBlock (msurface_t *surf)
{
	// potentially suspect construct here...
	do
	{
		// lightmap is too small
		if (this->size < surf->maxextent) break;

		int best = this->size;

		for (int i = 0; i < this->size - surf->smax; i++)
		{
			int j;
			int best2 = 0;

			for (j = 0; j < surf->smax; j++)
			{
				if (this->allocated[i + j] >= best) break;
				if (this->allocated[i + j] > best2) best2 = this->allocated[i + j];
			}

			if (j == surf->smax)
			{
				// this is a valid spot
				surf->light_l = i;
				surf->light_t = best = best2;
			}
		}

		if (best + surf->tmax > this->size)
			break;

		for (int i = 0; i < surf->smax; i++)
			this->allocated[surf->light_l + i] = best + surf->tmax;

		// fill in lightmap right and bottom (these exist just because I'm lazy and don't want to add a few numbers during updates)
		surf->light_r = surf->light_l + surf->smax;
		surf->light_b = surf->light_t + surf->tmax;

		// set lightmap for the surf
		surf->d3d_Lightmap = this;

		// build the lightmap
		D3D_BuildLightmap (surf);

		// and fill it in
		D3D_FillLightmap
		(
			surf,
			((byte *) this->LockedRect.pBits) + ((surf->light_t * this->size + surf->light_l) * 4),
			(this->size * 4) - (surf->smax << 2)
		);

		return true;
	} while (false);

	if (this->next)
		return this->next->AllocBlock (surf);
	else return false;
}


void CD3DLightmap::BindLightmap (int stage)
{
	// binds go here so that we can protect the texture against evil modifications
	D3D_SetTexture (stage, this->d3d_Texture);
}
