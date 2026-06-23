/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

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
// r_main.c

#include "quakedef.h"

extern cvar_t scr_fov_viewmodel;
extern cvar_t scr_fov;
extern float AdaptFovx (float fov_x, float width, float height);
extern float CalcFovy (float fov_x, float width, float height);
extern void GL_SetFrustum (float fovx, float fovy);

// viewmodel FOV: gun/hands drawn at a fixed FOV (not the world FOV) so they never morph,
// zooming in on ADS. r_viewmodel_fov sets the hipfire FOV (default 70); ADS zoom is automatic.
static float R_ViewmodelFovx (void)
{
	static float cur = 0.0f;
	float base = ((scr_fov_viewmodel.value > 0.0f) ? scr_fov_viewmodel.value : 70.0f) - 15.0f;
	float target = (cl.stats[STAT_ZOOM] == 1) ? base * 0.66f : base;	// ADS -> tighter = bigger sights
	float f;
	if (cur <= 0.0f) { cur = target; }
	else { f = (float)host_frametime * 12.0f; if (f > 1.0f) f = 1.0f; cur += (target - cur) * f; }
	{	// floor at 48 so a deep scope zoom can't balloon the gun (or Sys_Error "Bad fov")
		float out = cur;
		if (out < 48.0f) out = 48.0f;
		if (out > 170.0f) out = 170.0f;
		return AdaptFovx (out, vid.width, vid.height);
	}
}

qboolean	r_cache_thrash;		// compatability

vec3_t		modelorg, r_entorigin;
entity_t	*currententity;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

//johnfitz -- rendering statistics
int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;
float rs_megatexels;

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float r_fovx, r_fovy; //johnfitz -- rendering fov may be different becuase of r_waterwarp and r_stereo

//
// screen size info
//
refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


cvar_t	r_norefresh = {"r_norefresh","0",CVAR_NONE};
cvar_t	r_drawentities = {"r_drawentities","1",CVAR_NONE};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1",CVAR_NONE};
cvar_t	r_speeds = {"r_speeds","0",CVAR_NONE};
cvar_t	r_pos = {"r_pos","0",CVAR_NONE};
cvar_t	r_fullbright = {"r_fullbright","0",CVAR_NONE};
cvar_t	r_lightmap = {"r_lightmap","0",CVAR_NONE};
cvar_t	r_shadows = {"r_shadows","1",CVAR_ARCHIVE};	// flattened silhouette shadow (GLSL path) under alias models
cvar_t	r_wateralpha = {"r_wateralpha","1",CVAR_ARCHIVE};
cvar_t	r_dynamic = {"r_dynamic","1",CVAR_ARCHIVE};
cvar_t	r_novis = {"r_novis","0",CVAR_ARCHIVE};

cvar_t	gl_finish = {"gl_finish","0",CVAR_NONE};
cvar_t	gl_clear = {"gl_clear","1",CVAR_NONE};
cvar_t	gl_cull = {"gl_cull","1",CVAR_NONE};
cvar_t	gl_smoothmodels = {"gl_smoothmodels","1",CVAR_NONE};
cvar_t	gl_affinemodels = {"gl_affinemodels","0",CVAR_NONE};
cvar_t	gl_polyblend = {"gl_polyblend","1",CVAR_NONE};
cvar_t	gl_flashblend = {"gl_flashblend","0",CVAR_ARCHIVE};
cvar_t	gl_playermip = {"gl_playermip","0",CVAR_NONE};
cvar_t	gl_nocolors = {"gl_nocolors","0",CVAR_NONE};

//johnfitz -- new cvars
cvar_t	r_stereo = {"r_stereo","0",CVAR_NONE};
cvar_t	r_stereodepth = {"r_stereodepth","128",CVAR_NONE};
cvar_t	r_clearcolor = {"r_clearcolor","2",CVAR_ARCHIVE};
cvar_t	r_drawflat = {"r_drawflat","0",CVAR_NONE};
cvar_t	r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t	gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE};
cvar_t	gl_farclip = {"gl_farclip", "16384", CVAR_ARCHIVE};
cvar_t	gl_overbright = {"gl_overbright", "1", CVAR_ARCHIVE};
cvar_t	gl_overbright_models = {"gl_overbright_models", "1", CVAR_ARCHIVE};
cvar_t	r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t	r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t	r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t	r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t	r_lerpmodels = {"r_lerpmodels", "1", CVAR_NONE};
cvar_t	r_lerpmove = {"r_lerpmove", "1", CVAR_NONE};
cvar_t	r_nolerp_list = {"r_nolerp_list", "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl", CVAR_NONE};
cvar_t	r_noshadow_list = {"r_noshadow_list", "progs/flame2.mdl,progs/flame.mdl,progs/bolt1.mdl,models/misc/bolt2.mdl,progs/bolt3.mdl,progs/laser.mdl,models/props/rebar.mdl", CVAR_NONE};

extern cvar_t	r_vfog;
//johnfitz

cvar_t	gl_zfix = {"gl_zfix", "0", CVAR_NONE}; // QuakeSpasm z-fighting fix

cvar_t	r_lavaalpha = {"r_lavaalpha","0",CVAR_NONE};
cvar_t	r_telealpha = {"r_telealpha","0",CVAR_NONE};
cvar_t	r_slimealpha = {"r_slimealpha","0",CVAR_NONE};

float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;

qboolean r_drawflat_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe, r_drawworld_cheatsafe; //johnfitz

cvar_t	r_scale = {"r_scale", "1", CVAR_ARCHIVE};
cvar_t	gl_motionblur = {"gl_motionblur", "0", CVAR_ARCHIVE};	// master motion-blur toggle/strength (0 = off)
cvar_t	r_mblur_object = {"r_mblur_object", "2.0", CVAR_ARCHIVE};	// generic alias-prop shutter (non-zombie world models)
cvar_t	r_mblur_zombie = {"r_mblur_zombie", "4.0", CVAR_ARCHIVE};	// zombie shutter (live-tunable; 5 was a touch intense on close ones)
cvar_t	r_mblur_viewmodel = {"r_mblur_viewmodel", "0.45", CVAR_ARCHIVE};	// gun/hands shutter (calmer -> no bright halo when bobbing/strafing)
cvar_t	r_mblur_world  = {"r_mblur_world",  "0.01", CVAR_ARCHIVE};	// world/camera blur amount (subtle room blur on movement)
cvar_t	r_mblur_rain   = {"r_mblur_rain",   "1.5", CVAR_ARCHIVE};	// rain streak shutter (Nacht)
cvar_t	r_mblur_occlude = {"r_mblur_occlude", "0", CVAR_ARCHIVE};	// object world-occlusion mask: 0 = off (reliable), 1 = experimental (can kill object blur)
cvar_t	r_mblur_scenedepth = {"r_mblur_scenedepth", "1", CVAR_ARCHIVE};	// occlusion via SSAA depth texture (needs SSAA on)
cvar_t	r_mblur_debug  = {"r_mblur_debug",  "0", CVAR_NONE};	// 1 = show velocity buffer, 2 = same w/ occlusion mask off
cvar_t	r_ssaa = {"r_ssaa", "1", CVAR_ARCHIVE};	// supersampling AA factor: 1 = off, 2/3/4 = NxN
int	r_ssaa_active = 1;	// this frame's SSAA scale (read by R_SetupGL's viewport)
cvar_t	r_bloom = {"r_bloom", "0", CVAR_ARCHIVE};	// soft glow on bright areas
cvar_t	r_vignette = {"r_vignette", "0", CVAR_ARCHIVE};	// subtle darkened screen corners
cvar_t	r_cg_contrast = {"r_cg_contrast", "1.0", CVAR_ARCHIVE};
cvar_t	r_cg_saturation = {"r_cg_saturation", "1.0", CVAR_ARCHIVE};
cvar_t	r_tonemap = {"r_tonemap", "0", CVAR_ARCHIVE};		// tone/grade preset: 0=off 1=Clean 2=Cinematic 3=Cold 4=Noir 5=Vintage
cvar_t	r_filmgrain = {"r_filmgrain", "0", CVAR_ARCHIVE};	// old-film grain strength (0=off, ~0.04 light .. 0.12 heavy)
cvar_t	r_ssao = {"r_ssao", "0", CVAR_ARCHIVE};			// screen-space ambient occlusion strength (0=off; needs SSAA for depth)
cvar_t	r_quality = {"r_quality", "0", CVAR_NONE};		// master preset: 0=custom 1=Performance 2=Balanced 3=Quality (applies to the heavy cvars below, which stay archived)
float	r_scene_proj[16], r_scene_mv[16];	// this frame's scene matrices (captured in R_SetupGL, for motion-blur reprojection)
qboolean r_scene_matrices_valid;

// master quality preset -- applies the heavy cvars together (SSAA factor is the dominant cost).
// r_quality itself isn't archived; the cvars it sets are, so the chosen level persists.
void R_QualityPreset_f (cvar_t *var)
{
	int q = (int)var->value;
	if (q <= 0) return;	// 0 = custom: leave individual settings alone
	switch (q)
	{
	case 1:	// Performance
		Cvar_SetValue ("r_ssaa", 1);    Cvar_SetValue ("r_ssao", 0);
		Cvar_SetValue ("r_bloom", 0);   Cvar_SetValue ("gl_motionblur", 0);
		Cvar_SetValue ("r_vignette", 0);
		break;
	case 2:	// Balanced
		Cvar_SetValue ("r_ssaa", 2);    Cvar_SetValue ("r_ssao", 0);
		Cvar_SetValue ("r_bloom", 1);   Cvar_SetValue ("gl_motionblur", 0.35);
		Cvar_SetValue ("r_vignette", 0);
		break;
	default:	// 3+ = Quality
		Cvar_SetValue ("r_ssaa", 4);    Cvar_SetValue ("r_ssao", 2.5);
		Cvar_SetValue ("r_bloom", 1);   Cvar_SetValue ("gl_motionblur", 0.35);
		Cvar_SetValue ("r_vignette", 1);
		break;
	}
}

//==============================================================================
//
// GLSL GAMMA CORRECTION
//
//==============================================================================

static GLuint r_gamma_texture;
static GLuint r_gamma_program;
static int r_gamma_texture_width, r_gamma_texture_height;

// uniforms used in gamma shader
static GLuint gammaLoc;
static GLuint contrastLoc;
static GLuint textureLoc;

// filmic tonemapping and color grading variables
static GLuint r_tg_texture;
static GLuint r_tg_program;
static int r_tg_texture_width, r_tg_texture_height;
static int r_tg_texture_pad_width, r_tg_texture_pad_height;
static GLint r_tg_contrastLoc = -1;
static GLint r_tg_saturationLoc = -1;
static GLint r_tg_modeLoc = -1;
static qboolean r_tg_failed = false;

/*
=============
GLSLGamma_DeleteTexture
=============
*/
void GLSLGamma_DeleteTexture (void)
{
#ifndef VITA
	glDeleteTextures (1, &r_gamma_texture);
	r_gamma_texture = 0;
	r_gamma_program = 0; // deleted in R_DeleteShaders

	if (r_tg_texture)
	{
		glDeleteTextures (1, &r_tg_texture);
		r_tg_texture = 0;
	}
	r_tg_program = 0;
	r_tg_texture_width = 0;
	r_tg_texture_height = 0;
#endif
}

/*
=============
GLSLGamma_CreateShaders
=============
*/
static void GLSLGamma_CreateShaders (void)
{
#ifndef VITA
	const GLchar *vertSource = \
		"#version 110\n"
		"\n"
		"void main(void) {\n"
		"	gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);\n"
		"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"}\n";

	const GLchar *fragSource = \
		"#version 110\n"
		"\n"
		"uniform sampler2D GammaTexture;\n"
		"uniform float GammaValue;\n"
		"uniform float ContrastValue;\n"
		"\n"
		"void main(void) {\n"
		"	  vec4 frag = texture2D(GammaTexture, gl_TexCoord[0].xy);\n"
		"	  frag.rgb = frag.rgb * ContrastValue;\n"
		"	  gl_FragColor = vec4(pow(frag.rgb, vec3(GammaValue)), 1.0);\n"
		"}\n";

	if (!gl_glsl_gamma_able)
		return;

	r_gamma_program = GL_CreateProgram (vertSource, fragSource, 0, NULL);

// get uniform locations
	gammaLoc = GL_GetUniformLocation (&r_gamma_program, "GammaValue");
	contrastLoc = GL_GetUniformLocation (&r_gamma_program, "ContrastValue");
	textureLoc = GL_GetUniformLocation (&r_gamma_program, "GammaTexture");
#endif
}

/*
=============
GLSLGamma_GammaCorrect
=============
*/
void GLSLGamma_GammaCorrect (void)
{
#ifndef VITA
	float smax, tmax;

	if (!gl_glsl_gamma_able)
		return;

	if (vid_gamma.value == 1 && vid_contrast.value == 1)
		return;

// create render-to-texture texture if needed
	if (!r_gamma_texture)
	{
		glGenTextures (1, &r_gamma_texture);
		glBindTexture (GL_TEXTURE_2D, r_gamma_texture);

		r_gamma_texture_width = glwidth;
		r_gamma_texture_height = glheight;

		if (!gl_texture_NPOT)
		{
			r_gamma_texture_width = TexMgr_Pad(r_gamma_texture_width);
			r_gamma_texture_height = TexMgr_Pad(r_gamma_texture_height);
		}
	
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, r_gamma_texture_width, r_gamma_texture_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

// create shader if needed
	if (!r_gamma_program)
	{
		GLSLGamma_CreateShaders ();
		if (!r_gamma_program)
		{
			Sys_Error("GLSLGamma_CreateShaders failed");
		}
	}
	
// copy the framebuffer to the texture
	GL_DisableMultitexture();
	glBindTexture (GL_TEXTURE_2D, r_gamma_texture);
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, glx, gly, glwidth, glheight);

// draw the texture back to the framebuffer with a fragment shader
	GL_UseProgramFunc (r_gamma_program);
	GL_Uniform1fFunc (gammaLoc, vid_gamma.value);
	GL_Uniform1fFunc (contrastLoc, q_min(2.0, q_max(1.0, vid_contrast.value)));
	GL_Uniform1iFunc (textureLoc, 0); // use texture unit 0

	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);

	glViewport (glx, gly, glwidth, glheight);

	smax = glwidth/(float)r_gamma_texture_width;
	tmax = glheight/(float)r_gamma_texture_height;

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);
	glVertex2f (-1, -1);
	glTexCoord2f (smax, 0);
	glVertex2f (1, -1);
	glTexCoord2f (smax, tmax);
	glVertex2f (1, 1);
	glTexCoord2f (0, tmax);
	glVertex2f (-1, 1);
	glEnd ();
	
	GL_UseProgramFunc (0);
	
// clear cached binding
	GL_ClearBindings ();
#endif
}

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int i;
	mplane_t *p;
	for (i = 0;i < 4;i++)
	{
		p = frustum + i;
		switch(p->signbits)
		{
		default:
		case 0:
			if (p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2] < p->dist)
				return true;
			break;
		case 1:
			if (p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2] < p->dist)
				return true;
			break;
		case 2:
			if (p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2] < p->dist)
				return true;
			break;
		case 3:
			if (p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2] < p->dist)
				return true;
			break;
		case 4:
			if (p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2] < p->dist)
				return true;
			break;
		case 5:
			if (p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2] < p->dist)
				return true;
			break;
		case 6:
			if (p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2] < p->dist)
				return true;
			break;
		case 7:
			if (p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2] < p->dist)
				return true;
			break;
		}
	}
	return false;
}
/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;

	if (e->angles[0] || e->angles[2]) //pitch or roll
	{
		VectorAdd (e->origin, e->model->rmins, mins);
		VectorAdd (e->origin, e->model->rmaxs, maxs);
	}
	else if (e->angles[1]) //yaw
	{
		VectorAdd (e->origin, e->model->ymins, mins);
		VectorAdd (e->origin, e->model->ymaxs, maxs);
	}
	else //no rotation
	{
		VectorAdd (e->origin, e->model->mins, mins);
		VectorAdd (e->origin, e->model->maxs, maxs);
	}

	return R_CullBox (mins, maxs);
}

