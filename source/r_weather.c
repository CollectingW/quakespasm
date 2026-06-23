/*
r_weather.c -- NZP rain weather (sky-gated), currently scoped to Nacht Der Untoten.

Rain only falls where the sky is actually open above: each drop traces DOWN to find
the surface it lands on, then UP from that surface to confirm the first thing above
is a SKY surface (SURF_DRAWSKY). Indoor columns (a solid ceiling above) get nothing.
This naturally handles NDU's multi-level layout -- each drop lands on its own floor.
*/

#include "quakedef.h"

cvar_t nzp_weather_ndu = {"nzp_weather_ndu", "1", CVAR_ARCHIVE};	// rain on NDU on/off
cvar_t nzp_lightning_debug = {"nzp_lightning_debug", "0", CVAR_NONE};	// >0 holds the lightning flash at this intensity (test/tune)

static qboolean	weather_active;		// true on a map we rain on (NDU)

float weather_wetness = 0.0f; // Global wetness value for the viewmodel shader

#define RAIN_MAX		6000		// max simultaneous drops
#define RAIN_RADIUS		520.0f		// horizontal spawn radius around the camera
#define RAIN_TOP		440.0f		// how far above the camera drops can start
#define RAIN_SPEED		1250.0f		// fall speed (units/sec)
#define RAIN_STREAK		14.0f		// streak length (visual)
#define SPLASH_MAX		1024		// max simultaneous floor splashes
#define SPLASH_LIFE		0.18f		// seconds a splash lasts
#define RAIN_PLACE_PER_FRAME	300		// (re)placement attempts per frame (density)
#define RAIN_TRACE_UP		8192.0f		// how far up to look for sky

typedef struct {
	vec3_t		org;		// current position (top of the streak)
	float		landz;		// Z it dies at (the surface it lands on)
	float		streak;		// per-drop streak length (varied)
	float		bright;		// per-drop brightness (varied, for depth)
	qboolean	active;
} raindrop_t;

static raindrop_t	rain[RAIN_MAX];

typedef struct {
	vec3_t		org;	// impact point on the floor
	float		born;	// cl.time when it landed
	qboolean	active;
} splash_t;

static splash_t	splash[SPLASH_MAX];
static int		splash_next;

// Batched line buffer: all rain + splash lines go into ONE glDrawArrays call
// instead of thousands of immediate-mode glBegin/glEnd batches (the citron killer).
#define WX_MAXVERTS	(RAIN_MAX*2 + SPLASH_MAX*22)
static float		wx_pos[WX_MAXVERTS*3];
static unsigned char	wx_col[WX_MAXVERTS*4];
static int		s_rain_nv = 0;	// # of rain-streak verts in wx_pos this frame (for motion blur velocity)

// expose this frame's rain-streak verts so the motion-blur velocity buffer can replay
// them with the fall velocity. Returns vert count (0 = none); *pos -> wx_pos.
int R_Weather_RainVerts (float **pos, float *fallstep3)
{
	extern double host_frametime;
	if (s_rain_nv <= 0) return 0;
	if (pos) *pos = wx_pos;
	if (fallstep3) {	// world fall velocity (matches the streak: 160,-80,RAIN_SPEED) * dt
		fallstep3[0] = 160.0f      * (float)host_frametime;
		fallstep3[1] = -80.0f      * (float)host_frametime;
		fallstep3[2] = RAIN_SPEED  * (float)host_frametime;
	}
	return s_rain_nv;
}

// lightning and thunder state variables
static float lightning_time;
static qboolean lightning_active;
static float lightning_next_strike;
static vec3_t lightning_pos;
static float thunder_time;
static vec3_t thunder_pos;
static qboolean thunder_pending;

float weather_lightning_flash = 0.0f;

static void Weather_AddSplash (float x, float y, float z)
{
	splash_t *s = &splash[splash_next];
	splash_next = (splash_next + 1) % SPLASH_MAX;
	s->org[0] = x; s->org[1] = y; s->org[2] = z;
	s->born = cl.time;
	s->active = true;
}

// NO-RAIN ZONES (NDU only): boxes where rain must never spawn (tracing can leak sky
// through a ceiling). Format: { x_min,y_min,z_min, x_max,y_max,z_max } around the room.
static float norain_boxes[][6] = {
	// Leak room #1 (jagged ceiling) -- from viewpos readings X~990-2130, Y~2310-2820,
	// eye Z ~290-310 (floor ~250). Generous box, floor to above the ceiling jags.
	{ 940, 2260, 150,   2180, 2870, 430 },
	// Leak outcut #2 ("top of map" jagged spot) -- viewpos X~1122-1286, Y~2136-2266,
	// eye Z ~281-289. Sits just below box #1's Y range.
	{ 1040, 2070, 150,   1380, 2300, 430 },
	// Leak #3 (small high spot, eye Z ~290) -- viewpos X~1011-1104, Y~1575-1640.
	{ 970, 1530, 0,   1150, 1690, 600 },
	// Leak #4 (lower area, eye Z ~143) -- viewpos X~1009-1339, Y~1192-1572.
	{ 970, 1150, 0,   1390, 1610, 600 },
};
#define NUM_NORAIN_BOXES (sizeof(norain_boxes)/sizeof(norain_boxes[0]))