/*
===============
R_RotateForEntity -- johnfitz -- modified to take origin and angles instead of pointer to entity
===============
*/
void R_RotateForEntity (vec3_t origin, vec3_t angles, unsigned char scale)
{
	glTranslatef (origin[0],  origin[1],  origin[2]);
	glRotatef (angles[1],  0, 0, 1);
	glRotatef (-angles[0],  0, 1, 0);
	glRotatef (angles[2],  1, 0, 0);

	if (scale != ENTSCALE_DEFAULT) {
		float scalefactor = ENTSCALE_DECODE(scale);
		glScalef(scalefactor, scalefactor, scalefactor);
	}
}

/*
=============
GL_PolygonOffset -- johnfitz

negative offset moves polygon closer to camera
=============
*/
void GL_PolygonOffset (int offset)
{
	if (offset > 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(1, offset);
	}
	else if (offset < 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(-1, offset);
	}
	else
	{
		glDisable (GL_POLYGON_OFFSET_FILL);
		glDisable (GL_POLYGON_OFFSET_LINE);
	}
}

//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
#define DEG2RAD( a ) ( (a) * M_PI_DIV_180 )
void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float scale_forward, scale_side;

	scale_forward = cos( DEG2RAD( angle ) );
	scale_side = sin( DEG2RAD( angle ) );

	out[0] = scale_forward*forward[0] + scale_side*side[0];
	out[1] = scale_forward*forward[1] + scale_side*side[1];
	out[2] = scale_forward*forward[2] + scale_side*side[2];
}

/*
===============
R_SetFrustum -- johnfitz -- rewritten
===============
*/
void R_SetFrustum (float fovx, float fovy)
{
	int		i;

	if (r_stereo.value)
		fovx += 10; //silly hack so that polygons don't drop out becuase of stereo skew

	TurnVector(frustum[0].normal, vpn, vright, fovx/2 - 90); //left plane
	TurnVector(frustum[1].normal, vpn, vright, 90 - fovx/2); //right plane
	TurnVector(frustum[2].normal, vpn, vup, 90 - fovy/2); //bottom plane
	TurnVector(frustum[3].normal, vpn, vup, fovy/2 - 90); //top plane

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal); //FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
=============
GL_SetFrustum -- johnfitz -- written to replace MYgluPerspective
=============
*/
#define NEARCLIP 4
float frustum_skew = 0.0; //used by r_stereo
void GL_SetFrustum(float fovx, float fovy)
{
	float xmax, ymax;
	xmax = NEARCLIP * tan( fovx * M_PI / 360.0 );
	ymax = NEARCLIP * tan( fovy * M_PI / 360.0 );
	glFrustum(-xmax + frustum_skew, xmax + frustum_skew, -ymax, ymax, NEARCLIP, gl_farclip.value);
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	int scale;

	//johnfitz -- rewrote this section
	glMatrixMode(GL_PROJECTION);
    glLoadIdentity ();
	scale =  CLAMP(1, (int)r_scale.value, 4); // ericw -- see R_ScaleView
	// r_ssaa_active (1 or 2) supersamples the 3D view: render at Nx into the SSAA
	// FBO (R_SSAA_Begin bound it), then downsample in R_SSAA_End. Identity at 1.
	glViewport ((glx + r_refdef.vrect.x) * r_ssaa_active,
				(gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height) * r_ssaa_active,
				(r_refdef.vrect.width / scale) * r_ssaa_active,
				(r_refdef.vrect.height / scale) * r_ssaa_active);
	//johnfitz

    GL_SetFrustum (r_fovx, r_fovy); //johnfitz -- use r_fov* vars

//	glCullFace(GL_BACK); //johnfitz -- glquake used CCW with backwards culling -- let's do it right

	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity ();

    glRotatef (-90,  1, 0, 0);	    // put Z going up
    glRotatef (90,  0, 0, 1);	    // put Z going up
    glRotatef (-r_refdef.viewangles[2],  1, 0, 0);
    glRotatef (-r_refdef.viewangles[0],  0, 1, 0);
    glRotatef (-r_refdef.viewangles[1],  0, 0, 1);
    glTranslatef (-r_refdef.vieworg[0],  -r_refdef.vieworg[1],  -r_refdef.vieworg[2]);

	//
	// set drawing parms
	//
	if (gl_cull.value)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_DEPTH_TEST);

	// Motion blur (Stage 1): capture this frame's scene matrices for depth reprojection,
	// here -- before any entity pushes its own modelview.
	glGetFloatv (GL_PROJECTION_MATRIX, r_scene_proj);
	glGetFloatv (GL_MODELVIEW_MATRIX, r_scene_mv);
	r_scene_matrices_valid = true;
}

/*
=============
R_Clear -- johnfitz -- rewritten and gutted
=============
*/
void R_Clear (void)
{
	unsigned int clearbits;

	clearbits = GL_DEPTH_BUFFER_BIT;
	// from mh -- if we get a stencil buffer, we should clear it, even though we don't use it
	if (gl_stencilbits)
		clearbits |= GL_STENCIL_BUFFER_BIT;
	if (gl_clear.value)
		clearbits |= GL_COLOR_BUFFER_BIT;
	glClear (clearbits);
}

/*
===============
R_SetupScene -- johnfitz -- this is the stuff that needs to be done once per eye in stereo mode
===============
*/
void R_SetupScene (void)
{
	R_PushDlights ();
	R_AnimateLight ();
	r_framecount++;
	R_SetupGL ();
}

/*
===============
R_SetupView -- johnfitz -- this is the stuff that needs to be done once per frame, even in stereo mode
===============
*/
void R_SetupView (void)
{
	Fog_SetupFrame (); //johnfitz

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	//johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	if (r_waterwarp.value)
	{
		int contents = Mod_PointInLeaf (r_origin, cl.worldmodel)->contents;
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			//variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.  what a mess!
			r_fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) * (0.97 + sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			r_fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) * (1.03 - sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
		}
	}
	//johnfitz

	R_SetFrustum (r_fovx, r_fovy); //johnfitz -- use r_fov* vars

	R_MarkSurfaces (); //johnfitz -- create texture chains from PVS

	R_CullSurfaces (); //johnfitz -- do after R_SetFrustum and R_MarkSurfaces

	R_UpdateWarpTextures (); //johnfitz -- do this before R_Clear

	R_Clear ();

	//johnfitz -- cheat-protect some draw modes
	r_drawflat_cheatsafe = r_fullbright_cheatsafe = r_lightmap_cheatsafe = false;
	r_drawworld_cheatsafe = true;
	if (cl.maxclients == 1)
	{
		if (!r_drawworld.value) r_drawworld_cheatsafe = false;

		if (r_drawflat.value) r_drawflat_cheatsafe = true;
		else if (r_fullbright.value || !cl.worldmodel->lightdata) r_fullbright_cheatsafe = true;
		else if (r_lightmap.value) r_lightmap_cheatsafe = true;
	}
	//johnfitz
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_DrawEntitiesOnList
=============
*/
//int doZHack;

void R_DrawEntitiesOnList (qboolean alphapass) //johnfitz -- added parameter
{
	int		i;
	//char specChar; //nzp

	if (!r_drawentities.value)
		return;

	//int zHackCount = 0;
	//doZHack = 0;
	char specChar;

	//johnfitz -- sprites are not a special case
	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		//johnfitz -- if alphapass is true, draw only alpha entites this time
		//if alphapass is false, draw only nonalpha entities this time
		if ((ENTALPHA_DECODE(currententity->alpha) < 1 && !alphapass) ||
			(ENTALPHA_DECODE(currententity->alpha) == 1 && alphapass))
			continue;

		//johnfitz -- chasecam
		if (currententity == &cl_entities[cl.viewentity])
			currententity->angles[0] *= 0.3;
		//johnfitz

		if(!(currententity->model))
		{
			// Naiveil -- fixme
			//R_DrawNullModel();
			Con_Printf("Model is null! %s\n", currententity->model->name);
			continue;
		}

		// while forced first-person, hide the local player's own third-person weapon model
		// (a non-player.mdl alias within ~30u) so it doesn't float in the first-person view.
		{
			extern cvar_t chase_firstperson;
			if (chase_firstperson.value
				&& currententity != &cl_entities[cl.viewentity]
				&& currententity->model->type == mod_alias
				&& strcmp(currententity->model->name, "models/player.mdl"))
			{
				vec3_t nzp_d;
				VectorSubtract(currententity->origin,
					cl_entities[cl.viewentity].origin, nzp_d);
				if (nzp_d[0]*nzp_d[0] + nzp_d[1]*nzp_d[1] < 36.0f*36.0f
					&& nzp_d[2]*nzp_d[2] < 48.0f*48.0f)
					continue;
			}
		}


		specChar = currententity->model->name[strlen(currententity->model->name)-5];

		// naievil -- skip this and hack in through qc 
		//if(specChar == '(' || specChar == '^')//skip heads and arms: it's faster to do this than a strcmp...
		//{
		//	continue;
		//}

		/*doZHack = 0; sB needs to fix updateLimb in Quakespasm l8er
		if(specChar == '#')
		{
			if(zHackCount > 5 || ((currententity->z_head != 0) && (currententity->z_larm != 0) && (currententity->z_rarm != 0)))
			{
				doZHack = 1;
			}
			else
			{
				zHackCount ++;//drawing zombie piece by piece.
			}
		}
		
		specChar = currententity->model->name[strlen(currententity->model->name)-5];

		if(specChar == '(' || specChar == '^')//skip heads and arms: it's faster to do this than a strcmp...
		{
			continue;
		}
		doZHack = 0;
		if(specChar == '#')
		{
			if(zHackCount > 5 || ((currententity->z_head != 0) && (currententity->z_larm != 0) && (currententity->z_rarm != 0)))
			{
				doZHack = 1;
			}
			else
			{
				zHackCount ++;//drawing zombie piece by piece.
			}
		}*/

		switch (currententity->model->type)
		{
		case mod_alias:

			// Naievil -- fixme
			// if (qmb_initialized && SetFlameModelState() == -1)
			// 	continue;
			
			if (currententity->model->aliastype == ALIASTYPE_MD2) {
				Sys_Error ("R_DrawMD2Model not yet implemented!!!");
				// Naievil -- fixme
				//R_DrawMD2Model (currententity);
			} else {
				if(specChar == '$')//This is for smooth alpha, draw in the following loop, not this one
				{
					continue;
				}
				R_DrawAliasModel (currententity);
			}
			
			break;
		case mod_md3:
			Sys_Error ("R_DrawQ3Model not yet implemented!!!");
			// Naievil -- fixme
			//R_DrawQ3Model (currententity);
			break;

		case mod_halflife:
			Sys_Error ("R_DrawHLModel not yet implemented!!!\n");
			// Naievil -- fixme
			//R_DrawHLModel (currententity);
			break;

		case mod_brush:
			R_DrawBrushModel (currententity);
			break;

		default:
			break;
		}
		//doZHack = 0;
	}

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];
		
		if(!(currententity->model))
		{
			continue;
		}
		
		specChar = currententity->model->name[strlen(currententity->model->name)-5];

		switch (currententity->model->type)
		{
		case mod_sprite:
		{
			Fog_DisableGFog();
			R_DrawSpriteModel (currententity);
			Fog_EnableGFog();
			break;
		}
		case mod_alias:
			if (currententity->model->aliastype != ALIASTYPE_MD2)
			{
				if(specChar == '$')//mdl model with blended alpha
				{
					R_DrawTransparentAliasModel(currententity);
				}
			}
			break;
		default: break;
		}
	}
}

/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
void R_DrawViewModel (void)
{
	extern cvar_t cam_tour;
	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value || cam_tour.value)
		return;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;

	//johnfitz -- this fixes a crash
	if (currententity->model->type != mod_alias)
		return;
	//johnfitz

	// Change projection matrix to use the viewmodel FOV (auto-tracks world FOV, no stretch)
	{
		float vm_fovx = R_ViewmodelFovx();
		float vm_fovy = CalcFovy(vm_fovx, r_refdef.vrect.width, r_refdef.vrect.height);

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		GL_SetFrustum(vm_fovx, vm_fovy);
		glMatrixMode(GL_MODELVIEW);
	}

	// hack the depth range to prevent view model from poking into walls
	glDepthRange (0, 0.3);
	R_DrawAliasModel (currententity);
	glDepthRange (0, 1);

	// Restore original projection matrix
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}

/*
=============
R_DrawView2Model -- johnfitz -- gutted
=============
*/
void R_DrawView2Model (void)
{
	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value)
		return;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent2;
	if (!currententity->model)
		return;

	//johnfitz -- this fixes a crash
	if (currententity->model->type != mod_alias)
		return;
	//johnfitz

	// Change projection matrix to use the viewmodel FOV (auto-tracks world FOV, no stretch)
	{
		float vm_fovx = R_ViewmodelFovx();
		float vm_fovy = CalcFovy(vm_fovx, r_refdef.vrect.width, r_refdef.vrect.height);

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		GL_SetFrustum(vm_fovx, vm_fovy);
		glMatrixMode(GL_MODELVIEW);
	}

	// hack the depth range to prevent view model from poking into walls
	glDepthRange (0, 0.3);
	R_DrawAliasModel (currententity);
	glDepthRange (0, 1);

	// Restore original projection matrix
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}

/*
================
R_EmitWirePoint -- johnfitz -- draws a wireframe cross shape for point entities
================
*/
void R_EmitWirePoint (vec3_t origin)
{
	int size=8;

	glBegin (GL_LINES);
	glVertex3f (origin[0]-size, origin[1], origin[2]);
	glVertex3f (origin[0]+size, origin[1], origin[2]);
	glVertex3f (origin[0], origin[1]-size, origin[2]);
	glVertex3f (origin[0], origin[1]+size, origin[2]);
	glVertex3f (origin[0], origin[1], origin[2]-size);
	glVertex3f (origin[0], origin[1], origin[2]+size);
	glEnd ();
}

/*
================
R_EmitWireBox -- johnfitz -- draws one axis aligned bounding box
================
*/
void R_EmitWireBox (vec3_t mins, vec3_t maxs)
{
#ifdef VITA
	glBegin (GL_QUADS);
	glVertex3f (mins[0], mins[1], mins[2]);
	glVertex3f (mins[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], mins[1], mins[2]);
	glVertex3f (maxs[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], mins[1], mins[2]);
	glVertex3f (maxs[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], maxs[1], mins[2]);
	glVertex3f (maxs[0], maxs[1], maxs[2]);
	glVertex3f (maxs[0], maxs[1], mins[2]);
	glVertex3f (maxs[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], maxs[1], mins[2]);
	glVertex3f (mins[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], maxs[1], mins[2]);
	glVertex3f (mins[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], mins[1], mins[2]);
	glVertex3f (mins[0], mins[1], maxs[2]);
#else
	glBegin (GL_QUAD_STRIP);
	glVertex3f (mins[0], mins[1], mins[2]);
	glVertex3f (mins[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], mins[1], mins[2]);
	glVertex3f (maxs[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], maxs[1], mins[2]);
	glVertex3f (maxs[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], maxs[1], mins[2]);
	glVertex3f (mins[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], mins[1], mins[2]);
	glVertex3f (mins[0], mins[1], maxs[2]);
#endif
	glEnd ();
}