static qboolean Weather_InNoRain (float x, float y, float z)
{
	int i;
	(void)z;	// XY footprint only -- the traced floorz can land outside a tight Z
			// window (roof / lower floor), which let rain leak; suppress the
			// whole column instead. (Z fields kept for documentation / future use.)
	for (i = 0; i < (int)NUM_NORAIN_BOXES; i++) {
		if (x >= norain_boxes[i][0] && y >= norain_boxes[i][1] &&
		    x <= norain_boxes[i][3] && y <= norain_boxes[i][4])
			return true;
	}
	return false;
}

/*
=================
Weather_RecursiveTrace -- find the first world surface the segment start->end
crosses; report its Z and whether it is a sky surface. Walks the BSP like
RecursiveLightPoint (gl_rlight.c).
=================
*/
static qboolean Weather_RecursiveTrace (mnode_t *node, vec3_t start, vec3_t end, float *hitz, qboolean *issky)
{
	float		front, back, frac;
	vec3_t		mid;
	int			i, ds, dt;
	msurface_t	*surf;

	if (node->contents < 0)
		return false;		// in a leaf, no surface here

	if (node->plane->type < 3)
	{
		front = start[node->plane->type] - node->plane->dist;
		back  = end[node->plane->type]   - node->plane->dist;
	}
	else
	{
		front = DotProduct (start, node->plane->normal) - node->plane->dist;
		back  = DotProduct (end,   node->plane->normal) - node->plane->dist;
	}

	if ((back < 0) == (front < 0))
		return Weather_RecursiveTrace (node->children[front < 0], start, end, hitz, issky);

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	// near side first
	if (Weather_RecursiveTrace (node->children[front < 0], start, mid, hitz, issky))
		return true;

	// check the surfaces on this node for a hit at mid
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (!surf->texinfo)
			continue;
		ds = (int) (DoublePrecisionDotProduct (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
		dt = (int) (DoublePrecisionDotProduct (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);
		if (ds < surf->texturemins[0] || dt < surf->texturemins[1])
			continue;
		ds -= surf->texturemins[0];
		dt -= surf->texturemins[1];
		if (ds > surf->extents[0] || dt > surf->extents[1])
			continue;
		*hitz  = mid[2];
		*issky = (surf->flags & SURF_DRAWSKY) ? true : false;
		return true;
	}

	// far side
	return Weather_RecursiveTrace (node->children[front >= 0], mid, end, hitz, issky);
}

static qboolean Weather_IsSkySurfaceAtPoint (vec3_t point)
{
	int i;
	msurface_t *surf;
	float d;
	float ds, dt;

	for (i = 0; i < cl.worldmodel->numsurfaces; i++)
	{
		surf = &cl.worldmodel->surfaces[i];
		if (!surf->texinfo)
			continue;
		
		// Only care about sky surfaces
		if (!(surf->flags & SURF_DRAWSKY))
			continue;

		// Check if point lies on the surface's plane
		d = DotProduct (point, surf->plane->normal) - surf->plane->dist;
		if (fabs(d) > 8.0f) // 8 units tolerance
			continue;

		// Check texture bounds
		ds = DotProduct (point, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
		dt = DotProduct (point, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];

		if (ds >= surf->texturemins[0] - 8.0f && ds <= surf->texturemins[0] + surf->extents[0] + 8.0f &&
			dt >= surf->texturemins[1] - 8.0f && dt <= surf->texturemins[1] + surf->extents[1] + 8.0f)
		{
			return true;
		}
	}
	return false;
}

// diagnostics
static int dbg_floorfail, dbg_skyfail, dbg_placed;

// (re)place a single drop in open space near the camera (trace up for sky, down for
// floor from the camera Z). Returns false if the column isn't open.
static qboolean Weather_PlaceDrop (raindrop_t *d)
{
	float	ang = (rand() & 1023) * (M_PI * 2.0f / 1024.0f);
	float	rad = RAIN_RADIUS * sqrtf ((rand() & 1023) / 1023.0f);
	float	x = r_origin[0] + cosf (ang) * rad;
	float	y = r_origin[1] + sinf (ang) * rad;
	float	z = r_origin[2];
	float	floorz, skyz, spawnz;
	vec3_t	a, b;
	trace_t trace;

	// Trace UPWARDS diagonally along the rain angle
	a[0] = x; a[1] = y; a[2] = z + 8.0f;
	b[0] = a[0] + 8192.0f * 0.128f;
	b[1] = a[1] - 8192.0f * 0.064f;
	b[2] = a[2] + 8192.0f;

	memset (&trace, 0, sizeof(trace));
	SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, a, b, &trace);

	if (trace.startsolid || trace.allsolid)
	{
		dbg_skyfail++;
		return false;
	}

	if (trace.fraction < 1.0f) {
		if (Weather_IsSkySurfaceAtPoint (trace.endpos)) {
			skyz = trace.endpos[2];
		} else {
			dbg_skyfail++;
			return false;
		}
	} else {
		skyz = b[2];
	}

	// Trace DOWNWARDS diagonally along the rain angle to find the landing floor
	vec3_t floor_b;
	float trace_dist = a[2] - (cl.worldmodel->mins[2] - 64.0f);
	floor_b[0] = a[0] - trace_dist * 0.128f;
	floor_b[1] = a[1] + trace_dist * 0.064f;
	floor_b[2] = cl.worldmodel->mins[2] - 64.0f;

	memset (&trace, 0, sizeof(trace));
	SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, a, floor_b, &trace);

	if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f) {
		dbg_floorfail++;
		return false;
	}
	floorz = trace.endpos[2];

	// Calculate spawn position at the sky height
	float dz_spawn = skyz - a[2];
	float spawn_x = a[0] + dz_spawn * 0.128f;
	float spawn_y = a[1] - dz_spawn * 0.064f;

	if (Weather_InNoRain (spawn_x, spawn_y, floorz)) {
		dbg_skyfail++;
		return false;
	}

	spawnz = floorz + RAIN_TOP + (rand() & 255);
	if (spawnz > skyz - 8.0f)
		spawnz = skyz - 8.0f;

	// Calculate the actual X and Y coordinates corresponding to spawnz height
	float dz_actual = spawnz - a[2];
	d->org[0] = a[0] + dz_actual * 0.128f;
	d->org[1] = a[1] - dz_actual * 0.064f;
	d->org[2] = spawnz;
	d->landz  = floorz;
	d->streak = 7.0f + (rand() & 1023) * (15.0f / 1023.0f);
	d->bright = 0.65f + (rand() & 1023) * (0.5f / 1023.0f);
	d->active = true;
	dbg_placed++;
	return true;
}