/*
================
R_ShowBoundingBoxes -- johnfitz

draw bounding boxes -- the server-side boxes, not the renderer cullboxes
================
*/
void R_ShowBoundingBoxes (void)
{
	extern		edict_t *sv_player;
	vec3_t		mins,maxs;
	edict_t		*ed;
	int			i;

	if (!r_showbboxes.value || cl.maxclients > 1 || !r_drawentities.value || !sv.active)
		return;

	glDisable (GL_DEPTH_TEST);
	glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset (OFFSET_SHOWTRIS);
	glDisable (GL_TEXTURE_2D);
	glDisable (GL_CULL_FACE);
	glColor3f (1,1,1);

	for (i=0, ed=NEXT_EDICT(sv.edicts) ; i<sv.num_edicts ; i++, ed=NEXT_EDICT(ed))
	{
		if (ed == sv_player)
			continue; //don't draw player's own bbox

//		if (r_showbboxes.value != 2)
//			if (!SV_VisibleToClient (sv_player, ed, sv.worldmodel))
//				continue; //don't draw if not in pvs

		if (ed->v.mins[0] == ed->v.maxs[0] && ed->v.mins[1] == ed->v.maxs[1] && ed->v.mins[2] == ed->v.maxs[2])
		{
			//point entity
			R_EmitWirePoint (ed->v.origin);
		}
		else
		{
			//box entity
			VectorAdd (ed->v.mins, ed->v.origin, mins);
			VectorAdd (ed->v.maxs, ed->v.origin, maxs);
			R_EmitWireBox (mins, maxs);
		}
	}

	glColor3f (1,1,1);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_CULL_FACE);
	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset (OFFSET_NONE);
	glEnable (GL_DEPTH_TEST);

	Sbar_Changed (); //so we don't get dots collecting on the statusbar
}

/*
================
R_ShowTris -- johnfitz
================
*/
void R_ShowTris (void)
{
	extern cvar_t r_particles;
	int i;

	if (r_showtris.value < 1 || r_showtris.value > 2 || cl.maxclients > 1)
		return;

	if (r_showtris.value == 1)
		glDisable (GL_DEPTH_TEST);
	glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset (OFFSET_SHOWTRIS);
	glDisable (GL_TEXTURE_2D);
	glColor3f (1,1,1);
//	glEnable (GL_BLEND);
//	glBlendFunc (GL_ONE, GL_ONE);

	if (r_drawworld.value)
	{
		R_DrawWorld_ShowTris ();
	}

	if (r_drawentities.value)
	{
		for (i=0 ; i<cl_numvisedicts ; i++)
		{
			currententity = cl_visedicts[i];

			if (currententity == &cl_entities[cl.viewentity]) // chasecam
				currententity->angles[0] *= 0.3;

			switch (currententity->model->type)
			{
			case mod_brush:
				R_DrawBrushModel_ShowTris (currententity);
				break;
			case mod_alias:
				R_DrawAliasModel_ShowTris (currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel (currententity);
				break;
			default:
				break;
			}
		}

		// viewmodel
		currententity = &cl.viewent;
		if (r_drawviewmodel.value
			&& !chase_active.value
			&& cl.stats[STAT_HEALTH] > 0
			&& !(cl.items & IT_INVISIBILITY)
			&& currententity->model
			&& currententity->model->type == mod_alias)
		{
			glDepthRange (0, 0.3);
			R_DrawAliasModel_ShowTris (currententity);
			glDepthRange (0, 1);
		}
	}

	if (r_particles.value)
	{
		R_DrawParticles_ShowTris ();
	}

//	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//	glDisable (GL_BLEND);
	glColor3f (1,1,1);
	glEnable (GL_TEXTURE_2D);
	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset (OFFSET_NONE);
	if (r_showtris.value == 1)
		glEnable (GL_DEPTH_TEST);

	Sbar_Changed (); //so we don't get dots collecting on the statusbar
}


//==================================================================================
/*
int SetFlameModelState (void)
{
	if (!r_part_flames.value && !strcmp(currententity->model->name, "progs/flame0.mdl"))
	{
		currententity->model = cl.model_precache[cl_modelindex[mi_flame1]];
	}
	else if (r_part_flames.value)
	{
		vec3_t	liteorg;

		VectorCopy (currententity->origin, liteorg);
		if (currententity->baseline.modelindex == cl_modelindex[mi_flame0])
		{
			if (r_part_flames.value == 2)
			{
				liteorg[2] += 14;
				QMB_Q3TorchFlame (liteorg, 15);
			}
			else
			{
				liteorg[2] += 5.5;

				if(r_flametype.value == 2)
				  QMB_FlameGt (liteorg, 7, 0.8);
				else
				  QMB_TorchFlame(liteorg);
			}
		}
		else if (currententity->baseline.modelindex == cl_modelindex[mi_flame1])
		{
			if (r_part_flames.value == 2)
			{
				liteorg[2] += 14;
				QMB_Q3TorchFlame (liteorg, 15);
			}
			else
			{
				liteorg[2] += 5.5;

				if(r_flametype.value > 1)
				  QMB_FlameGt (liteorg, 7, 0.8);
				else
			      QMB_TorchFlame(liteorg);

			}
			currententity->model = cl.model_precache[cl_modelindex[mi_flame0]];
		}
		else if (currententity->baseline.modelindex == cl_modelindex[mi_flame2])
		{
			if (r_part_flames.value == 2)
            {
				liteorg[2] += 14;
				QMB_Q3TorchFlame (liteorg, 32);
            }
			else
			{
                liteorg[2] -= 1;

				if(r_flametype.value > 1)
				  QMB_FlameGt (liteorg, 12, 1);
				else
			      QMB_BigTorchFlame(liteorg);
			}
			return -1;	//continue;
		}
        else if (!strcmp(currententity->model->name, "progs/wyvflame.mdl"))
		{
			liteorg[2] -= 1;

			if(r_flametype.value > 1)
			  QMB_FlameGt (liteorg, 12, 1);
			else
			  QMB_BigTorchFlame(liteorg);

			return -1;	//continue;
		}
	}

	return 0;
}
*/

/*
================
R_DrawShadows
================
*/
void R_DrawShadows (void)
{
	int i;

	if (!r_shadows.value || !r_drawentities.value || r_drawflat_cheatsafe || r_lightmap_cheatsafe)
		return;

	// Stencil keeps the flattened silhouette from self-overlapping into dark blotches:
	// each floor pixel is shaded once per model. (Skipped if there's no stencil buffer.)
	if (gl_stencilbits)
	{
		glClear(GL_STENCIL_BUFFER_BIT);
		glStencilFunc(GL_EQUAL, 0, ~0);
		glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
		glEnable(GL_STENCIL_TEST);
	}

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		if (currententity->model->type != mod_alias)
			continue;

		if (currententity == &cl.viewent)
			continue;

		GL_DrawAliasShadow (currententity);

		if (gl_stencilbits)	// reset so the next model's silhouette draws once too
			glClear(GL_STENCIL_BUFFER_BIT);
	}

	if (gl_stencilbits)
		glDisable(GL_STENCIL_TEST);
}

/*
================
R_RenderScene
================
*/
void R_RenderScene (void)
{
	R_SetupScene (); //johnfitz -- this does everything that should be done once per call to RenderScene

	Fog_EnableGFog (); //johnfitz

	Sky_DrawSky (); //johnfitz

	R_DrawWorld ();

	S_ExtraUpdate (); // don't let sound get messed up if going slow
		
	R_DrawShadows (); //johnfitz -- render entity shadows

	R_DrawEntitiesOnList (false); //johnfitz -- false means this is the pass for nonalpha entities

	R_DrawWorld_Water (); //johnfitz -- drawn here since they might have transparency

	R_DrawEntitiesOnList (true); //johnfitz -- true means this is the pass for alpha entities

	R_RenderDlights (); //triangle fan dlights -- johnfitz -- moved after water

	Fog_DisableGFog (); //johnfitz

	R_DrawParticles ();

	R_DrawWeather (); //rain

	QMB_DrawParticles();

	R_DrawViewModel (); //johnfitz -- moved here from R_RenderView

	R_DrawView2Model ();

	R_ShowTris (); //johnfitz

	R_ShowBoundingBoxes (); //johnfitz
}

static GLuint r_scaleview_texture;
static int r_scaleview_texture_width, r_scaleview_texture_height;

/*
=============
R_ScaleView_DeleteTexture
=============
*/
void R_ScaleView_DeleteTexture (void)
{
	glDeleteTextures (1, &r_scaleview_texture);
	r_scaleview_texture = 0;
}

/*
================
R_ScaleView

The r_scale cvar allows rendering the 3D view at 1/2, 1/3, or 1/4 resolution.
This function scales the reduced resolution 3D view back up to fill 
r_refdef.vrect. This is for emulating a low-resolution pixellated look,
or possibly as a perforance boost on slow graphics cards.
================
*/
void R_ScaleView (void)
{
#ifndef VITA
	float smax, tmax;
	int scale;
	int srcx, srcy, srcw, srch;

	// copied from R_SetupGL()
	scale = CLAMP(1, (int)r_scale.value, 4);
	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width / scale;
	srch = r_refdef.vrect.height / scale;

	if (scale == 1)
		return;

	// make sure texture unit 0 is selected
	GL_DisableMultitexture ();

	// create (if needed) and bind the render-to-texture texture
	if (!r_scaleview_texture)
	{
		glGenTextures (1, &r_scaleview_texture);

		r_scaleview_texture_width = 0;
		r_scaleview_texture_height = 0;
	}
	glBindTexture (GL_TEXTURE_2D, r_scaleview_texture);

	// resize render-to-texture texture if needed
	if (r_scaleview_texture_width < srcw
		|| r_scaleview_texture_height < srch)
	{
		r_scaleview_texture_width = srcw;
		r_scaleview_texture_height = srch;

		if (!gl_texture_NPOT)
		{
			r_scaleview_texture_width = TexMgr_Pad(r_scaleview_texture_width);
			r_scaleview_texture_height = TexMgr_Pad(r_scaleview_texture_height);
		}

		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, r_scaleview_texture_width, r_scaleview_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	// copy the framebuffer to the texture
	glBindTexture (GL_TEXTURE_2D, r_scaleview_texture);
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);

	// draw the texture back to the framebuffer
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);

	glViewport (srcx, srcy, r_refdef.vrect.width, r_refdef.vrect.height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity ();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity ();

	// correction factor if we lack NPOT textures, normally these are 1.0f
	smax = srcw/(float)r_scaleview_texture_width;
	tmax = srch/(float)r_scaleview_texture_height;

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);
	glVertex2f (-1, -1);
	glTexCoord2f (smax, 0);
	glVertex2f (1, -1);
	glTexCoord2f (smax, tmax);
	glVertex2f (1, 1);
	glTexCoord2f (0, tmax);
	glVertex2f (-1, 1);
	glEnd ();

	// clear cached binding
	GL_ClearBindings ();
#endif
}

/*
================
R_MotionBlur -- PC-style (FTE gl_motionblur) frame-blend motion blur.

After the 3D scene is drawn, blend the previous frame (stored in a texture) over
the current one with alpha = gl_motionblur, then store the blended result for the
next frame. The feedback loop produces the motion trail. Cheap: one copy + one
fullscreen quad, the same ops R_ScaleView already uses (so it's citron-safe).
================
*/
static GLuint r_motionblur_texture;		// screen colour copy
static GLuint r_motionblur_depthtex;		// screen depth copy (Stage 1 reprojection)
static int r_motionblur_tw, r_motionblur_th;	// allocated (possibly pow2) tex size
static int r_motionblur_vw, r_motionblur_vh;	// last view size we tracked
static GLuint mb_program;
static GLint mb_colorLoc=-1, mb_depthLoc=-1, mb_prevVPLoc=-1, mb_strengthLoc=-1, mb_maxstreakLoc=-1, mb_smaxLoc=-1, mb_tmaxLoc=-1;
static qboolean mb_failed;
static float mb_prevVP[16];
static qboolean mb_prevVP_valid;

#ifdef __SWITCH__
void R_VelocityBuffer_Delete (void);	// defined below (Phase 2 per-object velocity buffer)
#endif

void R_MotionBlur_DeleteTexture (void)
{
	if (r_motionblur_texture)  { glDeleteTextures (1, &r_motionblur_texture);  r_motionblur_texture = 0; }
	if (r_motionblur_depthtex) { glDeleteTextures (1, &r_motionblur_depthtex); r_motionblur_depthtex = 0; }
	r_motionblur_vw = r_motionblur_vh = 0;
	mb_program = 0; mb_failed = false; mb_prevVP_valid = false;
#ifdef __SWITCH__
	R_VelocityBuffer_Delete ();	// Phase 2 per-object velocity buffer (forward-declared below)
#endif
}

#ifndef VITA
// column-major 4x4 multiply (GL convention): out = a * b
static void Matrix4_Mult (const float *a, const float *b, float *out)
{
	int c, r, k;
	for (c = 0; c < 4; c++)
		for (r = 0; r < 4; r++)
		{
			float s = 0;
			for (k = 0; k < 4; k++)
				s += a[k*4+r] * b[c*4+k];
			out[c*4+r] = s;
		}
}

static void R_MotionBlur_CreateShader (void)
{
	const GLchar *vert =
		"#version 110\n"
		"varying vec2 tc;\n"
		"void main(void){ gl_Position = vec4(gl_Vertex.xy,0.0,1.0); tc = gl_MultiTexCoord0.xy; }\n";
	// per-pixel camera motion blur: reconstruct each pixel's world pos from depth, reproject
	// through last frame's view-projection, and blur the colour along that screen motion.
	const GLchar *frag =
		"#version 110\n"
		"uniform sampler2D ColorTex;\n"
		"uniform sampler2D DepthTex;\n"
		"uniform mat4 PrevVP;\n"
		"uniform float strength;\n"
		"uniform float maxstreak;\n"
		"uniform float smax;\n"
		"uniform float tmax;\n"
		"varying vec2 tc;\n"
		"void main(void){\n"
		"	vec2 stc = vec2(tc.x * smax, tc.y * tmax);\n"
		"	float depth = texture2D(DepthTex, stc).r;\n"
		"	vec4 ndc = vec4(tc.x*2.0-1.0, tc.y*2.0-1.0, depth*2.0-1.0, 1.0);\n"
		"	vec4 wp = gl_ModelViewProjectionMatrixInverse * ndc;\n"
		"	wp /= wp.w;\n"
		"	vec4 pc = PrevVP * wp;\n"
		"	vec2 prevtc = (pc.xy / pc.w) * 0.5 + 0.5;\n"
		"	vec2 vel = (tc - prevtc) * strength;\n"
		"	float vl = length(vel);\n"
		"	if (vl > maxstreak) vel *= maxstreak / vl;\n"	// streak cap (walk vs sprint set on the CPU)
		"	vec3 sum = vec3(0.0);\n"
		"	for (int i = 0; i < 8; i++) {\n"
		"		float t = (float(i) / 7.0) - 0.5;\n"
		"		vec2 s = tc + vel * t;\n"
		"		sum += texture2D(ColorTex, vec2(s.x * smax, s.y * tmax)).rgb;\n"
		"	}\n"
		"	gl_FragColor = vec4(sum * 0.125, 1.0);\n"
		"}\n";
	if (!gl_glsl_able || !GL_UniformMatrix4fvFunc) { mb_failed = true; return; }
	mb_program = GL_CreateProgram (vert, frag, 0, NULL);
	if (mb_program) {
		mb_colorLoc    = GL_GetUniformLocation (&mb_program, "ColorTex");
		mb_depthLoc    = GL_GetUniformLocation (&mb_program, "DepthTex");
		mb_prevVPLoc   = GL_GetUniformLocation (&mb_program, "PrevVP");
		mb_strengthLoc = GL_GetUniformLocation (&mb_program, "strength");
		mb_maxstreakLoc = GL_GetUniformLocation (&mb_program, "maxstreak");
		mb_smaxLoc     = GL_GetUniformLocation (&mb_program, "smax");
		mb_tmaxLoc     = GL_GetUniformLocation (&mb_program, "tmax");
	} else mb_failed = true;
}
#endif