/*
=================
Lightning world flash -- a real sky-cast flash instead of a fat dlight blob.

At map load we find every world surface that is OPEN TO THE SKY (trace up from the
surface, hit a sky brush) and bake its triangles into a static vertex array with a
per-vertex "brightness" = how much it faces up (ground flashes hard, walls grazing,
undersides nothing). During a strike we additively re-draw just that geometry tinted
blue-white * the flash intensity -> only sky-exposed surfaces light up, indoors stays
dark, and it reads as light coming from above. Coincident with the world (LEQUAL,
no depth write) so it sits exactly on those surfaces.
=================
*/
static float	*flash_pos = NULL;	// xyz per vert
static float	*flash_norm = NULL;	// world normal per vert (for per-pixel specular)
static int	flash_nv = 0;
static GLuint	flash_program = 0;
static GLint	flash_uFlashLoc = -1, flash_uStrikeLoc = -1, flash_uEyeLoc = -1, flash_uWetLoc = -1;
static qboolean	flash_failed = false;

static void Weather_FreeFlashGeo (void)
{
	if (flash_pos)  { free (flash_pos);  flash_pos = NULL; }
	if (flash_norm) { free (flash_norm); flash_norm = NULL; }
	flash_nv = 0;
}

static qboolean Weather_SurfExposed (msurface_t *surf, vec3_t n, vec3_t center)
{
	vec3_t a, b;
	trace_t tr;
	a[0] = center[0] + n[0]*4.0f;
	a[1] = center[1] + n[1]*4.0f;
	a[2] = center[2] + n[2]*4.0f + 2.0f;
	b[0] = a[0] + 8192.0f * 0.128f;	// same diagonal as the rain / sky trace
	b[1] = a[1] - 8192.0f * 0.064f;
	b[2] = a[2] + 8192.0f;
	memset (&tr, 0, sizeof(tr));
	SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, a, b, &tr);
	if (tr.startsolid || tr.allsolid)
		return false;
	if (tr.fraction < 1.0f)
		return Weather_IsSkySurfaceAtPoint (tr.endpos);
	return true;	// trace reached the sky box -> open
}

static void Weather_BuildFlashGeo (void)
{
	int s, pass, i, v, count = 0;

	Weather_FreeFlashGeo ();
	if (!weather_active || !cl.worldmodel)
		return;

	for (pass = 0; pass < 2; pass++)
	{
		flash_nv = 0;
		if (pass == 1)
		{
			if (count == 0) return;
			flash_pos  = (float *) malloc (sizeof(float) * 3 * count);
			flash_norm = (float *) malloc (sizeof(float) * 3 * count);
			if (!flash_pos || !flash_norm) { Weather_FreeFlashGeo (); return; }
		}
		for (s = 0; s < cl.worldmodel->numsurfaces; s++)
		{
			msurface_t *surf = &cl.worldmodel->surfaces[s];
			glpoly_t *p = surf->polys;
			vec3_t n, center;

			if (!surf->texinfo || !p || p->numverts < 3)
				continue;
			if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
				continue;

			n[0] = surf->plane->normal[0]; n[1] = surf->plane->normal[1]; n[2] = surf->plane->normal[2];
			if (surf->flags & SURF_PLANEBACK) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
			if (n[2] < -0.1f)	// down-facing (undersides) never see the sky
				continue;

			center[0] = center[1] = center[2] = 0;
			for (v = 0; v < p->numverts; v++)
			{ center[0]+=p->verts[v][0]; center[1]+=p->verts[v][1]; center[2]+=p->verts[v][2]; }
			center[0]/=p->numverts; center[1]/=p->numverts; center[2]/=p->numverts;

			if (Weather_InNoRain (center[0], center[1], center[2]))
				continue;	// manually-gated interior leak (jagged ceilings) -> no flash
			if (!Weather_SurfExposed (surf, n, center))
				continue;

			for (i = 1; i < p->numverts - 1; i++)
			{
				int idx[3], k;
				idx[0] = 0; idx[1] = i; idx[2] = i + 1;
				for (k = 0; k < 3; k++)
				{
					if (pass == 0) { count++; flash_nv++; continue; }
					flash_pos[flash_nv*3+0] = p->verts[idx[k]][0];
					flash_pos[flash_nv*3+1] = p->verts[idx[k]][1];
					flash_pos[flash_nv*3+2] = p->verts[idx[k]][2];
					flash_norm[flash_nv*3+0] = n[0];
					flash_norm[flash_nv*3+1] = n[1];
					flash_norm[flash_nv*3+2] = n[2];
					flash_nv++;
				}
			}
		}
	}
}

static void Weather_CreateFlashShader (void)
{
	// per-pixel specular sheen: wet surfaces glint from the bolt's sky position with the
	// highlight tracking your view; a distance falloff keeps the flash centred on the strike.
	const GLchar *vert =
		"#version 110\n"
		"attribute vec3 Nrm;\n"
		"varying vec3 vW;\n"		// world pos
		"varying vec3 vN;\n"		// world normal
		"void main(void){ gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; vW = gl_Vertex.xyz; vN = Nrm; }\n";
	const GLchar *frag =
		"#version 110\n"
		"uniform float flash;\n"
		"uniform vec3 strike;\n"	// bolt position (sky)
		"uniform vec3 eye;\n"		// camera position
		"uniform float wet;\n"		// 0..1 surface wetness
		"varying vec3 vW;\n"
		"varying vec3 vN;\n"
		"void main(void){\n"
		"	vec3 N = normalize(vN);\n"
		"	vec3 L = normalize(strike - vW);\n"		// toward the bolt
		"	vec3 V = normalize(eye - vW);\n"
		"	vec3 H = normalize(L + V);\n"
		"	float diff = max(0.0, dot(N, L));\n"
		"	float shin = mix(20.0, 90.0, wet);\n"		// wet -> tighter glint
		"	float spec = pow(max(0.0, dot(N, H)), shin) * (0.5 + 1.3 * wet);\n"
		"	float d = distance(vW, strike);\n"
		"	float fall = clamp(1.0 - d / 3000.0, 0.0, 1.0); fall *= fall;\n"	// centred on the strike
		"	vec3 col = vec3(0.60, 0.72, 1.0) * (flash * fall * (diff * 0.22 + spec));\n"
		"	gl_FragColor = vec4(col, 1.0);\n"
		"}\n";
	static const glsl_attrib_binding_t bind[] = { {"Nrm", 1} };

	if (!gl_glsl_able) { flash_failed = true; return; }
	flash_program = GL_CreateProgram (vert, frag, 1, bind);
	if (flash_program) {
		flash_uFlashLoc  = GL_GetUniformLocation (&flash_program, "flash");
		flash_uStrikeLoc = GL_GetUniformLocation (&flash_program, "strike");
		flash_uEyeLoc    = GL_GetUniformLocation (&flash_program, "eye");
		flash_uWetLoc    = GL_GetUniformLocation (&flash_program, "wet");
	} else
		flash_failed = true;
}