void R_MotionBlur (void)
{
#ifndef VITA
	float	strength = gl_motionblur.value;
	int	srcx, srcy, srcw, srch;
	float	smax, tmax, curVP[16];

	if (strength <= 0.0f)
		return;
	if (!r_scene_matrices_valid)
		return;
	if (!mb_program && !mb_failed)
		R_MotionBlur_CreateShader ();
	if (!mb_program)
		return;

	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width;
	srch = r_refdef.vrect.height;
	if (srcw <= 0 || srch <= 0)
		return;

	Matrix4_Mult (r_scene_proj, r_scene_mv, curVP);	// world -> clip this frame

	GL_DisableMultitexture ();

	if (!r_motionblur_texture)  { glGenTextures (1, &r_motionblur_texture);  r_motionblur_vw = 0; }
	if (!r_motionblur_depthtex) { glGenTextures (1, &r_motionblur_depthtex); }

	if (r_motionblur_vw != srcw || r_motionblur_vh != srch)
	{
		r_motionblur_vw = srcw; r_motionblur_vh = srch;
		r_motionblur_tw = srcw; r_motionblur_th = srch;
		if (!gl_texture_NPOT) { r_motionblur_tw = TexMgr_Pad (r_motionblur_tw); r_motionblur_th = TexMgr_Pad (r_motionblur_th); }

		glBindTexture (GL_TEXTURE_2D, r_motionblur_texture);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, r_motionblur_tw, r_motionblur_th, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindTexture (GL_TEXTURE_2D, r_motionblur_depthtex);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, r_motionblur_tw, r_motionblur_th, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	// grab this frame's colour + depth
	glBindTexture (GL_TEXTURE_2D, r_motionblur_texture);
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);
	glBindTexture (GL_TEXTURE_2D, r_motionblur_depthtex);
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);

	smax = srcw / (float)r_motionblur_tw;
	tmax = srch / (float)r_motionblur_th;

	if (!mb_prevVP_valid)
	{	// first frame: no history -> seed, draw nothing (zero velocity)
		memcpy (mb_prevVP, curVP, sizeof(curVP));
		mb_prevVP_valid = true;
	}

	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glColor4f (1, 1, 1, 1);

	glViewport (srcx, srcy, srcw, srch);
	// load this frame's scene matrices so gl_ModelViewProjectionMatrixInverse = inverse(curVP)
	glMatrixMode (GL_PROJECTION); glLoadMatrixf (r_scene_proj);
	glMatrixMode (GL_MODELVIEW);  glLoadMatrixf (r_scene_mv);

	// bind colour (unit 0) + depth (unit 1)
	GL_SelectTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, r_motionblur_texture);
	GL_SelectTexture (GL_TEXTURE1);
	glBindTexture (GL_TEXTURE_2D, r_motionblur_depthtex);
	GL_SelectTexture (GL_TEXTURE0);

	GL_UseProgramFunc (mb_program);
	GL_Uniform1iFunc (mb_colorLoc, 0);
	GL_Uniform1iFunc (mb_depthLoc, 1);
	GL_UniformMatrix4fvFunc (mb_prevVPLoc, 1, GL_FALSE, mb_prevVP);
	{
		// Gate the world blur to movement: sprinting gets a bit, walking much less,
		// standing/turning the least. (Per-object blur for models comes separately.)
		float spd = sqrt (cl.velocity[0]*cl.velocity[0] + cl.velocity[1]*cl.velocity[1]);
		float sfactor, maxstreak;
		if (spd > 240.0f)      { sfactor = 0.50f; maxstreak = 0.007f; }	// sprint
		else if (spd > 40.0f)  { sfactor = 0.26f; maxstreak = 0.004f; }	// walk -- much subtler
		else                   { sfactor = 0.22f; maxstreak = 0.0035f; }	// standing -> turn-only, very subtle
		GL_Uniform1fFunc (mb_strengthLoc, strength * sfactor);
		GL_Uniform1fFunc (mb_maxstreakLoc, maxstreak);
	}
	GL_Uniform1fFunc (mb_smaxLoc, smax);
	GL_Uniform1fFunc (mb_tmaxLoc, tmax);

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0); glVertex2f (-1, -1);
	glTexCoord2f (1, 0); glVertex2f ( 1, -1);
	glTexCoord2f (1, 1); glVertex2f ( 1,  1);
	glTexCoord2f (0, 1); glVertex2f (-1,  1);
	glEnd ();

	GL_UseProgramFunc (0);
	glEnable (GL_DEPTH_TEST);

	memcpy (mb_prevVP, curVP, sizeof(curVP));	// this frame's VP -> next frame's "previous"
	GL_ClearBindings ();
#endif
}

/*
================
Per-object motion blur (Phase 2) -- a screen-resolution velocity buffer.

The alias draw records every model it renders (R_MotionBlur_PushAlias) with this
frame's AND last frame's transform + lerp poses. R_VelocityBuffer_Build replays them
into an off-screen RG texture, so each pixel carries that model's own screen-space
motion -- capturing both the model moving (a charging zombie, the gun's swing/recoil)
AND its limb animation (arms/legs), because the previous lerped pose is fed in too.
R_ObjectBlur then smears the (already camera-blurred) screen along that per-pixel
velocity, so ONLY things that actually moved blur -- no global smear, the failure of
every earlier attempt. Object velocity is masked off where the model is occluded by
the world (sampled scene depth), so a zombie behind a wall doesn't smear the wall.

Runs after the camera/world blur (R_MotionBlur) and after the SSAA resolve, so it
composites on the final screen. Each pass uses at most 2 samplers (citron reads a 3rd
as black). __SWITCH__ only (needs FBO entry points, like R_SSAA_*).
================
*/
typedef struct {
	GLuint		vbo, ebo;
	int		numindexes, indexofs, stride;
	const void	*p1n, *p2n, *p1p, *p2p;	// vbo byte offsets: pose verts (now/prev)
	float		blendN, blendP;
	float		mvpN[16], mvpP[16];	// model world->clip (this frame / last)
	int		viewmodel, zombie;	// which shutter cvar to use
} mb_alias_t;

#define MB_MAX_ALIAS	1024
#define MB_VEL_SCALE	8.0f		// encode: ~0.125 UV/frame fills the [0,1] RG range
static mb_alias_t	mb_alias_list[MB_MAX_ALIAS];
static int		mb_alias_count;

void R_MotionBlur_NewFrame (void)
{
	mb_alias_count = 0;	// reset the per-frame draw list before the scene captures into it
}

void R_MotionBlur_PushAlias (GLuint vbo, GLuint ebo, int numindexes, int indexofs,
	int stride, const void *p1n, const void *p2n, const void *p1p, const void *p2p,
	float blendN, float blendP, const float *mvpN, const float *mvpP, int viewmodel,
	int zombie)
{
#ifdef __SWITCH__
	mb_alias_t *m;
	if (mb_alias_count >= MB_MAX_ALIAS)
		return;
	m = &mb_alias_list[mb_alias_count++];
	m->vbo = vbo; m->ebo = ebo; m->numindexes = numindexes;
	m->indexofs = indexofs; m->stride = stride;
	m->p1n = p1n; m->p2n = p2n; m->p1p = p1p; m->p2p = p2p;
	m->blendN = blendN; m->blendP = blendP; m->viewmodel = viewmodel; m->zombie = zombie;
	memcpy (m->mvpN, mvpN, sizeof(m->mvpN));
	memcpy (m->mvpP, mvpP, sizeof(m->mvpP));
#else
	(void)vbo; (void)ebo; (void)numindexes; (void)indexofs; (void)stride;
	(void)p1n; (void)p2n; (void)p1p; (void)p2p; (void)blendN; (void)blendP;
	(void)mvpN; (void)mvpP; (void)viewmodel; (void)zombie;
#endif
}

#ifdef __SWITCH__
static GLuint	vel_fbo, vel_tex, vel_depthrb;	// velocity render target (view-sized)
static GLuint	vel_scenedepth;			// scene depth copy (for the occlusion mask)
static GLuint	ssaa_depth;			// SSAA FBO depth (a TEXTURE) -- reused for occlusion (defined below)
static GLuint	ssaa_color;
static int	ssaa_w, ssaa_h;
static int	vel_tw, vel_th, vel_vw, vel_vh;	// allocated (pow2) + tracked view size
static GLuint	vel_cam_program;		// fullscreen: camera/world reprojection velocity (the room)
static GLint	vcam_depthLoc=-1, vcam_prevVPLoc=-1, vcam_smaxLoc=-1, vcam_tmaxLoc=-1, vcam_strengthLoc=-1, vcam_fwdLoc=-1;
static GLuint	vel_obj_program;		// rasterise per-object velocity (zombies/gun/hands)
static GLint	vobj_mvpNLoc=-1, vobj_mvpPLoc=-1, vobj_blendNLoc=-1, vobj_blendPLoc=-1, vobj_objStrLoc=-1;
static GLint	vobj_depthLoc=-1, vobj_capLoc=-1, vobj_maskonLoc=-1, vobj_smaxLoc=-1, vobj_tmaxLoc=-1, vobj_vwLoc=-1, vobj_vhLoc=-1;
static GLuint	vel_rain_program;		// rain streak velocity (fast fall + camera)
static GLint	vrain_prevVPLoc=-1, vrain_fallLoc=-1, vrain_strLoc=-1;
static GLint	vrain_depthLoc=-1, vrain_smaxLoc=-1, vrain_tmaxLoc=-1, vrain_vwLoc=-1, vrain_vhLoc=-1;
static GLuint	vel_blur_program;		// smear the screen along the velocity buffer
static GLint	vblur_colorLoc=-1, vblur_velLoc=-1, vblur_strengthLoc=-1, vblur_smaxLoc=-1, vblur_tmaxLoc=-1, vblur_debugLoc=-1;
static qboolean	vel_failed;

void R_VelocityBuffer_Delete (void)
{
	if (vel_fbo)        { glDeleteFramebuffers (1, &vel_fbo);    vel_fbo = 0; }
	if (vel_tex)        { glDeleteTextures (1, &vel_tex);        vel_tex = 0; }
	if (vel_depthrb)    { glDeleteRenderbuffers (1, &vel_depthrb); vel_depthrb = 0; }
	if (vel_scenedepth) { glDeleteTextures (1, &vel_scenedepth); vel_scenedepth = 0; }
	vel_vw = vel_vh = 0;
	vel_cam_program = vel_obj_program = vel_rain_program = vel_blur_program = 0;
	vel_failed = false;
}