static void Weather_DrawLightningFlash (void)
{
	if (weather_lightning_flash <= 0.0f || flash_nv <= 0)
		return;
	if (!flash_program && !flash_failed)
		Weather_CreateFlashShader ();
	if (!flash_program)
		return;

	GL_DisableMultitexture ();
	glDisable (GL_TEXTURE_2D);
	glDisable (GL_CULL_FACE);
	glEnable (GL_DEPTH_TEST);
	glDepthMask (GL_FALSE);
	glDepthFunc (GL_LEQUAL);	// coincident with the world surfaces -> equal depth passes
	glEnable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE);	// additive sky-light

	GL_UseProgramFunc (flash_program);
	GL_Uniform1fFunc (flash_uFlashLoc, weather_lightning_flash);
	GL_Uniform3fFunc (flash_uStrikeLoc, lightning_pos[0], lightning_pos[1], lightning_pos[2]);
	GL_Uniform3fFunc (flash_uEyeLoc, r_origin[0], r_origin[1], r_origin[2]);
	GL_Uniform1fFunc (flash_uWetLoc, weather_wetness);

	glEnableClientState (GL_VERTEX_ARRAY);
	glVertexPointer (3, GL_FLOAT, 0, flash_pos);
	GL_EnableVertexAttribArrayFunc (1);
	GL_VertexAttribPointerFunc (1, 3, GL_FLOAT, GL_FALSE, 0, flash_norm);

	glDrawArrays (GL_TRIANGLES, 0, flash_nv);

	GL_DisableVertexAttribArrayFunc (1);
	glDisableClientState (GL_VERTEX_ARRAY);
	GL_UseProgramFunc (0);

	glDepthMask (GL_TRUE);
	glDisable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable (GL_TEXTURE_2D);
}

/*
=================
Weather_NewMap -- called from R_NewMap. Decide if this map rains.
=================
*/
void Weather_NewMap (void)
{
	int i;
	const char *name = cl.worldmodel ? cl.worldmodel->name : "";

	weather_active = (strstr (name, "ndu.bsp") != NULL) && (strstr (name, "ndu_cam") == NULL);

	for (i = 0; i < RAIN_MAX; i++)
		rain[i].active = false;
	for (i = 0; i < SPLASH_MAX; i++)
		splash[i].active = false;

	lightning_next_strike = cl.time + 15.0f + (rand() % 15);
	lightning_active = false;
	weather_lightning_flash = 0.0f;
	thunder_pending = false;

	Weather_BuildFlashGeo ();	// bake the sky-exposed surface geometry for the lightning flash
}

void Weather_Init (void)
{
	Cvar_RegisterVariable (&nzp_weather_ndu);
	Cvar_RegisterVariable (&nzp_lightning_debug);
}