static void R_VelocityBuffer_CreateShaders (void)
{
	// camera/world fill: per-pixel screen velocity from depth reprojection (this frame's
	// inverse VP through last frame's PrevVP), pre-scaled by strength. Objects overwrite their pixels.
	const GLchar *cvert =
		"#version 110\n"
		"varying vec2 tc;\n"
		"void main(void){ gl_Position = vec4(gl_Vertex.xy,0.0,1.0); tc = gl_MultiTexCoord0.xy; }\n";
	const GLchar *cfrag =
		"#version 110\n"
		"uniform sampler2D SceneDepth;\n"
		"uniform mat4 PrevVP;\n"
		"uniform float smax;\n"
		"uniform float tmax;\n"
		"uniform float strength;\n"
		"uniform float fwd;\n"		// signed forward speed -> radial zoom blur (depth-independent)
		"varying vec2 tc;\n"
		"void main(void){\n"
		"	vec2 stc = vec2(tc.x*smax, tc.y*tmax);\n"
		"	float d = texture2D(SceneDepth, stc).r;\n"
		"	vec4 ndc = vec4(tc.x*2.0-1.0, tc.y*2.0-1.0, d*2.0-1.0, 1.0);\n"
		"	vec4 wp = gl_ModelViewProjectionMatrixInverse * ndc;\n"
		"	wp /= wp.w;\n"
		"	vec4 pc = PrevVP * wp;\n"
		"	vec2 prevtc = (pc.xy/pc.w) * 0.5 + 0.5;\n"
		"	vec2 vel = (tc - prevtc) * strength;\n"
		"	vel += (tc - 0.5) * fwd;\n"	// forward = outward streaks, backward = inward; zero at crosshair
		"	float vl = length(vel); if (vl > 0.02) vel *= 0.02 / vl;\n"	// world streak cap
		"	gl_FragColor = vec4(vel * 4.0 + 0.5, 0.0, 1.0);\n"	// encode: *(MB_VEL_SCALE*0.5)
		"}\n";

	// object velocity pass: project this pose with MVPnow and last frame's with MVPprev;
	// the screen-space delta is per-vertex velocity (captures movement AND limb animation).
	const GLchar *overt =
		"#version 110\n"
		"attribute vec4 P1n;\n"		// this frame pose verts (raw byte coords)
		"attribute vec4 P2n;\n"
		"attribute vec4 P1p;\n"		// last frame pose verts
		"attribute vec4 P2p;\n"
		"uniform mat4 MVPnow;\n"
		"uniform mat4 MVPprev;\n"
		"uniform float Bnow;\n"
		"uniform float Bprev;\n"
		"varying vec2 vVel;\n"		// screen-UV velocity
		"void main(void){\n"
		"	vec4 vn = mix(vec4(P1n.xyz,1.0), vec4(P2n.xyz,1.0), Bnow);\n"
		"	vec4 vp = mix(vec4(P1p.xyz,1.0), vec4(P2p.xyz,1.0), Bprev);\n"
		"	vec4 cn = MVPnow * vn;\n"
		"	vec4 cp = MVPprev * vp;\n"
		"	gl_Position = cn;\n"
		"	vVel = (cn.xy/cn.w - cp.xy/cp.w) * 0.5;\n"	// NDC delta /2 = UV delta
		"}\n";
	const GLchar *ofrag =
		"#version 110\n"
		"uniform sampler2D SceneDepth;\n"
		"uniform float objStrength;\n"	// object shutter, baked into the velocity here
		"uniform float objCap;\n"		// per-entity streak cap (zombies higher than gun/props)
		"uniform float maskon;\n"		// 1 = occlude where hidden by world, 0 = off (fallback)
		"uniform float smax;\n"
		"uniform float tmax;\n"
		"uniform float vpw;\n"
		"uniform float vph;\n"
		"varying vec2 vVel;\n"
		"void main(void){\n"
		"	vec2 uv = vec2(gl_FragCoord.x/vpw, gl_FragCoord.y/vph);\n"
		"	float sceneZ = texture2D(SceneDepth, vec2(uv.x*smax, uv.y*tmax)).r;\n"
		"	float vis = mix(1.0, step(gl_FragCoord.z, sceneZ + 0.0015), maskon);\n"	// 0 where hidden by world
		"	vec2 vel = vVel * objStrength * vis;\n"	// per-object motion (transform + animation)
		"	float vl = length(vel); if (vl > objCap) vel *= objCap / vl;\n"
		"	gl_FragColor = vec4(vel * 4.0 + 0.5, 0.0, 1.0);\n"	// encode: *(MB_VEL_SCALE*0.5)
		"}\n";

	// Final blur: smear the screen along the decoded velocity. Zero velocity -> identity.
	const GLchar *bvert =
		"#version 110\n"
		"varying vec2 tc;\n"
		"void main(void){ gl_Position = vec4(gl_Vertex.xy,0.0,1.0); tc = gl_MultiTexCoord0.xy; }\n";
	const GLchar *bfrag =
		"#version 110\n"
		"uniform sampler2D ColorTex;\n"
		"uniform sampler2D VelTex;\n"
		"uniform float strength;\n"
		"uniform float smax;\n"
		"uniform float tmax;\n"
		"uniform float debug;\n"
		"varying vec2 tc;\n"
		"void main(void){\n"
		"	vec2 stc = vec2(tc.x*smax, tc.y*tmax);\n"
		"	vec2 vel = (texture2D(VelTex, stc).rg - 0.5) * 0.25;\n"	// decode: *(2.0/MB_VEL_SCALE)
		"	vec3 dbg = vec3(abs(vel.x)*20.0, abs(vel.y)*20.0, 0.0);\n"	// debug viz: motion as R/G
		"	vel *= strength;\n"	// each source already clamped its own streak length
		"	vec3 sum = vec3(0.0);\n"
		"	float wsum = 0.0;\n"
		"	for (int i = 0; i < 21; i++) {\n"	// dense taps -> long fast-snap streaks stay smooth (no ghost copies)
		"		float t = (float(i)/20.0) - 0.5;\n"
		"		float w = 1.0 - abs(t)*2.0;\n"	// triangular: center-heavy -> smooth trail, not a hard ghost
		"		vec2 s = tc + vel * t;\n"
		"		sum += texture2D(ColorTex, vec2(s.x*smax, s.y*tmax)).rgb * w;\n"
		"		wsum += w;\n"
		"	}\n"
		"	gl_FragColor = vec4(mix(sum / wsum, dbg, step(0.5, debug)), 1.0);\n"
		"}\n";

	static const glsl_attrib_binding_t obindings[] = {
		{"P1n", 0}, {"P2n", 1}, {"P1p", 2}, {"P2p", 3}
	};

	if (!gl_glsl_able || !GL_UniformMatrix4fvFunc) { vel_failed = true; return; }

	vel_cam_program = GL_CreateProgram (cvert, cfrag, 0, NULL);
	if (vel_cam_program) {
		vcam_depthLoc    = GL_GetUniformLocation (&vel_cam_program, "SceneDepth");
		vcam_prevVPLoc   = GL_GetUniformLocation (&vel_cam_program, "PrevVP");
		vcam_smaxLoc     = GL_GetUniformLocation (&vel_cam_program, "smax");
		vcam_tmaxLoc     = GL_GetUniformLocation (&vel_cam_program, "tmax");
		vcam_strengthLoc = GL_GetUniformLocation (&vel_cam_program, "strength");
		vcam_fwdLoc      = GL_GetUniformLocation (&vel_cam_program, "fwd");
	} else vel_failed = true;

	vel_obj_program = GL_CreateProgram (overt, ofrag, 4, obindings);
	if (vel_obj_program) {
		vobj_mvpNLoc   = GL_GetUniformLocation (&vel_obj_program, "MVPnow");
		vobj_mvpPLoc   = GL_GetUniformLocation (&vel_obj_program, "MVPprev");
		vobj_blendNLoc = GL_GetUniformLocation (&vel_obj_program, "Bnow");
		vobj_blendPLoc = GL_GetUniformLocation (&vel_obj_program, "Bprev");
		vobj_objStrLoc = GL_GetUniformLocation (&vel_obj_program, "objStrength");
		vobj_capLoc    = GL_GetUniformLocation (&vel_obj_program, "objCap");
		vobj_maskonLoc = GL_GetUniformLocation (&vel_obj_program, "maskon");
		vobj_depthLoc  = GL_GetUniformLocation (&vel_obj_program, "SceneDepth");
		vobj_smaxLoc   = GL_GetUniformLocation (&vel_obj_program, "smax");
		vobj_tmaxLoc   = GL_GetUniformLocation (&vel_obj_program, "tmax");
		vobj_vwLoc     = GL_GetUniformLocation (&vel_obj_program, "vpw");
		vobj_vhLoc     = GL_GetUniformLocation (&vel_obj_program, "vph");
	} else vel_failed = true;

	// Rain streak velocity: world pos this frame vs (pos - fall*dt) last frame, so the
	// fast vertical fall (plus camera motion) becomes a per-pixel velocity on the streaks.
	{
		const GLchar *rvert =
			"#version 110\n"
			"attribute vec3 RainPos;\n"
			"uniform mat4 PrevVP;\n"		// camera previous view-proj (curVP = built-in matrix stack)
			"uniform vec3 fallStep;\n"	// world fall delta for one frame
			"uniform float strength;\n"
			"varying vec2 vVel;\n"
			"void main(void){\n"
			"	vec4 cn = gl_ModelViewProjectionMatrix * vec4(RainPos, 1.0);\n"
			"	vec4 cp = PrevVP * vec4(RainPos - fallStep, 1.0);\n"
			"	gl_Position = cn;\n"
			"	vVel = (cn.xy/cn.w - cp.xy/cp.w) * 0.5 * strength;\n"
			"}\n";
		// occlude rain against full scene depth: rain behind a wall/zombie writes zero
		// velocity (no smear) instead of dragging that surface's colour along the streak.
		const GLchar *rfrag =
			"#version 110\n"
			"uniform sampler2D SceneDepth;\n"
			"uniform float smax;\n"
			"uniform float tmax;\n"
			"uniform float vpw;\n"
			"uniform float vph;\n"
			"varying vec2 vVel;\n"
			"void main(void){\n"
			"	vec2 uv = vec2(gl_FragCoord.x/vpw, gl_FragCoord.y/vph);\n"
			"	float sceneZ = texture2D(SceneDepth, vec2(uv.x*smax, uv.y*tmax)).r;\n"
			"	float vis = step(gl_FragCoord.z, sceneZ + 0.0008);\n"	// 1 only if rain is in front of the scene
			"	vec2 vel = vVel * vis;\n"
			"	float vl = length(vel); if (vl > 0.025) vel *= 0.025 / vl;\n"	// rain streak cap
			"	gl_FragColor = vec4(vel * 4.0 + 0.5, 0.0, 1.0);\n"
			"}\n";
		static const glsl_attrib_binding_t rbindings[] = { {"RainPos", 0} };
		vel_rain_program = GL_CreateProgram (rvert, rfrag, 1, rbindings);
		if (vel_rain_program) {
			vrain_prevVPLoc = GL_GetUniformLocation (&vel_rain_program, "PrevVP");
			vrain_fallLoc   = GL_GetUniformLocation (&vel_rain_program, "fallStep");
			vrain_strLoc    = GL_GetUniformLocation (&vel_rain_program, "strength");
			vrain_depthLoc  = GL_GetUniformLocation (&vel_rain_program, "SceneDepth");
			vrain_smaxLoc   = GL_GetUniformLocation (&vel_rain_program, "smax");
			vrain_tmaxLoc   = GL_GetUniformLocation (&vel_rain_program, "tmax");
			vrain_vwLoc     = GL_GetUniformLocation (&vel_rain_program, "vpw");
			vrain_vhLoc     = GL_GetUniformLocation (&vel_rain_program, "vph");
		}	// rain is optional -> don't set vel_failed if it doesn't build
	}

	vel_blur_program = GL_CreateProgram (bvert, bfrag, 0, NULL);
	if (vel_blur_program) {
		vblur_colorLoc    = GL_GetUniformLocation (&vel_blur_program, "ColorTex");
		vblur_velLoc      = GL_GetUniformLocation (&vel_blur_program, "VelTex");
		vblur_strengthLoc = GL_GetUniformLocation (&vel_blur_program, "strength");
		vblur_smaxLoc     = GL_GetUniformLocation (&vel_blur_program, "smax");
		vblur_tmaxLoc     = GL_GetUniformLocation (&vel_blur_program, "tmax");
		vblur_debugLoc    = GL_GetUniformLocation (&vel_blur_program, "debug");
	} else vel_failed = true;
}

/*
================
R_VelocityBuffer_Build -- replay the frame's alias draws into the velocity texture.
================
*/
void R_VelocityBuffer_Build (void)
{
	int	i, srcx, srcy, srcw, srch;
	float	curVP[16], smax, tmax;

	if (gl_motionblur.value <= 0.0f) return;
	if (!r_scene_matrices_valid) return;
	if (vel_failed) return;
	if (!vel_obj_program) { R_VelocityBuffer_CreateShaders (); if (!vel_obj_program) return; }

	Matrix4_Mult (r_scene_proj, r_scene_mv, curVP);	// camera world->clip this frame
	if (!mb_prevVP_valid) { memcpy (mb_prevVP, curVP, sizeof(curVP)); mb_prevVP_valid = true; }

	static float mb_prevfov = 0.0f;	// kill blur on FOV snaps (scope in/out) -- prev vs now is huge
	float mb_blur_scale = (fabs(r_refdef.fov_x - mb_prevfov) > 0.5f) ? 0.0f : 1.0f;
	mb_prevfov = r_refdef.fov_x;

	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width;
	srch = r_refdef.vrect.height;
	if (srcw <= 0 || srch <= 0) return;

	GL_DisableMultitexture ();

	if (!vel_fbo)
	{
		glGenFramebuffers (1, &vel_fbo);
		glGenTextures (1, &vel_tex);
		glGenRenderbuffers (1, &vel_depthrb);
		glGenTextures (1, &vel_scenedepth);
		vel_vw = 0;
	}
	if (vel_vw != srcw || vel_vh != srch)
	{
		vel_vw = srcw; vel_vh = srch; vel_tw = srcw; vel_th = srch;
		if (!gl_texture_NPOT) { vel_tw = TexMgr_Pad (vel_tw); vel_th = TexMgr_Pad (vel_th); }

		glBindTexture (GL_TEXTURE_2D, vel_tex);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, vel_tw, vel_th, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindTexture (GL_TEXTURE_2D, vel_scenedepth);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, vel_tw, vel_th, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindRenderbuffer (GL_RENDERBUFFER, vel_depthrb);
		glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, vel_tw, vel_th);
		glBindTexture (GL_TEXTURE_2D, 0);
	}

	// occlusion uses the SSAA FBO's real depth texture (no color->depth copy, citron-safe).
	// only available while SSAA is on; gated by r_mblur_scenedepth.
	qboolean have_depth = (r_mblur_scenedepth.value > 0.0f && r_ssaa_active > 1 && ssaa_depth != 0);
	GLuint depthtex = have_depth ? ssaa_depth : vel_scenedepth;
	float dsmax = have_depth ? 1.0f : smax;
	float dtmax = have_depth ? 1.0f : tmax;

	glBindFramebuffer (GL_FRAMEBUFFER, vel_fbo);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vel_tex, 0);
	glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, vel_depthrb);
	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		glBindFramebuffer (GL_FRAMEBUFFER, 0);
		vel_failed = true;
		return;
	}

	smax = srcw / (float)vel_tw;
	tmax = srch / (float)vel_th;

	glViewport (0, 0, srcw, srch);
	glClearColor (0.5f, 0.5f, 0.0f, 1.0f);	// 0.5 = zero velocity
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// load this frame's camera matrices so gl_ModelViewProjectionMatrixInverse = inverse(curVP)
	// (both passes use it: the fill directly, the object pass for its occluded fallback).
	glMatrixMode (GL_PROJECTION); glLoadMatrixf (r_scene_proj);
	glMatrixMode (GL_MODELVIEW);  glLoadMatrixf (r_scene_mv);

	glDisable (GL_BLEND);
	glDisable (GL_CULL_FACE);
	glDisable (GL_ALPHA_TEST);

	GL_SelectTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, vel_scenedepth);

	// --- STEP A: camera/world reprojection fill (the room blurs when you move) ---
	glDisable (GL_DEPTH_TEST);	// fullscreen fill, no depth write -> objects below win
	GL_UseProgramFunc (vel_cam_program);
	GL_Uniform1iFunc (vcam_depthLoc, 0);
	GL_UniformMatrix4fvFunc (vcam_prevVPLoc, 1, GL_FALSE, mb_prevVP);
	GL_Uniform1fFunc (vcam_smaxLoc, smax);
	GL_Uniform1fFunc (vcam_tmaxLoc, tmax);
	{	// World blur scales with BOTH movement speed AND view-turn speed, so whipping the
		// camera blurs the floor/walls to match the objects (sandbags) -- which looked wrong
		// when the world stayed razor-sharp. Walking-only stays subtle.
		static float mb_prevyaw = 0.0f, mb_prevpitch = 0.0f;
		static qboolean mb_anglesinit = false;
		float spd = sqrt (cl.velocity[0]*cl.velocity[0] + cl.velocity[1]*cl.velocity[1]);
		float wf = (spd > 220.0f) ? 2.5f : (spd > 40.0f) ? 1.0f : 0.7f;	// sprint / walk / stand
		float dy = r_refdef.viewangles[YAW]   - mb_prevyaw;
		float dp = r_refdef.viewangles[PITCH] - mb_prevpitch;
		float turn, turnf;
		while (dy > 180.0f) dy -= 360.0f; while (dy < -180.0f) dy += 360.0f;	// yaw wrap
		turn = sqrt (dy*dy + dp*dp);	// degrees this frame
		mb_prevyaw = r_refdef.viewangles[YAW]; mb_prevpitch = r_refdef.viewangles[PITCH];
		if (!mb_anglesinit) { turn = 0.0f; mb_anglesinit = true; }	// no spike on the first frame
		{	// camera jolts (barricade-repair kick, weapon recoil) punch the view angles -> blur the
			// world to match. Tracked off the raw punchangle so a sharp kick reads strongly even
			// though the view smooths it; capped so it can't blow out.
			static float mb_prevpunchx = 0.0f, mb_prevpunchy = 0.0f;
			float jx = cl.punchangle[0] - mb_prevpunchx, jy = cl.punchangle[1] - mb_prevpunchy;
			float jolt = sqrt (jx*jx + jy*jy);
			mb_prevpunchx = cl.punchangle[0]; mb_prevpunchy = cl.punchangle[1];
			turn += jolt * 3.0f;
		}
		turnf = 1.0f + turn * 1.2f;	// fast whip -> much more world blur
		if (turnf > 10.0f) turnf = 10.0f;
		GL_Uniform1fFunc (vcam_strengthLoc, r_mblur_world.value * wf * turnf * mb_blur_scale);
		{	// radial zoom blur so forward/backward movement reads symmetrically (reprojection
			// alone barely blurs forward motion into distant geometry). Zero at the crosshair.
			float fdot = DotProduct (cl.velocity, vpn) / 300.0f;	// signed, normalized to ~run speed
			if (fdot > 1.0f) fdot = 1.0f; else if (fdot < -1.0f) fdot = -1.0f;
			GL_Uniform1fFunc (vcam_fwdLoc, (r_mblur_world.value > 0.0f) ? fdot * 0.035f * mb_blur_scale : 0.0f);
		}
	}
	glBegin (GL_QUADS);
	glTexCoord2f (0, 0); glVertex2f (-1, -1);
	glTexCoord2f (1, 0); glVertex2f ( 1, -1);
	glTexCoord2f (1, 1); glVertex2f ( 1,  1);
	glTexCoord2f (0, 1); glVertex2f (-1,  1);
	glEnd ();

	// --- STEP B: per-object velocity (zombies/gun/hands), overwriting their pixels ---
	glEnable (GL_DEPTH_TEST);
	glDepthMask (GL_TRUE);
	glDepthFunc (GL_LEQUAL);

	GL_SelectTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, depthtex);	// world-occlusion mask (real depth when SSAA on)
	GL_UseProgramFunc (vel_obj_program);
	GL_Uniform1iFunc (vobj_depthLoc, 0);
	GL_Uniform1fFunc (vobj_maskonLoc, have_depth ? 1.0f : 0.0f);
	GL_Uniform1fFunc (vobj_smaxLoc, dsmax);
	GL_Uniform1fFunc (vobj_tmaxLoc, dtmax);
	GL_Uniform1fFunc (vobj_vwLoc, (float)vel_vw);
	GL_Uniform1fFunc (vobj_vhLoc, (float)vel_vh);

	GL_EnableVertexAttribArrayFunc (0);
	GL_EnableVertexAttribArrayFunc (1);
	GL_EnableVertexAttribArrayFunc (2);
	GL_EnableVertexAttribArrayFunc (3);

	for (i = 0; i < mb_alias_count; i++)
	{
		mb_alias_t *m = &mb_alias_list[i];
		float ostr = m->viewmodel ? r_mblur_viewmodel.value
		           : m->zombie    ? r_mblur_zombie.value
		           :                 r_mblur_object.value;
		float ocap = m->viewmodel ? 0.028f : m->zombie ? 0.06f : 0.03f;	// gun: slow bob calm, fast ADS/recoil streaks; zombies long
		ostr *= mb_blur_scale;	// FOV snap (scope) -> no object blur this frame
		if (m->vbo == 0 || m->ebo == 0)
			continue;
		GL_Uniform1fFunc (vobj_objStrLoc, ostr);	// per-entity shutter (gun/zombie/prop)
		GL_Uniform1fFunc (vobj_capLoc, ocap);
		// the gun renders in front of the world (depth range 0..0.3); match it so its
		// depth lines up with the rest of the scene in the velocity buffer.
		if (m->viewmodel) glDepthRange (0.0, 0.3); else glDepthRange (0.0, 1.0);
		GL_BindBuffer (GL_ARRAY_BUFFER, m->vbo);
		GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, m->ebo);
		GL_VertexAttribPointerFunc (0, 4, GL_UNSIGNED_BYTE, GL_FALSE, m->stride, m->p1n);
		GL_VertexAttribPointerFunc (1, 4, GL_UNSIGNED_BYTE, GL_FALSE, m->stride, m->p2n);
		GL_VertexAttribPointerFunc (2, 4, GL_UNSIGNED_BYTE, GL_FALSE, m->stride, m->p1p);
		GL_VertexAttribPointerFunc (3, 4, GL_UNSIGNED_BYTE, GL_FALSE, m->stride, m->p2p);
		GL_UniformMatrix4fvFunc (vobj_mvpNLoc, 1, GL_FALSE, m->mvpN);
		GL_UniformMatrix4fvFunc (vobj_mvpPLoc, 1, GL_FALSE, m->mvpP);
		GL_Uniform1fFunc (vobj_blendNLoc, m->blendN);
		GL_Uniform1fFunc (vobj_blendPLoc, m->blendP);
		glDrawElements (GL_TRIANGLES, m->numindexes, GL_UNSIGNED_SHORT, (void *)(intptr_t)m->indexofs);
	}

	glDepthRange (0.0, 1.0);
	GL_DisableVertexAttribArrayFunc (1);	// rain (step C) reuses attrib 0 only
	GL_DisableVertexAttribArrayFunc (2);
	GL_DisableVertexAttribArrayFunc (3);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);

	// --- STEP C: rain streaks (Nacht) -- replay the rain lines with their fall velocity ---
	// needs scene depth to occlude (else rain smears over walls/wallbuys/objects)
	if (vel_rain_program && have_depth)
	{
		extern int R_Weather_RainVerts (float **pos, float *fallstep3);
		float *rpos = NULL, fall[3];
		int rcount = R_Weather_RainVerts (&rpos, fall);
		if (rcount > 0 && rpos)
		{
			// shader occludes against the scene depth -- rain behind walls/objects writes zero
			glDisable (GL_DEPTH_TEST);
			GL_SelectTexture (GL_TEXTURE0);
			glBindTexture (GL_TEXTURE_2D, depthtex);
			GL_BindBuffer (GL_ARRAY_BUFFER, 0);	// client-side array (like the scene rain draw)
			GL_VertexAttribPointerFunc (0, 3, GL_FLOAT, GL_FALSE, 0, rpos);
			GL_UseProgramFunc (vel_rain_program);
			GL_UniformMatrix4fvFunc (vrain_prevVPLoc, 1, GL_FALSE, mb_prevVP);
			GL_Uniform3fFunc (vrain_fallLoc, fall[0], fall[1], fall[2]);
			GL_Uniform1fFunc (vrain_strLoc, r_mblur_rain.value);
			GL_Uniform1iFunc (vrain_depthLoc, 0);
			GL_Uniform1fFunc (vrain_smaxLoc, dsmax);
			GL_Uniform1fFunc (vrain_tmaxLoc, dtmax);
			GL_Uniform1fFunc (vrain_vwLoc, (float)vel_vw);
			GL_Uniform1fFunc (vrain_vhLoc, (float)vel_vh);
			glLineWidth (1.5f);
			glDrawArrays (GL_LINES, 0, rcount);
			glLineWidth (1.0f);
		}
	}

	GL_DisableVertexAttribArrayFunc (0);
	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	GL_UseProgramFunc (0);

	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	glViewport (glx, gly, glwidth, glheight);
	GL_ClearBindings ();

	memcpy (mb_prevVP, curVP, sizeof(curVP));	// this frame's camera VP -> next frame's "previous"
}

/*
================
R_ObjectBlur -- smear the screen along the unified velocity buffer (world + objects).
================
*/
void R_ObjectBlur (void)
{
	int	srcx, srcy, srcw, srch;
	float	smax, tmax;

	if (gl_motionblur.value <= 0.0f) return;
	if (!vel_blur_program || !vel_tex || vel_tw <= 0) return;

	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width;
	srch = r_refdef.vrect.height;
	if (srcw <= 0 || srch <= 0) return;

	GL_DisableMultitexture ();

	// colour scratch (reuse the Stage-1 tex); on Switch R_MotionBlur doesn't run, so size
	// it here to match vel_tex's padded size (it isn't used as cross-frame history).
	if (!r_motionblur_texture) { glGenTextures (1, &r_motionblur_texture); r_motionblur_vw = 0; }
	if (r_motionblur_vw != srcw || r_motionblur_vh != srch || r_motionblur_tw != vel_tw)
	{
		r_motionblur_vw = srcw; r_motionblur_vh = srch;
		r_motionblur_tw = vel_tw; r_motionblur_th = vel_th;
		glBindTexture (GL_TEXTURE_2D, r_motionblur_texture);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, r_motionblur_tw, r_motionblur_th, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	glBindTexture (GL_TEXTURE_2D, r_motionblur_texture);
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);

	smax = srcw / (float)vel_tw;	// color scratch shares vel_tex's padded size
	tmax = srch / (float)vel_th;

	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glColor4f (1, 1, 1, 1);
	glViewport (srcx, srcy, srcw, srch);

	GL_SelectTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, r_motionblur_texture);
	GL_SelectTexture (GL_TEXTURE1);
	glBindTexture (GL_TEXTURE_2D, vel_tex);
	GL_SelectTexture (GL_TEXTURE0);

	GL_UseProgramFunc (vel_blur_program);
	GL_Uniform1iFunc (vblur_colorLoc, 0);
	GL_Uniform1iFunc (vblur_velLoc, 1);
	// world + object strengths are already baked into the velocity buffer -> flat 1.0 here
	GL_Uniform1fFunc (vblur_strengthLoc, 1.0f);
	GL_Uniform1fFunc (vblur_smaxLoc, smax);
	GL_Uniform1fFunc (vblur_tmaxLoc, tmax);
	GL_Uniform1fFunc (vblur_debugLoc, r_mblur_debug.value);

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0); glVertex2f (-1, -1);
	glTexCoord2f (1, 0); glVertex2f ( 1, -1);
	glTexCoord2f (1, 1); glVertex2f ( 1,  1);
	glTexCoord2f (0, 1); glVertex2f (-1,  1);
	glEnd ();

	GL_UseProgramFunc (0);
	glEnable (GL_DEPTH_TEST);
	GL_ClearBindings ();
}
#endif	// __SWITCH__

/*
================
R_PostProcess -- bloom + vignette, drawn over the final screen (after motion blur,
before the HUD). Single-pass each, citron-safe (one texture copy + fullscreen quads,
the same ops R_ScaleView/R_MotionBlur use -- no extra FBOs).
================
*/
#ifndef VITA
static GLuint	bloom_tex;
static int	bloom_tw, bloom_th, bloom_vw, bloom_vh;
static GLuint	bloom_program;
static GLint	bloom_tapXLoc = -1, bloom_tapYLoc = -1, bloom_threshLoc = -1, bloom_intensLoc = -1;
static qboolean	bloom_failed;
static GLuint	vignette_program;
static GLint	vignette_strengthLoc = -1;
static qboolean	vignette_failed;

static void R_TonemapColorGrade_CreateShader (void)
{
	const GLchar *vert =
		"#version 110\n"
		"varying vec2 tc;\n"
		"void main(void){ gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0); tc = gl_MultiTexCoord0.xy; }\n";
	const GLchar *frag =
		"#version 110\n"
		"uniform sampler2D Tex;\n"
		"uniform float contrast;\n"
		"uniform float saturation;\n"
		"uniform float mode;\n"		// 0=off 1=Clean 2=Cinematic 3=Cold 4=Noir 5=Vintage
		"varying vec2 tc;\n"
		"float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }\n"
		"void main(void) {\n"
		"	vec4 texCol = texture2D(Tex, tc);\n"
		"	vec3 c = texCol.rgb;\n"
		"	float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
		// --- tone/grade presets (LDR looks) ---
		"	if (mode > 0.5 && mode < 1.5) {\n"			// Clean: gentle S-curve + tiny warmth
		"		c = mix(c, c*c*(3.0-2.0*c), 0.18);\n"
		"		c *= vec3(1.03, 1.0, 0.97);\n"
		"	} else if (mode < 2.5) {\n"				// Cinematic: teal shadows / orange highlights
		"		c = mix(c, c*c*(3.0-2.0*c), 0.25);\n"
		"		vec3 sh = c * vec3(0.85, 1.0, 1.12);\n"
		"		vec3 hi = c * vec3(1.14, 1.02, 0.82);\n"
		"		c = mix(sh, hi, smoothstep(0.2, 0.8, l));\n"
		"		c = mix(vec3(l), c, 1.05);\n"
		"	} else if (mode < 3.5) {\n"				// Cold: desaturate, blue, crush blacks
		"		c = mix(vec3(l), c, 0.7);\n"
		"		c *= vec3(0.88, 0.96, 1.12);\n"
		"		c = max(vec3(0.0), c - 0.04) * 1.06;\n"
		"	} else if (mode < 4.5) {\n"				// Noir: desaturated + mild contrast, lifted so dark scenes still read
		"		c = mix(vec3(l), c, 0.12);\n"
		"		c = (c - 0.5) * 1.12 + 0.5;\n"
		"		c = c * 0.88 + 0.09;\n"
		"		c *= vec3(0.98, 0.99, 1.03);\n"
		"	} else if (mode > 4.5) {\n"				// Vintage horror: warm sepia, faded/lifted blacks
		"		c = mix(vec3(l), c, 0.55);\n"
		"		c = mix(c, vec3(l)*vec3(1.20, 0.98, 0.70), 0.45);\n"
		"		c = c*0.92 + 0.06;\n"				// lift = faded old film
		"	}\n"
		// --- user contrast/saturation sliders on top ---
		"	c = (c - vec3(0.5)) * contrast + vec3(0.5);\n"
		"	float l2 = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
		"	c = mix(vec3(l2), c, saturation);\n"
		"	c = clamp(c, 0.0, 1.0);\n"
		"	gl_FragColor = vec4(c, texCol.a);\n"
		"}\n";

	if (!gl_glsl_able) { r_tg_failed = true; return; }
	r_tg_program = GL_CreateProgram (vert, frag, 0, NULL);
	if (r_tg_program) {
		r_tg_contrastLoc = GL_GetUniformLocation (&r_tg_program, "contrast");
		r_tg_saturationLoc = GL_GetUniformLocation (&r_tg_program, "saturation");
		r_tg_modeLoc = GL_GetUniformLocation (&r_tg_program, "mode");
	} else r_tg_failed = true;
}

static void R_TonemapColorGrade (void)
{
	int srcx, srcy, srcw, srch;
	float smax, tmax;

	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width;
	srch = r_refdef.vrect.height;
	if (srcw <= 0 || srch <= 0) return;

	if (!r_tg_program && !r_tg_failed) R_TonemapColorGrade_CreateShader ();
	if (!r_tg_program) return;

	GL_DisableMultitexture ();
	if (!r_tg_texture) { glGenTextures (1, &r_tg_texture); r_tg_texture_width = r_tg_texture_height = 0; }
	glBindTexture (GL_TEXTURE_2D, r_tg_texture);
	if (r_tg_texture_width != srcw || r_tg_texture_height != srch)
	{
		r_tg_texture_width = srcw; r_tg_texture_height = srch;
		r_tg_texture_pad_width = srcw; r_tg_texture_pad_height = srch;
		if (!gl_texture_NPOT) {
			r_tg_texture_pad_width = TexMgr_Pad (r_tg_texture_pad_width);
			r_tg_texture_pad_height = TexMgr_Pad (r_tg_texture_pad_height);
		}
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, r_tg_texture_pad_width, r_tg_texture_pad_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);

	smax = srcw / (float)r_tg_texture_pad_width;
	tmax = srch / (float)r_tg_texture_pad_height;

	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glColor4f (1, 1, 1, 1);

	glViewport (srcx, srcy, srcw, srch);
	glMatrixMode (GL_PROJECTION); glLoadIdentity ();
	glMatrixMode (GL_MODELVIEW);  glLoadIdentity ();

	GL_UseProgramFunc (r_tg_program);
	GL_Uniform1fFunc (r_tg_contrastLoc, r_cg_contrast.value);
	GL_Uniform1fFunc (r_tg_saturationLoc, r_cg_saturation.value);
	GL_Uniform1fFunc (r_tg_modeLoc, r_tonemap.value);

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);		glVertex2f (-1, -1);
	glTexCoord2f (smax, 0);		glVertex2f ( 1, -1);
	glTexCoord2f (smax, tmax);	glVertex2f ( 1,  1);
	glTexCoord2f (0, tmax);		glVertex2f (-1,  1);
	glEnd ();

	GL_UseProgramFunc (0);
	glEnable (GL_DEPTH_TEST);
	GL_ClearBindings ();
}

void R_PostProcess_DeleteTextures (void)
{
	if (bloom_tex) { glDeleteTextures (1, &bloom_tex); bloom_tex = 0; }
	bloom_vw = bloom_vh = 0;
	bloom_program = 0; bloom_failed = false;
	vignette_program = 0; vignette_failed = false;

	if (r_tg_texture) { glDeleteTextures (1, &r_tg_texture); r_tg_texture = 0; }
	r_tg_texture_width = r_tg_texture_height = 0;
	r_tg_program = 0; r_tg_failed = false;
}

static void R_Bloom_CreateShader (void)
{
	const GLchar *vert =
		"#version 110\n"
		"varying vec2 tc;\n"
		"void main(void){ gl_Position = vec4(gl_Vertex.xy,0.0,1.0); tc = gl_MultiTexCoord0.xy; }\n";
	const GLchar *frag =
		"#version 110\n"
		"uniform sampler2D Tex;\n"
		"uniform float tapX;\n"
		"uniform float tapY;\n"
		"uniform float threshold;\n"
		"uniform float intensity;\n"
		"varying vec2 tc;\n"
		"void main(void){\n"
		"	vec3 sum = vec3(0.0);\n"
		"	float wsum = 0.0;\n"
		"	for (int y = -2; y <= 2; y++) {\n"
		"		for (int x = -2; x <= 2; x++) {\n"
		"			vec2 off = vec2(float(x) * tapX, float(y) * tapY);\n"
		"			vec3 c = texture2D(Tex, tc + off).rgb;\n"
		"			float l = max(c.r, max(c.g, c.b));\n"
		"			float b = min(max(0.0, l - threshold), 0.30);\n"	// cap so already-bright glow sprites don't blow out
		"			float w = 1.0 / (1.0 + float(x*x + y*y));\n"
		"			sum += c * (b * w);\n"
		"			wsum += w;\n"
		"		}\n"
		"	}\n"
		"	gl_FragColor = vec4(sum / wsum * intensity, 1.0);\n"
		"}\n";
	if (!gl_glsl_able) { bloom_failed = true; return; }
	bloom_program = GL_CreateProgram (vert, frag, 0, NULL);
	if (bloom_program) {
		bloom_tapXLoc = GL_GetUniformLocation (&bloom_program, "tapX");
		bloom_tapYLoc = GL_GetUniformLocation (&bloom_program, "tapY");
		bloom_threshLoc = GL_GetUniformLocation (&bloom_program, "threshold");
		bloom_intensLoc = GL_GetUniformLocation (&bloom_program, "intensity");
	} else bloom_failed = true;
}