/*
=================
R_DrawWeather -- called from R_RenderScene after the world/particles.
=================
*/
void R_DrawWeather (void)
{
	int		i, attempts;
	float	dt = (float) host_frametime;
	raindrop_t *d;

	if (cl.paused)
		return;

	if (dt <= 0.0f)
		dt = 0.0f;

	// Update wetness
	if (cl.stats[STAT_HEALTH] <= 0)
	{
		weather_wetness = 0.0f;
	}
	else if (weather_active && nzp_weather_ndu.value && cl.worldmodel)
	{
		extern qboolean Weather_PointExposedToSky (vec3_t pos);
		if (Weather_PointExposedToSky (r_origin))
		{
			// Standing in the rain: gets wet
			weather_wetness += dt * 0.25f; // 4 seconds to get fully wet
			if (weather_wetness > 1.0f)
				weather_wetness = 1.0f;
		}
		else
		{
			// Standing indoors: dries up
			weather_wetness -= dt * 0.16f; // 6 seconds to dry up
			if (weather_wetness < 0.0f)
				weather_wetness = 0.0f;
		}
	}
	else
	{
		// No active weather: dries up
		weather_wetness -= dt * 0.16f; // 6 seconds to dry up
		if (weather_wetness < 0.0f)
			weather_wetness = 0.0f;
	}

	if (!weather_active || !nzp_weather_ndu.value || !cl.worldmodel)
	{
		s_rain_nv = 0;	// no rain this frame -> nothing for the velocity buffer
		return;
	}

	// update lightning strike timers
	if (cl.time >= lightning_next_strike && !lightning_active)
	{
		lightning_active = true;
		lightning_time = cl.time;
		
		// Position the lightning dlight high in the sky, somewhere near the player
		float ang = (rand() & 1023) * (M_PI * 2.0f / 1024.0f);
		float rad = 300.0f + (rand() % 600); // 300 to 900 units away
		float x = r_origin[0] + cosf(ang) * rad;
		float y = r_origin[1] + sinf(ang) * rad;
		float z = r_origin[2];
		float skyz, floorz;
		qboolean issky;
		vec3_t a, b;
		
		a[0] = x; a[1] = y; a[2] = z + 8.0f;
		b[0] = x; b[1] = y; b[2] = z + RAIN_TRACE_UP;
		if (Weather_RecursiveTrace(cl.worldmodel->nodes, a, b, &skyz, &issky) && issky) {
			lightning_pos[0] = x;
			lightning_pos[1] = y;
			lightning_pos[2] = skyz - 30.0f;
		} else {
			lightning_pos[0] = r_origin[0];
			lightning_pos[1] = r_origin[1];
			lightning_pos[2] = r_origin[2] + 400.0f;
		}
		
		// Play the thunder sound after a realistic delay (1.0 to 2.5 seconds)
		thunder_time = cl.time + 1.0f + (rand() % 1500) * 0.001f;
		VectorCopy(lightning_pos, thunder_pos);
		thunder_pending = true;
		
		// Schedule next lightning strike in 20-45 seconds
		lightning_next_strike = cl.time + 20.0f + (rand() % 25);
	}

	// Update lightning flash intensity profile
	if (lightning_active)
	{
		float age = cl.time - lightning_time;
		if (age >= 0.5f)
		{
			lightning_active = false;
			weather_lightning_flash = 0.0f;
		}
		else
		{
			// Double flash intensity profile
			if (age < 0.05f) {
				weather_lightning_flash = age / 0.05f;
			} else if (age < 0.12f) {
				weather_lightning_flash = 1.0f - (age - 0.05f) / 0.07f * 0.8f; // fade to 0.2
			} else if (age < 0.18f) {
				float t = (age - 0.12f) / 0.06f;
				weather_lightning_flash = 0.2f + t * 0.7f; // flash again up to 0.9
			} else {
				float t = (age - 0.18f) / 0.32f;
				weather_lightning_flash = 0.9f * (1.0f - t); // slow fade to 0
			}
		}
	}
	else
	{
		weather_lightning_flash = 0.0f;
	}

	// Debug: hold the flash on at a fixed intensity so we can see/tune it (strikes are rare).
	// Also parks the strike position overhead so the sheen has a sensible direction.
	if (nzp_lightning_debug.value > 0.0f)
	{
		weather_lightning_flash = nzp_lightning_debug.value;
		if (lightning_pos[2] < r_origin[2])
		{ lightning_pos[0] = r_origin[0]; lightning_pos[1] = r_origin[1]; lightning_pos[2] = r_origin[2] + 600.0f; }
	}

	// (Per-surface world flash retired -- it looked splotchy/low-res. The full-screen flash
	// in V_PolyBlend now lights the whole scene, gated to actual open-sky exposure.)
	// Weather_DrawLightningFlash ();

	// Play thunder sound if pending and delay has elapsed
	if (thunder_pending && cl.time >= thunder_time)
	{
		thunder_pending = false;
		extern sfx_t *cl_sfx_r_exp3;
		if (cl_sfx_r_exp3)
		{
			// Play with 0.0f attenuation so it is global and heard everywhere at full volume, panned towards strike
			S_StartSound (-1, 0, cl_sfx_r_exp3, thunder_pos, 0.8f, 0.0f);
		}
	}

	// update / respawn (re-trace only on (re)placement, not every frame)
	attempts = 0;
	for (i = 0; i < RAIN_MAX; i++)
	{
		d = &rain[i];
		if (d->active)
		{
			d->org[2] -= RAIN_SPEED * dt;
			d->org[0] -= 160.0f * dt; // steady wind in X
			d->org[1] += 80.0f * dt;  // steady wind in Y
			if (d->org[2] <= d->landz)
			{
				if ((rand() & 3) == 0)	// splash on ~1 in 4 landings
					Weather_AddSplash (d->org[0], d->org[1], d->landz);
				d->active = false;
			}
		}
		if (!d->active && attempts < RAIN_PLACE_PER_FRAME)
		{
			attempts++;
			Weather_PlaceDrop (d);		// may fail (closed column) -> stays inactive
		}
	}

	(void)dbg_placed; (void)dbg_floorfail;	// (counters kept for future debugging)

	// batch every rain + splash line into one vertex/color array and draw with a single
	// glDrawArrays(GL_LINES) (thousands of glBegin/glEnd batches froze citron).
	{
		int nv = 0;
		float *pp = wx_pos;
		unsigned char *cp = wx_col;
		#define WX_PUSH(X,Y,Z,R,G,B,A) do { \
			if (nv < WX_MAXVERTS) { \
				pp[0]=(X); pp[1]=(Y); pp[2]=(Z); pp+=3; \
				cp[0]=(R); cp[1]=(G); cp[2]=(B); cp[3]=(A); cp+=4; nv++; } \
		} while(0)

		// rain streaks
		for (i = 0; i < RAIN_MAX; i++)
		{
			unsigned char rr, gg, bb;
			float x2, y2, z2;
			d = &rain[i];
			if (!d->active)
				continue;
			rr = (unsigned char)(0.62f * d->bright * 255.0f);
			gg = (unsigned char)(0.70f * d->bright * 255.0f);
			bb = (unsigned char)(0.85f * d->bright * 255.0f);
			x2 = d->org[0] + 160.0f * (d->streak / RAIN_SPEED);
			y2 = d->org[1] -  80.0f * (d->streak / RAIN_SPEED);
			z2 = d->org[2] + d->streak;
			WX_PUSH (d->org[0], d->org[1], d->org[2], rr, gg, bb, 107);
			WX_PUSH (x2, y2, z2,                      rr, gg, bb, 107);
		}

		s_rain_nv = nv;	// rain streaks are the first nv verts; splashes follow (excluded from blur)

		// floor splashes (ring as line pairs + rising crown ticks)
		for (i = 0; i < SPLASH_MAX; i++)
		{
			splash_t *s = &splash[i];
			float t, r, h, dr, a;
			float px[8], py[8];
			unsigned char a1, a2;
			int k;
			if (!s->active)
				continue;
			t = (cl.time - s->born) / SPLASH_LIFE;
			if (t >= 1.0f) { s->active = false; continue; }
			a  = (1.0f - t) * 0.45f;
			a1 = (unsigned char)(a * 255.0f);
			a2 = (unsigned char)((a * 1.2f > 1.0f ? 1.0f : a * 1.2f) * 255.0f);
			r  = 2.0f + t * 9.0f;
			for (k = 0; k < 8; k++) {
				float ang = k * (M_PI * 0.25f);
				px[k] = s->org[0] + cosf(ang) * r;
				py[k] = s->org[1] + sinf(ang) * r;
			}
			for (k = 0; k < 8; k++) {
				int k2 = (k + 1) & 7;
				WX_PUSH (px[k],  py[k],  s->org[2]+0.5f, 179, 199, 235, a1);
				WX_PUSH (px[k2], py[k2], s->org[2]+0.5f, 179, 199, 235, a1);
			}
			h  = (1.0f - t) * 5.0f;
			dr = 1.0f + t * 4.0f;
			for (k = 0; k < 3; k++) {
				float ang = k * (M_PI * 2.0f / 3.0f) + (s->org[0] * 0.05f);
				float dx = cosf(ang) * dr, dy = sinf(ang) * dr;
				WX_PUSH (s->org[0]+dx*0.5f, s->org[1]+dy*0.5f, s->org[2]+0.5f, 179, 199, 235, a2);
				WX_PUSH (s->org[0]+dx,      s->org[1]+dy,      s->org[2]+h,    179, 199, 235, a2);
			}
		}
		#undef WX_PUSH

		if (nv > 0)
		{
			extern int r_ssaa_active;	// scale width by SSAA factor so streaks survive the downsample
			float lw = 1.5f * (r_ssaa_active > 0 ? r_ssaa_active : 1);
			glDisable (GL_TEXTURE_2D);
			glEnable (GL_BLEND);
			glDepthMask (GL_FALSE);
			glLineWidth (lw);
			glEnableClientState (GL_VERTEX_ARRAY);
			glEnableClientState (GL_COLOR_ARRAY);
			glVertexPointer (3, GL_FLOAT, 0, wx_pos);
			glColorPointer (4, GL_UNSIGNED_BYTE, 0, wx_col);
			glDrawArrays (GL_LINES, 0, nv);
			glDisableClientState (GL_COLOR_ARRAY);
			glDisableClientState (GL_VERTEX_ARRAY);
			glLineWidth (1.0f);
			glColor4f (1, 1, 1, 1);
			glDepthMask (GL_TRUE);
			glDisable (GL_BLEND);
			glEnable (GL_TEXTURE_2D);
		}
	}
}

qboolean Weather_PointExposedToSky (vec3_t pos)
{
	if (!weather_active || !nzp_weather_ndu.value || !cl.worldmodel)
		return false;
	if (Weather_InNoRain (pos[0], pos[1], pos[2]))
		return false;	// manually-gated interior leak (jagged ceilings) -> not exposed

	vec3_t a, b;
	trace_t trace;

	a[0] = pos[0];
	a[1] = pos[1];
	a[2] = pos[2] + 2.0f;
	b[0] = a[0] + 8192.0f * 0.128f;
	b[1] = a[1] - 8192.0f * 0.064f;
	b[2] = a[2] + 8192.0f;

	memset (&trace, 0, sizeof(trace));
	SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, a, b, &trace);

	if (trace.startsolid || trace.allsolid)
		return false;

	if (trace.fraction < 1.0f)
	{
		if (Weather_IsSkySurfaceAtPoint (trace.endpos))
			return true;
		return false;
	}

	return true;
}

/*
=================
Weather_PointUnderOpenSky -- "is there open sky DIRECTLY above this point?" (straight-up
trace). Gates the lightning flash: the diagonal rain-angle test slips out a side gap in a
big covered room and false-positives, so the flash leaked into roofed rooms. A vertical
trace cleanly answers "is there a roof over me" -- exposed only if it reaches a sky surface
(or open space), not a solid ceiling.
=================
*/
qboolean Weather_PointUnderOpenSky (vec3_t pos)
{
	vec3_t a, b;
	trace_t trace;

	if (!weather_active || !nzp_weather_ndu.value || !cl.worldmodel)
		return false;
	if (Weather_InNoRain (pos[0], pos[1], pos[2]))
		return false;

	a[0] = pos[0]; a[1] = pos[1]; a[2] = pos[2] + 2.0f;
	b[0] = pos[0]; b[1] = pos[1]; b[2] = pos[2] + 8192.0f;	// STRAIGHT up

	memset (&trace, 0, sizeof(trace));
	SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, a, b, &trace);

	if (trace.startsolid || trace.allsolid)
		return false;
	if (trace.fraction < 1.0f)
		return Weather_IsSkySurfaceAtPoint (trace.endpos);	// hit a roof -> exposed only if it's sky
	return true;	// nothing overhead -> open
}