static void R_Bloom (void)
{
	int srcx, srcy, srcw, srch;
	float smax, tmax;

	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width;
	srch = r_refdef.vrect.height;
	if (srcw <= 0 || srch <= 0) return;

	if (!bloom_program && !bloom_failed) R_Bloom_CreateShader ();
	if (!bloom_program) return;

	GL_DisableMultitexture ();
	if (!bloom_tex) { glGenTextures (1, &bloom_tex); bloom_vw = bloom_vh = 0; }
	glBindTexture (GL_TEXTURE_2D, bloom_tex);
	if (bloom_vw != srcw || bloom_vh != srch)
	{
		bloom_vw = srcw; bloom_vh = srch;
		bloom_tw = srcw; bloom_th = srch;
		if (!gl_texture_NPOT) { bloom_tw = TexMgr_Pad (bloom_tw); bloom_th = TexMgr_Pad (bloom_th); }
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, bloom_tw, bloom_th, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	// grab the current screen, then add a blurred bright-pass back over it
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);

	smax = srcw / (float)bloom_tw;
	tmax = srch / (float)bloom_th;

	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glEnable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE);	// additive glow
	glColor4f (1, 1, 1, 1);

	glViewport (srcx, srcy, srcw, srch);
	glMatrixMode (GL_PROJECTION); glLoadIdentity ();
	glMatrixMode (GL_MODELVIEW);  glLoadIdentity ();

	GL_UseProgramFunc (bloom_program);
	GL_Uniform1fFunc (bloom_tapXLoc, smax / (float)srcw * 5.0f);	// ~5px tap spacing -> ~20px bloom radius
	GL_Uniform1fFunc (bloom_tapYLoc, tmax / (float)srch * 5.0f);
	GL_Uniform1fFunc (bloom_threshLoc, 0.40f);	// lower -> lit walls/lights bloom too, not just glow sprites
	GL_Uniform1fFunc (bloom_intensLoc, 0.55f);	// softer add

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);		glVertex2f (-1, -1);
	glTexCoord2f (smax, 0);		glVertex2f ( 1, -1);
	glTexCoord2f (smax, tmax);	glVertex2f ( 1,  1);
	glTexCoord2f (0, tmax);		glVertex2f (-1,  1);
	glEnd ();

	GL_UseProgramFunc (0);
	glDisable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);	// RESTORE default -- HUD draws after us
	glEnable (GL_DEPTH_TEST);
	GL_ClearBindings ();
}

static void R_Vignette_CreateShader (void)
{
	const GLchar *vert =
		"#version 110\n"
		"varying vec2 tc;\n"
		"void main(void){ gl_Position = vec4(gl_Vertex.xy,0.0,1.0); tc = gl_MultiTexCoord0.xy; }\n";
	const GLchar *frag =
		"#version 110\n"
		"uniform float strength;\n"
		"varying vec2 tc;\n"
		"void main(void){\n"
		"	vec2 d = tc - vec2(0.5);\n"
		"	float r = length(d) * 1.41421356;\n"		// 0 centre .. ~1 corner
		"	float v = 1.0 - strength * smoothstep(0.45, 1.0, r);\n"
		"	gl_FragColor = vec4(v, v, v, 1.0);\n"
		"}\n";
	if (!gl_glsl_able) { vignette_failed = true; return; }
	vignette_program = GL_CreateProgram (vert, frag, 0, NULL);
	if (vignette_program)
		vignette_strengthLoc = GL_GetUniformLocation (&vignette_program, "strength");
	else vignette_failed = true;
}

static void R_Vignette (void)
{
	int dstx, dsty, dstw, dsth;
	if (!vignette_program && !vignette_failed) R_Vignette_CreateShader ();
	if (!vignette_program) return;

	dstx = glx + r_refdef.vrect.x;
	dsty = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	dstw = r_refdef.vrect.width;
	dsth = r_refdef.vrect.height;

	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glEnable (GL_BLEND);
	glBlendFunc (GL_ZERO, GL_SRC_COLOR);	// multiply screen by the vignette factor
	glColor4f (1, 1, 1, 1);

	glViewport (dstx, dsty, dstw, dsth);
	glMatrixMode (GL_PROJECTION); glLoadIdentity ();
	glMatrixMode (GL_MODELVIEW);  glLoadIdentity ();

	GL_UseProgramFunc (vignette_program);
	GL_Uniform1fFunc (vignette_strengthLoc, 0.40f);	// gentle -- corners darken ~40% max

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0); glVertex2f (-1, -1);
	glTexCoord2f (1, 0); glVertex2f ( 1, -1);
	glTexCoord2f (1, 1); glVertex2f ( 1,  1);
	glTexCoord2f (0, 1); glVertex2f (-1,  1);
	glEnd ();

	GL_UseProgramFunc (0);
	glDisable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);	// RESTORE default -- HUD draws after us
	glEnable (GL_DEPTH_TEST);
	GL_ClearBindings ();
}

// ---- SSAO: samples the SSAA depth texture, multiplies the screen by the AO factor ----
extern GLuint ssaa_depth;	// SSAA FBO depth texture (defined with the velocity statics)
extern GLuint ssaa_color;
static GLuint	ssao_program;
static GLint	ssao_depthLoc=-1, ssao_farLoc=-1, ssao_strLoc=-1, ssao_colorLoc=-1;
static GLint	ssao_tanxLoc=-1, ssao_tanyLoc=-1, ssao_texelXLoc=-1, ssao_texelYLoc=-1, ssao_radiusLoc=-1, ssao_biasLoc=-1;
static qboolean	ssao_failed;
// screen-res linearized-depth capture: AO samples this small texture instead of the
// 4x SSAA depth, so its cost stops scaling with the supersample factor.
static GLuint	sdepth_program;
static GLint	sd_depthLoc=-1, sd_nearLoc=-1, sd_farLoc=-1;
static GLuint	sdepth_fbo, sdepth_tex;
static int	sdepth_w, sdepth_h;
// AO render target + bilateral (depth-aware) blur to denoise it before compositing
static GLuint	ao_fbo, ao_tex;
static int	ao_w, ao_h;
static GLuint	blur_program;
static GLint	blur_aoLoc=-1, blur_depthLoc=-1, blur_farLoc=-1, blur_texelXLoc=-1, blur_texelYLoc=-1;

static void R_SSAO_CreateShader (void)
{
	const GLchar *vert =
		"#version 110\n"
		"varying vec2 tc;\n"
		"void main(void){ gl_Position = vec4(gl_Vertex.xy,0.0,1.0); tc = gl_MultiTexCoord0.xy; }\n";

	// linearize+pack the SSAA depth into a screen-res RGB8 texture
	const GLchar *sdfrag =
		"#version 110\n"
		"uniform sampler2D DepthTex;\n"
		"uniform float dnear, dfar;\n"
		"varying vec2 tc;\n"
		"float lz(float d){ return (2.0*dnear*dfar) / (dfar+dnear - (d*2.0-1.0)*(dfar-dnear)); }\n"
		"vec3 packd(float v){ vec3 e = fract(v*vec3(1.0,255.0,65025.0)); e -= e.yzz*vec3(1.0/255.0,1.0/255.0,0.0); return e; }\n"
		"void main(void){\n"
		"	float d = texture2D(DepthTex, tc).r;\n"
		"	if (d < 0.301) d = clamp(d / 0.3, 0.0, 1.0);\n"
		"	float zn = clamp(lz(d) / dfar, 0.0, 1.0);\n"
		"	gl_FragColor = vec4(packd(zn), 1.0);\n"
		"}\n";

	const GLchar *frag =
		"#version 110\n"
		"uniform sampler2D DepthTex;\n"	// packed linear depth, screen res
		"uniform sampler2D ColorTex;\n"
		"uniform float dfar, strength;\n"
		"uniform float tanX, tanY, radius, bias;\n"
		"uniform float texelX, texelY;\n"
		"varying vec2 tc;\n"
		"float unpackd(vec3 c){ return dot(c, vec3(1.0, 1.0/255.0, 1.0/65025.0)); }\n"
		"float lze(vec2 uv){ return unpackd(texture2D(DepthTex, uv).rgb) * dfar; }\n"
		"vec3 vpos(vec2 uv){\n"
		"	float ze = lze(uv);\n"
		"	return vec3((uv.x*2.0-1.0)*tanX, (uv.y*2.0-1.0)*tanY, -1.0) * ze;\n"
		"}\n"
		"void main(void){\n"
		"	vec3 P = vpos(tc);\n"
		"	float dist = -P.z;\n"
		"	vec3 pr = vpos(tc + vec2(texelX, 0.0));\n"
		"	vec3 pl = vpos(tc - vec2(texelX, 0.0));\n"
		"	vec3 pu = vpos(tc + vec2(0.0, texelY));\n"
		"	vec3 pd = vpos(tc - vec2(0.0, texelY));\n"
		"	vec3 dx = abs(pr.z - P.z) < abs(P.z - pl.z) ? (pr - P) : (P - pl);\n"
		"	vec3 dy = abs(pu.z - P.z) < abs(P.z - pd.z) ? (pu - P) : (P - pd);\n"
		"	vec3 N = normalize(cross(dx, dy));\n"
		"	float rang = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898,78.233)))*43758.5453) * 6.2831853;\n"
		"	vec3 up = abs(N.z) < 0.99 ? vec3(0.0,0.0,1.0) : vec3(1.0,0.0,0.0);\n"
		"	vec3 T = normalize(cross(up, N));\n"
		"	vec3 B = cross(N, T);\n"
		"	float occ = 0.0;\n"
		"	float current_radius = radius;\n"
		"	float current_bias = bias;\n"
		"	float current_strength = strength;\n"
		"	if (dist < 8.0){\n"
		"		current_radius = 1.2;\n"
		"		current_bias = 0.015;\n"
		"		current_strength = strength * 1.0;\n"
		"	}\n"
		"	int i;\n"
		"	for (i = 0; i < 16; i++){\n"
		"		float fi = float(i);\n"
		"		float k = (fi + 0.5) / 16.0;\n"
		"		float theta = fi * 2.3999632 + rang;\n"
		"		float rr = sqrt(k);\n"
		"		vec3 s = vec3(cos(theta)*rr, sin(theta)*rr, sqrt(1.0 - k));\n"
		"		float slen = mix(0.1, 1.0, k*k);\n"
		"		vec3 sp = P + (T*s.x + B*s.y + N*s.z) * current_radius * slen;\n"
		"		vec2 suv = vec2((sp.x / -sp.z) / tanX, (sp.y / -sp.z) / tanY) * 0.5 + 0.5;\n"
		"		float sz = lze(suv);\n"
		"		float sampZ = -sp.z;\n"
		"		float diff = abs(sampZ - sz);\n"
		"		float rc = 1.0 - smoothstep(0.0, current_radius, diff);\n"
		"		occ += (sz < sampZ - current_bias ? 1.0 : 0.0) * rc;\n"
		"	}\n"
		"	occ /= 16.0;\n"
		"	float ao = 1.0 - clamp(occ * current_strength, 0.0, 1.0);\n"
		"	if (dist >= 8.0){\n"
		"		ao = mix(1.0, ao, smoothstep(12.0, 32.0, dist));\n"
		"	}\n"
		"	vec3 col = texture2D(ColorTex, tc).rgb;\n"
		"	float luma = dot(col, vec3(0.299, 0.587, 0.114));\n"
		"	float color_fade = 1.0 - smoothstep(0.15, 0.45, luma);\n"
		"	ao = mix(1.0, ao, color_fade);\n"
		"	ao = mix(1.0, ao, 1.0 - smoothstep(300.0, 650.0, dist));\n"
		"	gl_FragColor = vec4(ao, ao, ao, 1.0);\n"
		"}\n";
	if (!gl_glsl_able) { ssao_failed = true; return; }
	sdepth_program = GL_CreateProgram (vert, sdfrag, 0, NULL);
	if (sdepth_program) {
		sd_depthLoc = GL_GetUniformLocation (&sdepth_program, "DepthTex");
		sd_nearLoc  = GL_GetUniformLocation (&sdepth_program, "dnear");
		sd_farLoc   = GL_GetUniformLocation (&sdepth_program, "dfar");
	}
	ssao_program = GL_CreateProgram (vert, frag, 0, NULL);
	if (ssao_program && sdepth_program) {
		ssao_depthLoc  = GL_GetUniformLocation (&ssao_program, "DepthTex");
		ssao_colorLoc  = GL_GetUniformLocation (&ssao_program, "ColorTex");
		ssao_farLoc    = GL_GetUniformLocation (&ssao_program, "dfar");
		ssao_strLoc    = GL_GetUniformLocation (&ssao_program, "strength");
		ssao_tanxLoc   = GL_GetUniformLocation (&ssao_program, "tanX");
		ssao_tanyLoc   = GL_GetUniformLocation (&ssao_program, "tanY");
		ssao_texelXLoc = GL_GetUniformLocation (&ssao_program, "texelX");
		ssao_texelYLoc = GL_GetUniformLocation (&ssao_program, "texelY");
		ssao_radiusLoc = GL_GetUniformLocation (&ssao_program, "radius");
		ssao_biasLoc   = GL_GetUniformLocation (&ssao_program, "bias");
	} else ssao_failed = true;

	// depth-aware (bilateral) blur: denoise the AO without bleeding across edges
	const GLchar *blurfrag =
		"#version 110\n"
		"uniform sampler2D AoTex;\n"
		"uniform sampler2D DepthTex;\n"
		"uniform float dfar, texelX, texelY;\n"
		"varying vec2 tc;\n"
		"float unpackd(vec3 c){ return dot(c, vec3(1.0, 1.0/255.0, 1.0/65025.0)); }\n"
		"void main(void){\n"
		"	float cz = unpackd(texture2D(DepthTex, tc).rgb) * dfar;\n"
		"	float sum = 0.0; float wsum = 0.0;\n"
		"	int x; int y;\n"
		"	for (y = -2; y <= 1; y++){\n"
		"		for (x = -2; x <= 1; x++){\n"
		"			vec2 o = vec2(float(x)+0.5, float(y)+0.5) * vec2(texelX, texelY);\n"
		"			float sz = unpackd(texture2D(DepthTex, tc + o).rgb) * dfar;\n"
		"			float w = exp(-abs(cz - sz) * 0.15);\n"
		"			sum += texture2D(AoTex, tc + o).r * w; wsum += w;\n"
		"		}\n"
		"	}\n"
		"	float ao = wsum > 0.0 ? sum / wsum : 1.0;\n"
		"	gl_FragColor = vec4(ao, ao, ao, 1.0);\n"
		"}\n";
	blur_program = GL_CreateProgram (vert, blurfrag, 0, NULL);
	if (blur_program) {
		blur_aoLoc     = GL_GetUniformLocation (&blur_program, "AoTex");
		blur_depthLoc  = GL_GetUniformLocation (&blur_program, "DepthTex");
		blur_farLoc    = GL_GetUniformLocation (&blur_program, "dfar");
		blur_texelXLoc = GL_GetUniformLocation (&blur_program, "texelX");
		blur_texelYLoc = GL_GetUniformLocation (&blur_program, "texelY");
	} else ssao_failed = true;
}

void R_SSAO_DeleteFBO (void)
{
	if (sdepth_fbo) { glDeleteFramebuffers (1, &sdepth_fbo); sdepth_fbo = 0; }
	if (sdepth_tex) { glDeleteTextures (1, &sdepth_tex); sdepth_tex = 0; }
	if (ao_fbo) { glDeleteFramebuffers (1, &ao_fbo); ao_fbo = 0; }
	if (ao_tex) { glDeleteTextures (1, &ao_tex); ao_tex = 0; }
	sdepth_w = sdepth_h = ao_w = ao_h = 0;
}

static void R_SSAO_DrawQuad (void)
{
	glBegin (GL_QUADS);
	glTexCoord2f (0, 0); glVertex2f (-1, -1);
	glTexCoord2f (1, 0); glVertex2f ( 1, -1);
	glTexCoord2f (1, 1); glVertex2f ( 1,  1);
	glTexCoord2f (0, 1); glVertex2f (-1,  1);
	glEnd ();
}

static void R_SSAO_SizeTarget (GLuint *fbo, GLuint *tex, int *w, int *h, int rw, int rh)
{
	if (!*fbo) { glGenFramebuffers (1, fbo); glGenTextures (1, tex); *w = *h = 0; }
	if (*w == rw && *h == rh) return;
	*w = rw; *h = rh;
	glBindTexture (GL_TEXTURE_2D, *tex);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, rw, rh, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture (GL_TEXTURE_2D, 0);
}