/*
=================
Weather_DrawLightningScreenFlash -- clean full-screen lightning flash (replaces the splotchy
per-surface paint). Brief additive blue-white over the whole frame so the scene AND the
player light up like real lightning -- but ONLY when actually out under open sky. Called at
the very end of the frame (from V_PolyBlend).
=================
*/
void Weather_DrawLightningScreenFlash (void)
{
	float in;

	if (weather_lightning_flash <= 0.0f)
		return;
	if (!Weather_PointUnderOpenSky (r_origin))
		return;	// under a roof -> no flash (fixes the covered PaP-room leak)

	in = weather_lightning_flash * 0.6f;	// scene brighten amount
	if (in > 0.85f) in = 0.85f;

	GL_UseProgramFunc (0);
	GL_DisableMultitexture ();
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_TEXTURE_2D);
	glDisable (GL_DEPTH_TEST);
	glEnable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE);	// additive -> brightens

	glMatrixMode (GL_PROJECTION);
	glPushMatrix ();
	glLoadIdentity ();
	glOrtho (0, 1, 1, 0, -99999, 99999);
	glMatrixMode (GL_MODELVIEW);
	glPushMatrix ();
	glLoadIdentity ();

	glColor4f (0.55f * in, 0.66f * in, 0.85f * in, 1.0f);	// cool blue-white
	glBegin (GL_QUADS);
	glVertex2f (0, 0);
	glVertex2f (1, 0);
	glVertex2f (1, 1);
	glVertex2f (0, 1);
	glEnd ();

	glMatrixMode (GL_PROJECTION);
	glPopMatrix ();
	glMatrixMode (GL_MODELVIEW);
	glPopMatrix ();

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f (1, 1, 1, 1);
	glDisable (GL_BLEND);
	glEnable (GL_DEPTH_TEST);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_ALPHA_TEST);
}

qboolean Weather_PointExposedToSkyStatic (vec3_t pos)
{
	if (!cl.worldmodel)
		return false;

	vec3_t a, b;
	trace_t trace;

	a[0] = pos[0];
	a[1] = pos[1];
	a[2] = pos[2] + 2.0f;
	b[0] = a[0] + 8192.0f * 0.128f;
	b[1] = a[1] - 8192.0f * 0.064f;
	b[2] = a[2] + 8192.0f;

	memset (&trace, 0, sizeof(trace));
	SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, a, b, &trace);

	if (trace.startsolid || trace.allsolid)
		return false;

	if (trace.fraction < 1.0f)
	{
		if (Weather_IsSkySurfaceAtPoint (trace.endpos))
			return true;
		return false;
	}

	return true;
}