static void R_SSAO (void)
{
	int dstx, dsty, dstw, dsth;
	if (!ssao_program && !ssao_failed) R_SSAO_CreateShader ();
	if (!ssao_program || !sdepth_program || !blur_program) return;

	dstx = glx + r_refdef.vrect.x;
	dsty = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	dstw = r_refdef.vrect.width;
	dsth = r_refdef.vrect.height;

	R_SSAO_SizeTarget (&sdepth_fbo, &sdepth_tex, &sdepth_w, &sdepth_h, dstw, dsth);
	R_SSAO_SizeTarget (&ao_fbo, &ao_tex, &ao_w, &ao_h, dstw, dsth);

	float tanx = tanf (DEG2RAD (r_refdef.fov_x) * 0.5f);
	float tany = tanf (DEG2RAD (r_refdef.fov_y) * 0.5f);

	GL_DisableMultitexture ();
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glColor4f (1, 1, 1, 1);
	glMatrixMode (GL_PROJECTION); glLoadIdentity ();
	glMatrixMode (GL_MODELVIEW);  glLoadIdentity ();

	// pass 1: SSAA depth -> packed screen-res linear depth
	glBindFramebuffer (GL_FRAMEBUFFER, sdepth_fbo);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sdepth_tex, 0);
	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		glBindFramebuffer (GL_FRAMEBUFFER, 0);
		glEnable (GL_DEPTH_TEST);
		GL_ClearBindings ();
		return;
	}
	glViewport (0, 0, dstw, dsth);
	GL_SelectTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, ssaa_depth);
	GL_UseProgramFunc (sdepth_program);
	GL_Uniform1iFunc (sd_depthLoc, 0);
	GL_Uniform1fFunc (sd_nearLoc, NEARCLIP);
	GL_Uniform1fFunc (sd_farLoc, gl_farclip.value);
	R_SSAO_DrawQuad ();
	GL_UseProgramFunc (0);

	// pass 2: noisy AO -> ao_tex
	glBindFramebuffer (GL_FRAMEBUFFER, ao_fbo);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ao_tex, 0);
	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		glBindFramebuffer (GL_FRAMEBUFFER, 0);
		glEnable (GL_DEPTH_TEST);
		GL_ClearBindings ();
		return;
	}
	glViewport (0, 0, dstw, dsth);
	GL_SelectTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, sdepth_tex);
	GL_SelectTexture (GL_TEXTURE1);
	glEnable (GL_TEXTURE_2D);
	glBindTexture (GL_TEXTURE_2D, ssaa_color);
	GL_SelectTexture (GL_TEXTURE0);

	GL_UseProgramFunc (ssao_program);
	GL_Uniform1iFunc (ssao_depthLoc, 0);
	GL_Uniform1iFunc (ssao_colorLoc, 1);
	GL_Uniform1fFunc (ssao_farLoc, gl_farclip.value);
	GL_Uniform1fFunc (ssao_strLoc, r_ssao.value);
	GL_Uniform1fFunc (ssao_tanxLoc, tanx);
	GL_Uniform1fFunc (ssao_tanyLoc, tany);
	GL_Uniform1fFunc (ssao_texelXLoc, 1.0f / (float)dstw);
	GL_Uniform1fFunc (ssao_texelYLoc, 1.0f / (float)dsth);
	GL_Uniform1fFunc (ssao_radiusLoc, 24.0f);
	GL_Uniform1fFunc (ssao_biasLoc, 4.0f);	// larger bias suppresses grazing-angle self-occlusion streaks
	R_SSAO_DrawQuad ();
	GL_UseProgramFunc (0);

	GL_SelectTexture (GL_TEXTURE1);
	glDisable (GL_TEXTURE_2D);
	glBindTexture (GL_TEXTURE_2D, 0);
	GL_SelectTexture (GL_TEXTURE0);

	// pass 3: bilateral blur ao_tex -> screen (multiply)
	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	glViewport (dstx, dsty, dstw, dsth);
	GL_SelectTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, ao_tex);
	GL_SelectTexture (GL_TEXTURE1);
	glEnable (GL_TEXTURE_2D);
	glBindTexture (GL_TEXTURE_2D, sdepth_tex);
	GL_SelectTexture (GL_TEXTURE0);
	glEnable (GL_BLEND);
	glBlendFunc (GL_ZERO, GL_SRC_COLOR);
	glColor4f (1, 1, 1, 1);
	GL_UseProgramFunc (blur_program);
	GL_Uniform1iFunc (blur_aoLoc, 0);
	GL_Uniform1iFunc (blur_depthLoc, 1);
	GL_Uniform1fFunc (blur_farLoc, gl_farclip.value);
	GL_Uniform1fFunc (blur_texelXLoc, 1.0f / (float)dstw);
	GL_Uniform1fFunc (blur_texelYLoc, 1.0f / (float)dsth);
	R_SSAO_DrawQuad ();

	GL_UseProgramFunc (0);
	GL_SelectTexture (GL_TEXTURE1);
	glDisable (GL_TEXTURE_2D);
	glBindTexture (GL_TEXTURE_2D, 0);
	GL_SelectTexture (GL_TEXTURE0);
	glDisable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable (GL_DEPTH_TEST);
	GL_ClearBindings ();
}
#endif

void R_PostProcess (void)
{
#ifndef VITA
	if (r_ssao.value > 0.0 && r_ssaa_active > 1 && ssaa_depth != 0)
		R_SSAO ();
	if (r_cg_contrast.value != 1.0 || r_cg_saturation.value != 1.0 || r_tonemap.value > 0.0)
		R_TonemapColorGrade ();
	if (r_bloom.value)
		R_Bloom ();
	if (r_vignette.value)
		R_Vignette ();
#endif
}

/*
================
R_SSAA_Begin / R_SSAA_End -- supersampling AA (game-side).

Render the 3D scene into an off-screen FBO at r_ssaa x the display resolution,
then downsample to the screen with a linear blit (the AA resolve). Works on citron
because FBO render targets are GPU-native -- it never touches the guest->GPU texture
upload path that the lamp/vertex corruption lived in. __SWITCH__ only (uses GLAD's
FBO entry points); a no-op (r_ssaa_active stays 1) elsewhere.
================
*/
#ifdef __SWITCH__
static GLuint	ssaa_fbo;	// ssaa_depth + ssaa_color + ssaa_w/h are declared up by the velocity statics
static GLuint	r_ssaa_program = 0;
static GLint	ssaa_srcWidthLoc = -1;
static GLint	ssaa_srcHeightLoc = -1;
static GLint	ssaa_scaleLoc = -1;
static qboolean	ssaa_shader_failed = false;

void R_SSAA_DeleteFBO (void)
{
	if (ssaa_fbo)   { glDeleteFramebuffers (1, &ssaa_fbo);   ssaa_fbo = 0; }
	if (ssaa_color) { glDeleteTextures (1, &ssaa_color);     ssaa_color = 0; }
	if (ssaa_depth) { glDeleteTextures (1, &ssaa_depth);     ssaa_depth = 0; }
	ssaa_w = ssaa_h = 0;
	r_ssaa_program = 0;
	ssaa_shader_failed = false;
	R_SSAO_DeleteFBO ();
}

static void R_SSAA_CreateShaders (void)
{
	const GLchar *vertSource = \
		"#version 110\n"
		"varying vec2 tc;\n"
		"void main(void) {\n"
		"	gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);\n"
		"	tc = gl_MultiTexCoord0.xy;\n"
		"}\n";

	// box-average resolve that scales with the SSAA factor (each output pixel = mean of its
	// NxN block). Fixed 4x4 loop with step() weights = single-exit (citron chokes on early return).
	const GLchar *fragSource = \
		"#version 110\n"
		"uniform sampler2D Tex;\n"
		"uniform float srcWidth;\n"
		"uniform float srcHeight;\n"
		"uniform float ssaaScale;\n"
		"varying vec2 tc;\n"
		"\n"
		"void main(void) {\n"
		"	vec2 texel = vec2(1.0 / srcWidth, 1.0 / srcHeight);\n"
		"	vec2 base = tc - texel * (ssaaScale - 1.0) * 0.5;\n"	// centre of the top-left source texel of the NxN block
		"	vec3 sum = vec3(0.0);\n"
		"	float cnt = 0.0;\n"
		"	for (int y = 0; y < 4; y++) {\n"
		"		for (int x = 0; x < 4; x++) {\n"
		"			float inside = step(float(x) + 0.5, ssaaScale) * step(float(y) + 0.5, ssaaScale);\n"
		"			vec2 sc = base + texel * vec2(float(x), float(y));\n"
		"			sum += texture2D(Tex, sc).rgb * inside;\n"
		"			cnt += inside;\n"
		"		}\n"
		"	}\n"
		"	gl_FragColor = vec4(sum / cnt, 1.0);\n"
		"}\n";

	if (!gl_glsl_able)
	{
		ssaa_shader_failed = true;
		return;
	}

	r_ssaa_program = GL_CreateProgram (vertSource, fragSource, 0, NULL);
	if (r_ssaa_program) {
		ssaa_srcWidthLoc = GL_GetUniformLocation (&r_ssaa_program, "srcWidth");
		ssaa_srcHeightLoc = GL_GetUniformLocation (&r_ssaa_program, "srcHeight");
		ssaa_scaleLoc = GL_GetUniformLocation (&r_ssaa_program, "ssaaScale");
	} else {
		ssaa_shader_failed = true;
	}
}
#endif

void R_SSAA_Begin (void)
{
	r_ssaa_active = 1;
#ifdef __SWITCH__
	int s = (int)r_ssaa.value;
	int w, h;

	if (s < 1) s = 1;
	if (s > 4) s = 4;			// cap at 4x (16x the pixels -- beyond this is pointless)
	if (s <= 1) return;
	if (r_scale.value > 1) return;		// mutually exclusive with the pixelation downscale

	w = glwidth * s;
	h = glheight * s;
	if (w <= 0 || h <= 0) return;

	if (!ssaa_fbo)
	{
		glGenFramebuffers (1, &ssaa_fbo);
		glGenTextures (1, &ssaa_color);
		glGenTextures (1, &ssaa_depth);	// depth as a TEXTURE so the blur pass can sample it
		ssaa_w = ssaa_h = 0;
	}
	if (ssaa_w != w || ssaa_h != h)
	{
		ssaa_w = w; ssaa_h = h;
		glBindTexture (GL_TEXTURE_2D, ssaa_color);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// CLAMP_TO_EDGE: the box filter samples just past the edge at screen borders;
		// without this it wraps to the opposite side (the dark band around the viewport).
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture (GL_TEXTURE_2D, ssaa_depth);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture (GL_TEXTURE_2D, 0);
	}

	glBindFramebuffer (GL_FRAMEBUFFER, ssaa_fbo);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaa_color, 0);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ssaa_depth, 0);

	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{	// FBO unusable on this driver -> fall back to direct rendering, never crash
		glBindFramebuffer (GL_FRAMEBUFFER, 0);
		return;
	}

	r_ssaa_active = s;	// success: R_SetupGL scales the viewport, scene renders into the FBO

	// R_Clear cleared the default framebuffer, not this FBO, so clear the FBO's own
	// color+depth here (else its depth is garbage and only sky survives).
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	GL_ClearBindings ();
#endif
}

void R_SSAA_End (void)
{
#ifdef __SWITCH__
	int dstx, dsty, dstw, dsth;

	if (r_ssaa_active <= 1)
		return;

	dstx = glx + r_refdef.vrect.x;
	dsty = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	dstw = r_refdef.vrect.width;
	dsth = r_refdef.vrect.height;

	// Back to the real screen and downsample by drawing the supersampled FBO color
	// texture as a fullscreen quad. If the Lanczos shader is compiled, use it;
	// otherwise fall back to linear filtering.
	glBindFramebuffer (GL_FRAMEBUFFER, 0);

	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glEnable (GL_TEXTURE_2D);
	glBindTexture (GL_TEXTURE_2D, ssaa_color);
	glColor4f (1, 1, 1, 1);

	glViewport (dstx, dsty, dstw, dsth);
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	if (!r_ssaa_program && !ssaa_shader_failed)
	{
		R_SSAA_CreateShaders ();
	}

	if (r_ssaa_program)
	{
		GL_UseProgramFunc (r_ssaa_program);
		GL_Uniform1fFunc (ssaa_srcWidthLoc, (float)ssaa_w);
		GL_Uniform1fFunc (ssaa_srcHeightLoc, (float)ssaa_h);
		GL_Uniform1fFunc (ssaa_scaleLoc, (float)r_ssaa_active);	// N (2/3/4) -- box averages NxN
	}

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0); glVertex2f (-1, -1);
	glTexCoord2f (1, 0); glVertex2f ( 1, -1);
	glTexCoord2f (1, 1); glVertex2f ( 1,  1);
	glTexCoord2f (0, 1); glVertex2f (-1,  1);
	glEnd ();

	if (r_ssaa_program)
	{
		GL_UseProgramFunc (0);
	}

	glEnable (GL_DEPTH_TEST);
	GL_ClearBindings ();
#endif
}

/*
================
R_RenderView
================
*/
void R_RenderView (void)
{
	double	time1, time2;

	if (r_norefresh.value)
		return;

	if (!cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	time1 = 0; /* avoid compiler warning */
	if (r_speeds.value)
	{
		glFinish ();
		time1 = Sys_DoubleTime ();

		//johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_particles = rs_fogpolys = rs_megatexels =
		rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}
	else if (gl_finish.value)
		glFinish ();

	R_SetupView (); //johnfitz -- this does everything that should be done once per frame

	R_MotionBlur_NewFrame ();	// reset the per-object velocity draw list before the scene fills it

	R_SSAA_Begin ();	// SSAA: bind the supersample FBO so the scene renders at Nx

	//johnfitz -- stereo rendering -- full of hacky goodness
	if (r_stereo.value)
	{
		float eyesep = CLAMP(-8.0f, r_stereo.value, 8.0f);
		float fdepth = CLAMP(32.0f, r_stereodepth.value, 1024.0f);

		AngleVectors (r_refdef.viewangles, vpn, vright, vup);

		//render left eye (red)
		glColorMask(1, 0, 0, 1);
		VectorMA (r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = 0.5 * eyesep * NEARCLIP / fdepth;
		srand((int) (cl.time * 1000)); //sync random stuff between eyes

		R_RenderScene ();

		//render right eye (cyan)
		glClear (GL_DEPTH_BUFFER_BIT);
		glColorMask(0, 1, 1, 1);
		VectorMA (r_refdef.vieworg, 1.0f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = -frustum_skew;
		srand((int) (cl.time * 1000)); //sync random stuff between eyes

		R_RenderScene ();

		//restore
		glColorMask(1, 1, 1, 1);
		VectorMA (r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = 0.0f;
	}
	else
	{
		R_RenderScene ();
	}
	//johnfitz

	R_ScaleView ();
	R_SSAA_End ();		// SSAA: downsample the Nx FBO to the screen (the AA resolve)
#ifdef __SWITCH__
	// unified velocity-buffer motion blur: one buffer holds camera/world reprojection AND
	// per-object velocity, then a single directional blur (old whole-screen blur smeared the gun).
	R_VelocityBuffer_Build ();
	R_ObjectBlur ();
#else
	R_MotionBlur ();	// desktop: camera/world depth-reprojection blur (after the SSAA resolve)
#endif
	R_PostProcess ();	// bloom + vignette over the final 3D image (before the HUD)
	r_ssaa_active = 1;

	//johnfitz -- modified r_speeds output
	time2 = Sys_DoubleTime ();
	if (r_pos.value)
		Con_Printf ("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
			(int)cl_entities[cl.viewentity].origin[0],
			(int)cl_entities[cl.viewentity].origin[1],
			(int)cl_entities[cl.viewentity].origin[2],
			(int)cl.viewangles[PITCH],
			(int)cl.viewangles[YAW],
			(int)cl.viewangles[ROLL]);
	else if (r_speeds.value == 2)
		Con_Printf ("%3i ms  %4i/%4i wpoly %4i/%4i epoly %3i lmap %4i/%4i sky %1.1f mtex\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_brushpasses,
					rs_aliaspolys,
					rs_aliaspasses,
					rs_dynamiclightmaps,
					rs_skypolys,
					rs_skypasses,
					TexMgr_FrameUsage ());
	else if (r_speeds.value)
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_aliaspolys,
					rs_dynamiclightmaps);
	//johnfitz
}

