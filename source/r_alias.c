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

// r_alias.c -- alias model rendering

#include "quakedef.h"

extern cvar_t r_drawflat, gl_overbright_models, gl_fullbrights, r_lerpmodels,
    r_lerpmove;                  // johnfitz
extern cvar_t scr_fov_viewmodel; // sB porting seperate viewmodel FOV
extern cvar_t scr_fov;

// per-object motion blur: the velocity pass (gl_rmain.c) re-draws each alias model into a
// velocity buffer using this + last frame's transform/pose; the normal draw records it here.
extern cvar_t gl_motionblur;
extern int    r_framecount;
void R_MotionBlur_PushAlias (unsigned int vbo, unsigned int ebo, int numindexes,
                             int indexofs, int stride, const void *p1n,
                             const void *p2n, const void *p1p, const void *p2p,
                             float blendN, float blendP, const float *mvpN,
                             const float *mvpP, int viewmodel, int zombie);

extern float fog_red;
extern float fog_green;
extern float fog_blue;

#include "mathlib.h"

// up to 16 color translated skins
gltexture_t *playertextures[MAX_SCOREBOARD]; // johnfitz -- changed to an array
                                             // of pointers

#define NUMVERTEXNORMALS 162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

extern vec3_t
    lightcolor; // johnfitz -- replaces "float shadelight" for lit support

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float r_avertexnormal_dots[SHADEDOT_QUANT][256] = {
#include "anorm_dots.h"
};

extern vec3_t lightspot;

float *shadedots = r_avertexnormal_dots[0];
vec3_t shadevector;
vec3_t alias_spec_lightview; // world light dir expressed in VIEW space
                             // (for specular L)

// derive a real world-space light direction for models from the baked lightmap
// (instead of the old yaw-faked vector) so diffuse + specular react to real light. Live tuning:
cvar_t r_modeldirlight = {"r_modeldirlight", "1",
                          CVAR_ARCHIVE}; // master on/off
cvar_t r_modeldirtilt = {"r_modeldirtilt", "2.5",
                         CVAR_ARCHIVE}; // up-bias (higher = more top-down)
cvar_t r_modeldirsmooth = {
    "r_modeldirsmooth", "0.12",
    CVAR_ARCHIVE}; // per-ent time-smoothing (s); 0 = instant. kills
                   // lightstyle/boundary snap
cvar_t r_modelspecworld = {
    "r_modelspecworld", "0.0",
    CVAR_ARCHIVE}; // gloss key light: 0 = sky-up, 1 = full world light dir
cvar_t r_modelspecfwd = {
    "r_modelspecfwd", "0.5",
    CVAR_ARCHIVE}; // push the 'sun' forward toward the muzzle (0 = straight up)

extern int zombie_skins[2][2];

float entalpha; // johnfitz

qboolean overbright; // johnfitz

qboolean shading =
    true; // johnfitz -- if false, disable vertex shading for various reasons
          // (fullbright, r_lightmap, showtris, etc)

// johnfitz -- struct for passing lerp information to drawing functions
typedef struct {
  short pose1;
  short pose2;
  float blend;
  vec3_t origin;
  vec3_t angles;
} lerpdata_t;
// johnfitz

static GLuint r_alias_program;
static GLuint r_aliasshadow_program;	// minimal flatten-to-floor silhouette shadow
static GLint  shadow_blendLoc = -1, shadow_alphaLoc = -1;

// uniforms used in vert shader
static GLuint blendLoc;
static GLuint shadevectorLoc;
static GLuint lightColorLoc;

// uniforms used in frag shader
static GLuint texLoc;
static GLuint fullbrightTexLoc;
static GLuint useFullbrightTexLoc;
static GLuint fullbrightPulseLoc;
static GLuint zombieEyeColorLoc;
static GLuint fbRecolorLoc;
static GLuint useOverbrightLoc;
static GLuint useAlphaTestLoc;
static GLuint aliasgrayscale_enableLoc;
static GLuint fogDensityLoc;
static GLuint fogRedLoc;
static GLuint fogGreenLoc;
static GLuint fogBlueLoc;
// specular lighting
static GLuint useSpecTexLoc;
static GLuint specLightViewLoc;
static GLuint wetnessLoc;
static GLuint timeLoc;
static GLuint flashLoc;	// lightning flash intensity on this model

#define pose1VertexAttrIndex 0
#define pose1NormalAttrIndex 1
#define pose2VertexAttrIndex 2
#define pose2NormalAttrIndex 3
#define texCoordsAttrIndex 4

/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static void *GLARB_GetXYZOffset(aliashdr_t *hdr, int pose) {
  const int xyzoffs = offsetof(meshxyz_t, xyz);
  return (void *)(currententity->model->vboxyzofs +
                  (hdr->numverts_vbo * pose * sizeof(meshxyz_t)) + xyzoffs);
}

// column-major 4x4 multiply (out = a*b), matching gl_rmain.c's Matrix4_Mult.
// Used by the per-object motion-blur capture to build each model's view-proj.
static void MB_Mat4Mult(float *out, const float *a, const float *b) {
  int c, r, k;
  for (c = 0; c < 4; c++)
    for (r = 0; r < 4; r++) {
      float s = 0.0f;
      for (k = 0; k < 4; k++)
        s += a[k * 4 + r] * b[c * 4 + k];
      out[c * 4 + r] = s;
    }
}

/*
=============
GLARB_GetNormalOffset

Returns the offset of the first vertex's meshxyz_t.normal in the vbo for the
given model and pose.
=============
*/
static void *GLARB_GetNormalOffset(aliashdr_t *hdr, int pose) {
  const int normaloffs = offsetof(meshxyz_t, normal);
  return (void *)(currententity->model->vboxyzofs +
                  (hdr->numverts_vbo * pose * sizeof(meshxyz_t)) + normaloffs);
}

/*
=============
GLAlias_CreateShaders
=============
*/
void GLAlias_CreateShaders(void) {
  const glsl_attrib_binding_t bindings[] = {
      {"TexCoords", texCoordsAttrIndex},
      {"Pose1Vert", pose1VertexAttrIndex},
      {"Pose1Normal", pose1NormalAttrIndex},
      {"Pose2Vert", pose2VertexAttrIndex},
      {"Pose2Normal", pose2NormalAttrIndex}};
#ifdef VITA
  const GLchar *vertSource =
      "uniform float Blend;\n"
      "uniform float3 ShadeVector;\n"
      "uniform float4 LightColor;\n"
      "uniform float4x4 gl_ModelViewProjectionMatrix;\n"
      "\n"
      "float r_avertexnormal_dot(float3 vertexnormal) // from MH \n"
      "{\n"
      "        float _dot = dot(vertexnormal, ShadeVector);\n"
      "        // wtf - this reproduces anorm_dots within as reasonable a "
      "degree of tolerance as the >= 0 case\n"
      "        if (_dot < 0.0)\n"
      "            return 1.0 + _dot * (13.0 / 44.0);\n"
      "        else\n"
      "            return 1.0 + _dot;\n"
      "}\n"
      "void main(\n"
      "	float4 TexCoords,\n"
      "	float4 Pose1Vert,\n"
      "	float3 Pose1Normal,\n"
      "	float4 Pose2Vert,\n"
      "	float3 Pose2Normal,\n"
      "	float2 out gl_TexCoord : TEXCOORD0,\n"
      "	float4 out gl_Position : POSITION,\n"
      "	float4 out gl_FrontColor : COLOR\n"
      ") {\n"
      "	gl_TexCoord = TexCoords.xy;\n"
      "	float4 lerpedVert = lerp(float4(Pose1Vert.xyz, 1.0), "
      "float4(Pose2Vert.xyz, 1.0), Blend);\n"
      "	gl_Position = mul(gl_ModelViewProjectionMatrix, lerpedVert);\n"
      "	float dot1 = r_avertexnormal_dot(Pose1Normal);\n"
      "	float dot2 = r_avertexnormal_dot(Pose2Normal);\n"
      "	gl_FrontColor = LightColor * float4(float3(lerp(dot1, dot2, Blend)), "
      "1.0);\n"
      "}\n";

  const GLchar *fragSource =
      "uniform sampler2D Tex;\n"
      "uniform sampler2D FullbrightTex;\n"
      "uniform int UseFullbrightTex;\n"
      "uniform int UseOverbright;\n"
      "uniform int UseAlphaTest;\n"
      "uniform int gs_mod;\n"
      "uniform float fog_density;\n"
      "uniform float fog_red;\n"
      "uniform float fog_green;\n"
      "uniform float fog_blue;\n"
      "uniform float Wetness;\n"
      "uniform float Time;\n"
      "\n"
      "float4 main(\n"
      "	float4 coords : WPOS,\n"
      "	float2 gl_TexCoord : TEXCOORD0,\n"
      "	float4 gl_Color : COLOR\n"
      ") {\n"
      "	float4 result = tex2D(Tex, gl_TexCoord);\n"
      "	if (UseAlphaTest && (result.a < 0.666))\n"
      "		discard;\n"
      "	result *= gl_Color;\n"
      "	if (UseOverbright)\n"
      "		result.rgb *= 2.0;\n"
      "	if (UseFullbrightTex)\n"
      "		result += tex2D(FullbrightTex, gl_TexCoord);\n"
      "	result = clamp(result, 0.0, 1.0);\n"
      "	float FogFragCoord = coords.z / coords.w;\n"
      "	float fog = exp(-fog_density * fog_density * FogFragCoord * "
      "FogFragCoord);\n"
      "	fog = clamp(fog, 0.0, 1.0);\n"
      "	result = lerp(float4(fog_red, fog_green, fog_blue, 1.0), result, "
      "fog);\n"
      "	result.a = gl_Color.a;\n" // FIXME: This will make almost transparent
                                  // things cut holes though heavy fog
      "   if (gs_mod) {\n"
      "       float value = clamp((result.r * 0.33) + (result.g * 0.55) + "
      "(result.b * 0.11), 0.0, 1.0);\n"
      "       result.r = value;\n"
      "       result.g = value;\n"
      "       result.b = value;\n"
      "   }"
      "	return result;\n"
      "}\n";
#else
  const GLchar *vertSource =
      "#version 110\n"
      "\n"
      "uniform float Blend;\n"
      "uniform vec3 ShadeVector;\n"
      "uniform vec4 LightColor;\n"
      "attribute vec4 TexCoords; // only xy are used \n"
      "attribute vec4 Pose1Vert;\n"
      "attribute vec3 Pose1Normal;\n"
      "attribute vec4 Pose2Vert;\n"
      "attribute vec3 Pose2Normal;\n"
      "\n"
      "varying vec2 tc;\n"
      "varying vec4 vertColor;\n"
      "varying float FogFragCoord;\n"
      "varying vec3 vModelNormal;\n"
      "varying vec3 vViewNormal;\n"
      "varying vec3 vViewPos;\n"
      "varying vec3 vLightView;\n"
      "\n"
      "float r_avertexnormal_dot(vec3 vertexnormal) // from MH \n"
      "{\n"
      "        float d = dot(vertexnormal, ShadeVector);\n"
      "        if (d < 0.0)\n"
      "            return 1.0 + d * (13.0 / 44.0);\n"
      "        else\n"
      "            return 1.0 + d;\n"
      "}\n"
      "void main()\n"
      "{\n"
      "	vec4 lerpedVert = mix(vec4(Pose1Vert.xyz, 1.0), vec4(Pose2Vert.xyz, "
      "1.0), Blend);\n"
      "	gl_Position = gl_ModelViewProjectionMatrix * lerpedVert;\n"
      "	tc = TexCoords.xy;\n"
      "	vec4 eye_pos = gl_ModelViewMatrix * lerpedVert;\n"
      "	FogFragCoord = -eye_pos.z;\n"
      "	vModelNormal = normalize(mix(Pose1Normal, Pose2Normal, Blend));\n"
      "	vViewPos = eye_pos.xyz;\n"
      "	vViewNormal = (gl_ModelViewMatrix * vec4(vModelNormal, 0.0)).xyz;\n"
      // bias the spec light upward (sky/sun) so highlights sit on top, not the
      // back
      "	vLightView = (gl_ModelViewMatrix * vec4(normalize(ShadeVector + "
      "vec3(0.0, 0.0, 0.35)), 0.0)).xyz;\n"
      "	float dot1 = r_avertexnormal_dot(Pose1Normal);\n"
      "	float dot2 = r_avertexnormal_dot(Pose2Normal);\n"
      "	vertColor = LightColor * vec4(vec3(mix(dot1, dot2, Blend)), 1.0);\n"
      "}\n";

  const GLchar *fragSource =
      "#version 110\n"
      "\n"
      "uniform sampler2D Tex;\n"
      "uniform sampler2D FullbrightTex;\n"
      "uniform int UseFullbrightTex;\n"
      "uniform int UseOverbright;\n"
      "uniform int UseAlphaTest;\n"
      "uniform int UseSpecTex;\n"
      "uniform int gs_mod;\n"
      "uniform float FullbrightPulse;\n"	// subtle pulsing aura (1.0 = static)
      "uniform vec3 ZombieEyeColor;\n"		// user-chosen eye-glow tint
      "uniform int FbRecolor;\n"			// 1 = recolour fullbright by ZombieEyeColor (zombies)
      "uniform float fog_density;\n"
      "uniform float fog_red;\n"
      "uniform float fog_green;\n"
      "uniform float fog_blue;\n"
      "uniform vec3 ShadeVector;\n"
      "uniform vec3 SpecLightView;\n"
      "uniform float Wetness;\n"
      "uniform float Time;\n"
      "uniform float Flash;\n"	// lightning flash on this model (0 = none / indoors)
      "\n"
      "varying vec2 tc;\n"
      "varying vec4 vertColor;\n"
      "varying float FogFragCoord;\n"
      "varying vec3 vModelNormal;\n"
      "varying vec3 vViewNormal;\n"
      "varying vec3 vViewPos;\n"
      "varying vec3 vLightView;\n"
      "\n"
      "void main()\n"
      "{\n"
      "	vec4 result = texture2D(Tex, tc);\n"
      "	if (UseAlphaTest != 0 && (result.a < 0.666))\n"
      "		discard;\n"
      "	vec3 base = result.rgb;\n"
      "	result *= vertColor;\n"
      "	if (UseOverbright != 0)\n"
      "		result.rgb *= 2.0;\n"
      "	vec3  surf = result.rgb;\n"
      "	float emis = 0.0;\n"
      "	if (UseFullbrightTex != 0) {\n"
      "		vec3 fbc = texture2D(FullbrightTex, tc).rgb * FullbrightPulse;\n"
      "		if (FbRecolor != 0) {\n"
      "			float gi = max(fbc.r, max(fbc.g, fbc.b));\n"
      "			fbc = gi * ZombieEyeColor;\n"
      "			float eyemask = smoothstep(0.15, 0.45, gi);\n"
      "			surf = mix(surf, ZombieEyeColor * max(surf.r, max(surf.g, surf.b)), eyemask);\n"
      "		}\n"
      "		surf += fbc;\n"
      "		emis = max(fbc.r, max(fbc.g, fbc.b));\n"
      "	}\n"
      // view-space metal shading: keep the engine-lit texture (surf, so it reacts to the
      // map) and add a Blinn-Phong gloss bounded to metal pixels, screen-blended so it never
      // burns to white. UseSpecTex: 1 = gold (gated to gold pixels), 2 = steel (low-sat metal).
      "	if (UseSpecTex != 0) {\n"
      "		vec3  N  = normalize(vViewNormal);\n"
      "		vec3  V  = normalize(-vViewPos);\n"
      "		vec3  Lu = normalize(SpecLightView);\n" // sun/key dir (view
                                                        // space)
      "		vec3  H  = normalize(Lu + V);\n"
      "		float ndv  = max(0.0, dot(N, V));\n"
      "		float hemi = dot(N, Lu) * 0.5 + 0.5;\n" // 1 = faces the sun, 0
                                                        // = away
      "		float top  = pow(max(0.0, dot(N, H)), 24.0);\n" // tight sun
                                                                // glint
      "		float fres = pow(1.0 - ndv, 4.0) * hemi;\n" // edge sheen on the
                                                            // sun-facing side
      "		float litB = max(surf.r, max(surf.g, surf.b));\n" // how bright
                                                                  // the map lit
                                                                  // this pixel
      "		vec3  m = surf;\n"
      "		float mask = 0.0;\n"
      "		if (UseSpecTex == 2) {\n"
      // --- neutral STEEL: keep the real map-lit texture, add a gentle gloss.
      // very light so a false positive on cloth is barely visible. ---
      "			float sat = max(abs(base.r - base.g), max(abs(base.g - "
      "base.b), abs(base.r - base.b)));\n"
      "			float metal = smoothstep(0.12, 0.04, sat) * "
      "smoothstep(0.12, 0.34, max(base.r, max(base.g, base.b)));\n"
      "			vec3  hotc  = vec3(0.86, 0.91, 1.0);\n"
      "			float hl    = clamp(top * 0.25 + fres * 0.10, 0.0, "
      "1.0) * clamp(litB, 0.0, 1.0);\n"
      "			m = surf + hotc * hl * (1.0 - surf);\n"
      "			mask = metal;\n"
      "			emis = max(emis, hl * metal * 0.04);\n"
      "		} else {\n"
      // --- warm GOLD: keep the engine-lit texture but steer the hue back toward gold
      // and add glint + edge sheen; mask is gold saturation only (excludes skin/cloth).
      "			float gold = smoothstep(0.33, 0.46, base.r - base.b);\n"
      "			vec3  goldhue = vec3(1.0, 0.74, 0.30);\n"
      "			vec3  gbase = mix(surf, goldhue * litB, 0.5);\n" // 50% map tint, 50% pure gold
      "			gbase *= (0.92 + 0.16 * hemi);\n"
      "			vec3  hotc  = vec3(1.0, 0.93, 0.70);\n"
      "			float ndh   = max(0.0, dot(N, H));\n"
      "			float glint = pow(ndh, 26.0);\n" // tight pop
      "			float broad = pow(ndh, 3.0);\n" // BROAD sheen -> glides
                                                        // across the gun
      "			float hl    = clamp(glint * 0.40 + broad * 0.35 + fres "
      "* 0.18, 0.0, 1.0) * clamp(litB + 0.15, 0.0, 1.0);\n"
      "			m = gbase + hotc * hl * (1.0 - gbase);\n" // bounded,
                                                                  // never burns
                                                                  // white
      "			mask = gold;\n"
      "			emis = max(emis, hl * gold * 0.12);\n"
      "		}\n"
      "		surf = mix(surf, m, mask);\n"
      "	}\n"
      "	if (Wetness > 0.01) {\n"
      "		vec3  N  = normalize(vViewNormal);\n"
      "		// Toned-down natural wave perturbation using Time\n"
      "		vec3  wave = vec3(\n"
      "			sin(tc.x * 24.0 + Time * 1.7) * 0.035,\n"
      "			cos(tc.y * 24.0 + Time * 1.4) * 0.035,\n"
      "			0.0\n"
      "		);\n"
      "		N = normalize(N + wave * Wetness);\n"
      "		vec3  V  = normalize(-vViewPos);\n"
      "		vec3  Lu = normalize(SpecLightView);\n"
      "		vec3  H  = normalize(Lu + V);\n"
      "		float ndv  = max(0.0, dot(N, V));\n"
      "		float litB = max(surf.r, max(surf.g, surf.b));\n"
      "		float top  = pow(max(0.0, dot(N, H)), 64.0);\n"
      "		float fres = pow(1.0 - ndv, 4.0);\n"
      "		// Wet specular sheen from light source (toned down to prevent white-out)\n"
      "		vec3  hotc  = vec3(0.85, 0.92, 1.0);\n"
      "		float hl    = clamp(top * 0.15 + fres * 0.18, 0.0, 1.0) * clamp(litB + 0.15, 0.0, 1.0) * Wetness;\n"
      "		// Procedural environment sky/ground reflection (toned down to subtle wetness)\n"
      "		vec3  R = reflect(-V, N);\n"
      "		vec3  skyColor = vec3(0.70, 0.88, 1.0) * 1.30;\n"
      "		vec3  groundColor = vec3(0.06, 0.06, 0.06);\n"
      "		vec3  envReflect = mix(groundColor, skyColor, smoothstep(-0.20, 0.60, R.y));\n"
      "		float envSheen = fres * 0.22 * Wetness * clamp(litB + 0.15, 0.0, 1.0);\n"
      "		// Darken base diffuse (soaked look)\n"
      "		surf = mix(surf, surf * 0.68, Wetness);\n"
      "		// Add specular and environment reflections\n"
      "		surf += hotc * hl * (1.0 - surf) + envReflect * envSheen;\n"
      "	}\n"
      // NZP lightning flash: brief bright fill + sky-lit specular sheen so sky-exposed
      // objects glint as the bolt fires (gated per-entity on the CPU, 0 indoors).
      "	if (Flash > 0.0) {\n"
      "		vec3 Nf = normalize(vViewNormal);\n"
      "		vec3 Vf = normalize(-vViewPos);\n"
      "		vec3 Lf = normalize(SpecLightView);\n"		// sky/key dir (view space)
      "		vec3 Hf = normalize(Lf + Vf);\n"
      "		float hemi  = dot(Nf, Lf) * 0.5 + 0.5;\n"	// 1 = faces the sky
      "		float glint = pow(max(0.0, dot(Nf, Hf)), 32.0);\n"	// tight sheen
      "		surf += vec3(0.72, 0.82, 1.0) * (Flash * (hemi * 0.55 + glint * 1.8));\n"
      "	}\n"
      // contour shading: darken grazing-angle surfaces (form/cavity), lift fullbright back
      "	float ndv_c = max(0.0, dot(normalize(vViewNormal), normalize(-vViewPos)));\n"
      "	surf *= mix(0.74, 1.0, ndv_c) + emis * (1.0 - mix(0.74, 1.0, ndv_c));\n"
      "	surf = clamp(surf, 0.0, 1.0);\n"
      // emissive pixels (glow / gloss) resist fog so props show through it
      "	float fog = exp(-fog_density * fog_density * FogFragCoord * "
      "FogFragCoord);\n"
      "	fog = clamp(fog + emis, 0.0, 1.0);\n"
      "	result.rgb = mix(vec3(fog_red, fog_green, fog_blue), surf, fog);\n"
      "	result.a = vertColor.a;\n"
      "	if (gs_mod != 0) {\n"
      "		float value = clamp((result.r * 0.33) + (result.g * 0.55) + "
      "(result.b * 0.11), 0.0, 1.0);\n"
      "		result.r = value;\n"
      "		result.g = value;\n"
      "		result.b = value;\n"
      "	}\n"
      "	gl_FragColor = result;\n"
      "}\n";
#endif

  if (!gl_glsl_alias_able)
    return;

  r_alias_program = GL_CreateProgram(
      vertSource, fragSource, sizeof(bindings) / sizeof(bindings[0]), bindings);

  if (r_alias_program != 0) {
    // get uniform locations
    blendLoc = GL_GetUniformLocation(&r_alias_program, "Blend");
    shadevectorLoc = GL_GetUniformLocation(&r_alias_program, "ShadeVector");
    lightColorLoc = GL_GetUniformLocation(&r_alias_program, "LightColor");
    // #ifndef VITA
    texLoc = GL_GetUniformLocation(&r_alias_program, "Tex");
    fullbrightTexLoc = GL_GetUniformLocation(&r_alias_program, "FullbrightTex");
    // #endif
    useFullbrightTexLoc =
        GL_GetUniformLocation(&r_alias_program, "UseFullbrightTex");
    fullbrightPulseLoc = GL_GetUniformLocation(&r_alias_program, "FullbrightPulse");
    zombieEyeColorLoc = GL_GetUniformLocation(&r_alias_program, "ZombieEyeColor");
    fbRecolorLoc = GL_GetUniformLocation(&r_alias_program, "FbRecolor");
    useOverbrightLoc = GL_GetUniformLocation(&r_alias_program, "UseOverbright");
    useAlphaTestLoc = GL_GetUniformLocation(&r_alias_program, "UseAlphaTest");
    aliasgrayscale_enableLoc =
        GL_GetUniformLocation(&r_alias_program, "gs_mod");
    fogDensityLoc = GL_GetUniformLocation(&r_alias_program, "fog_density");
    fogRedLoc = GL_GetUniformLocation(&r_alias_program, "fog_red");
    fogGreenLoc = GL_GetUniformLocation(&r_alias_program, "fog_green");
    fogBlueLoc = GL_GetUniformLocation(&r_alias_program, "fog_blue");
    // never look up a uniform that isn't in the shader: GL_GetUniformLocation
    // zeroes r_alias_program on a -1, silently killing the whole GLSL path.
    useSpecTexLoc = GL_GetUniformLocation(&r_alias_program, "UseSpecTex");
    specLightViewLoc = GL_GetUniformLocation(&r_alias_program, "SpecLightView");
    wetnessLoc = GL_GetUniformLocation(&r_alias_program, "Wetness");
    timeLoc = GL_GetUniformLocation(&r_alias_program, "Time");
    flashLoc = GL_GetUniformLocation(&r_alias_program, "Flash");
  }

  // minimal shadow program: same pose verts via the flattened matrix stack, output
  // flat black at a set alpha. Reuses the model VBO.
  {
    const GLchar *shvert =
        "#version 110\n"
        "uniform float Blend;\n"
        "attribute vec4 Pose1Vert;\n"
        "attribute vec4 Pose2Vert;\n"
        "void main(){\n"
        "  vec4 v = mix(vec4(Pose1Vert.xyz,1.0), vec4(Pose2Vert.xyz,1.0), Blend);\n"
        "  gl_Position = gl_ModelViewProjectionMatrix * v;\n"
        "}\n";
    const GLchar *shfrag =
        "#version 110\n"
        "uniform float ShadowAlpha;\n"
        "void main(){ gl_FragColor = vec4(0.0, 0.0, 0.0, ShadowAlpha); }\n";
    const glsl_attrib_binding_t shbindings[] = {
        {"Pose1Vert", pose1VertexAttrIndex}, {"Pose2Vert", pose2VertexAttrIndex}};
    r_aliasshadow_program = GL_CreateProgram(shvert, shfrag,
        sizeof(shbindings) / sizeof(shbindings[0]), shbindings);
    if (r_aliasshadow_program != 0) {
      shadow_blendLoc = GL_GetUniformLocation(&r_aliasshadow_program, "Blend");
      shadow_alphaLoc = GL_GetUniformLocation(&r_aliasshadow_program, "ShadowAlpha");
    }
  }
}

/*
=============
GL_DrawAliasFrame_GLSL -- ericw

Optimized alias model drawing codepath.
Compared to the original GL_DrawAliasFrame, this makes 1 draw call,
no vertex data is uploaded (it's already in the r_meshvbo and r_meshindexesvbo
static VBOs), and lerping and lighting is done in the vertex shader.

Supports optional overbright, optional fullbright pixels.

Based on code by MH from RMQEngine
=============
*/
void GL_DrawAliasFrame_GLSL(aliashdr_t *paliashdr, lerpdata_t lerpdata,
                            gltexture_t *tx, gltexture_t *fb, gltexture_t *sp) {
  float blend;

  // never draw with a missing vertex/index buffer: binding buffer 0 reads garbage as
  // vertices and hangs citron, so skip the draw (retried next frame) if rebuild failed.
  if (currententity->model->meshvbo == 0 ||
      currententity->model->meshindexesvbo == 0)
    return;

  if (lerpdata.pose1 != lerpdata.pose2) {
    blend = lerpdata.blend;
  } else // poses the same means either 1. the entity has paused its animation,
         // or 2. r_lerpmodels is disabled
  {
    blend = 0;
  }

  // per-object motion blur: record this draw (transform + lerp poses) so the velocity
  // pass can replay it for per-pixel velocity (model movement AND limb animation).
  if (gl_motionblur.value > 0.0f && currententity->model->meshvbo) {
    entity_t *e = currententity;
    float proj[16], mv[16], curmvp[16];
    int np = paliashdr->numposes;
    short p1 = lerpdata.pose1, p2 = lerpdata.pose2, pp1, pp2;
    float pbP;
    const float *mvpP;
    extern qboolean model_is_zombie(char name[MAX_QPATH]);
    qboolean hist = (e->mblur_mvpframe == r_framecount - 1);	// drawn last frame too?
    int isview = (e == &cl.viewent || e == &cl.viewent2);
    int isz = (e->model && model_is_zombie(e->model->name));

    glGetFloatv (GL_PROJECTION_MATRIX, proj);
    glGetFloatv (GL_MODELVIEW_MATRIX, mv);
    MB_Mat4Mult (curmvp, proj, mv);	// world -> clip for this model, this frame

    if (hist) {
      pp1 = e->mblur_curpose1; pp2 = e->mblur_curpose2; pbP = e->mblur_curblend;
      if (pp1 < 0) pp1 = 0; else if (pp1 >= np) pp1 = np - 1;	// clamp vs model swap
      if (pp2 < 0) pp2 = 0; else if (pp2 >= np) pp2 = np - 1;
      mvpP = e->mblur_curmvp;
    } else {	// no continuous history -> zero velocity this frame (avoids a pop)
      pp1 = p1; pp2 = p2; pbP = blend; mvpP = curmvp;
    }

    R_MotionBlur_PushAlias (e->model->meshvbo, e->model->meshindexesvbo,
                            paliashdr->numindexes, e->model->vboindexofs,
                            sizeof(meshxyz_t),
                            GLARB_GetXYZOffset (paliashdr, p1),
                            GLARB_GetXYZOffset (paliashdr, p2),
                            GLARB_GetXYZOffset (paliashdr, pp1),
                            GLARB_GetXYZOffset (paliashdr, pp2),
                            blend, pbP, curmvp, mvpP, isview, isz);

    memcpy (e->mblur_curmvp, curmvp, sizeof(curmvp));	// becomes next frame's "previous"
    e->mblur_curpose1 = p1; e->mblur_curpose2 = p2; e->mblur_curblend = blend;
    e->mblur_mvpframe = r_framecount;
  }

  GL_UseProgramFunc(r_alias_program);

  GL_BindBuffer(GL_ARRAY_BUFFER, currententity->model->meshvbo);
  GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, currententity->model->meshindexesvbo);

  GL_EnableVertexAttribArrayFunc(texCoordsAttrIndex);
  GL_EnableVertexAttribArrayFunc(pose1VertexAttrIndex);
  GL_EnableVertexAttribArrayFunc(pose2VertexAttrIndex);
  GL_EnableVertexAttribArrayFunc(pose1NormalAttrIndex);
  GL_EnableVertexAttribArrayFunc(pose2NormalAttrIndex);

  GL_VertexAttribPointerFunc(texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0,
                             (void *)(intptr_t)currententity->model->vbostofs);
  GL_VertexAttribPointerFunc(pose1VertexAttrIndex, 4, GL_UNSIGNED_BYTE,
                             GL_FALSE, sizeof(meshxyz_t),
                             GLARB_GetXYZOffset(paliashdr, lerpdata.pose1));
  GL_VertexAttribPointerFunc(pose2VertexAttrIndex, 4, GL_UNSIGNED_BYTE,
                             GL_FALSE, sizeof(meshxyz_t),
                             GLARB_GetXYZOffset(paliashdr, lerpdata.pose2));
  // GL_TRUE to normalize the signed bytes to [-1 .. 1]
  GL_VertexAttribPointerFunc(pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE,
                             sizeof(meshxyz_t),
                             GLARB_GetNormalOffset(paliashdr, lerpdata.pose1));
  GL_VertexAttribPointerFunc(pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE,
                             sizeof(meshxyz_t),
                             GLARB_GetNormalOffset(paliashdr, lerpdata.pose2));

  // set uniforms
  GL_Uniform1fFunc(blendLoc, blend);
  GL_Uniform3fFunc(shadevectorLoc, shadevector[0], shadevector[1],
                   shadevector[2]);
  GL_Uniform3fFunc(specLightViewLoc, alias_spec_lightview[0],
                   alias_spec_lightview[1], alias_spec_lightview[2]);
  GL_Uniform4fFunc(lightColorLoc, lightcolor[0], lightcolor[1], lightcolor[2],
                   entalpha);
  // #ifndef VITA
  GL_Uniform1iFunc(texLoc, 0);
  GL_Uniform1iFunc(fullbrightTexLoc, 1);
  // #endif
  //  no fullbright glow on the menu cam feed
  {
    extern cvar_t cam_tour;
    GL_Uniform1iFunc(useFullbrightTexLoc,
                     (fb != NULL && !cam_tour.value) ? 1 : 0);
  }
  // subtle pulsing "aura" on the zombie eye-glow only. Other fullbright
  // models (lamps, weapon glows) stay static at 1.0.
  {
    extern qboolean model_is_zombie(char name[MAX_QPATH]);
    extern void R_GetZombieEyeColor(float *out);
    qboolean isz = (currententity->model && model_is_zombie(currententity->model->name));
    float fbpulse = isz ? (0.85 + 0.15 * sin(cl.time * 2.5)) : 1.0;
    float ec[3] = {1, 1, 1};
    if (isz)
      R_GetZombieEyeColor(ec);
    GL_Uniform1fFunc(fullbrightPulseLoc, fbpulse);
    GL_Uniform1iFunc(fbRecolorLoc, isz ? 1 : 0);
    GL_Uniform3fFunc(zombieEyeColorLoc, ec[0], ec[1], ec[2]);
  }
  GL_Uniform1iFunc(useOverbrightLoc, overbright ? 1 : 0);
  GL_Uniform1iFunc(useAlphaTestLoc,
                   (currententity->model->flags & MF_HOLEY) ? 1 : 0);
  // specular uniforms. 0=off, 1=gold, 2=steel. gold models carry "gold" in
  // the name; everything else with a _spec marker gets the neutral steel sheen.
  {
    int specmode = 0;
    if (sp != NULL)
      specmode =
          (currententity->model && strstr(currententity->model->name, "gold"))
              ? 1
              : 2;
    GL_Uniform1iFunc(useSpecTexLoc, specmode);
  }

  {
    extern float weather_wetness;
    float current_wetness = 0.0f;
    if (currententity == &cl.viewent)
    {
      current_wetness = weather_wetness;
    }
    GL_Uniform1fFunc(wetnessLoc, current_wetness);
    GL_Uniform1fFunc(timeLoc, cl.time);
    {	// NZP lightning: flash this model only if it's out under the open sky (gun -> player pos).
        extern float weather_lightning_flash;
        extern qboolean Weather_PointUnderOpenSky (vec3_t pos);
        float fa = 0.0f;
        if (weather_lightning_flash > 0.0f) {
            int isview = (currententity == &cl.viewent || currententity == &cl.viewent2);
            if (Weather_PointUnderOpenSky (isview ? r_origin : currententity->origin))
                fa = weather_lightning_flash;
        }
        GL_Uniform1fFunc(flashLoc, fa);
    }
  }

  {
    float *fog_c = Fog_GetColor();
    qboolean nofog = (currententity->effects & EF_FULLBRIGHT) ||
                     (currententity->model &&
                      strstr(currententity->model->name, "mglow") != NULL);
    float fogdens = nofog ? 0.0f : (Fog_GetDensity() / 64.0f);
    GL_Uniform1fFunc(fogDensityLoc, fogdens);
    GL_Uniform1fFunc(fogRedLoc, fog_c[0]);
    GL_Uniform1fFunc(fogGreenLoc, fog_c[1]);
    GL_Uniform1fFunc(fogBlueLoc, fog_c[2]);
  }

  // also send ShadeVector to the frag shader for the specular L dir (same uniform
  // name in both stages, so one GL_Uniform3f covers both)
  // naievil -- experimental grayscale mod
  GL_Uniform1iFunc(aliasgrayscale_enableLoc,
                   /*sv_player->v.renderGrayscale*/ 0);

  // set textures
  GL_SelectTexture(GL_TEXTURE0);
  GL_Bind(tx);

  if (fb) {
    GL_SelectTexture(GL_TEXTURE1);
    GL_Bind(fb);
  }

  // draw
  glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
                 (void *)(intptr_t)currententity->model->vboindexofs);

  // clean up
  GL_DisableVertexAttribArrayFunc(texCoordsAttrIndex);
  GL_DisableVertexAttribArrayFunc(pose1VertexAttrIndex);
  GL_DisableVertexAttribArrayFunc(pose2VertexAttrIndex);
  GL_DisableVertexAttribArrayFunc(pose1NormalAttrIndex);
  GL_DisableVertexAttribArrayFunc(pose2NormalAttrIndex);

  GL_BindBuffer(GL_ARRAY_BUFFER, 0);
  GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  GL_UseProgramFunc(0);
  GL_SelectTexture(GL_TEXTURE0);

  rs_aliaspasses += paliashdr->numtris;
}

/*
=============
GL_DrawAliasFrame -- johnfitz -- rewritten to support colored light, lerping,
entalpha, multitexture, and r_drawflat
=============
*/
void GL_DrawAliasFrame(aliashdr_t *paliashdr, lerpdata_t lerpdata) {
  float vertcolor[4];
  trivertx_t *verts1, *verts2;
  int *commands;
  int count;
  float u, v;
  float blend, iblend;
  qboolean lerping;

  if (lerpdata.pose1 != lerpdata.pose2) {
    lerping = true;
    verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
    verts2 = verts1;
    verts1 += lerpdata.pose1 * paliashdr->poseverts;
    verts2 += lerpdata.pose2 * paliashdr->poseverts;
    blend = lerpdata.blend;
    iblend = 1.0f - blend;
  } else // poses the same means either 1. the entity has paused its animation,
         // or 2. r_lerpmodels is disabled
  {
    lerping = false;
    verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
    verts2 = verts1; // avoid bogus compiler warning
    verts1 += lerpdata.pose1 * paliashdr->poseverts;
    blend = iblend = 0; // avoid bogus compiler warning
  }

  commands = (int *)((byte *)paliashdr + paliashdr->commands);

  vertcolor[3] =
      entalpha; // never changes, so there's no need to put this inside the loop

  while (1) {
    // get the vertex count and primitive type
    count = *commands++;
    if (!count)
      break; // done

    if (count < 0) {
      count = -count;
      glBegin(GL_TRIANGLE_FAN);
    } else
      glBegin(GL_TRIANGLE_STRIP);

    do {
      u = ((float *)commands)[0];
      v = ((float *)commands)[1];
      if (mtexenabled) {
        GL_MTexCoord2fFunc(GL_TEXTURE0_ARB, u, v);
        GL_MTexCoord2fFunc(GL_TEXTURE1_ARB, u, v);
      } else
        glTexCoord2f(u, v);

      commands += 2;

      if (shading) {
        if (r_drawflat_cheatsafe) {
          srand(count * (unsigned int)(src_offset_t)commands);
          glColor3f(rand() % 256 / 255.0, rand() % 256 / 255.0,
                    rand() % 256 / 255.0);
        } else if (lerping) {
          vertcolor[0] = (shadedots[verts1->lightnormalindex] * iblend +
                          shadedots[verts2->lightnormalindex] * blend) *
                         lightcolor[0];
          vertcolor[1] = (shadedots[verts1->lightnormalindex] * iblend +
                          shadedots[verts2->lightnormalindex] * blend) *
                         lightcolor[1];
          vertcolor[2] = (shadedots[verts1->lightnormalindex] * iblend +
                          shadedots[verts2->lightnormalindex] * blend) *
                         lightcolor[2];
          glColor4fv(vertcolor);
        } else {
          vertcolor[0] = shadedots[verts1->lightnormalindex] * lightcolor[0];
          vertcolor[1] = shadedots[verts1->lightnormalindex] * lightcolor[1];
          vertcolor[2] = shadedots[verts1->lightnormalindex] * lightcolor[2];
          glColor4fv(vertcolor);
        }
      }

      if (lerping) {
        glVertex3f(verts1->v[0] * iblend + verts2->v[0] * blend,
                   verts1->v[1] * iblend + verts2->v[1] * blend,
                   verts1->v[2] * iblend + verts2->v[2] * blend);
        verts1++;
        verts2++;
      } else {
        glVertex3f(verts1->v[0], verts1->v[1], verts1->v[2]);
        verts1++;
      }
    } while (--count);

    glEnd();
  }

  rs_aliaspasses += paliashdr->numtris;
}

/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping
=================
*/
void R_SetupAliasFrame(aliashdr_t *paliashdr, int frame, lerpdata_t *lerpdata) {
  entity_t *e = currententity;
  int posenum, numposes;

  if ((frame >= paliashdr->numframes) || (frame < 0)) {
    Con_DPrintf("R_AliasSetupFrame: no such frame %d for '%s'\n", frame,
                e->model->name);
    frame = 0;
  }

  posenum = paliashdr->frames[frame].firstpose;
  numposes = paliashdr->frames[frame].numposes;

  if (numposes > 1) {
    e->lerptime = paliashdr->frames[frame].interval;
    posenum += (int)(cl.time / e->lerptime) % numposes;
  } else
    e->lerptime = 0.1;

  if (e->lerpflags & LERP_RESETANIM) // kill any lerp in progress
  {
    e->lerpstart = 0;
    e->previouspose = posenum;
    e->currentpose = posenum;
    e->lerpflags -= LERP_RESETANIM;
  } else if (e->currentpose != posenum) // pose changed, start new lerp
  {
    if (e->lerpflags & LERP_RESETANIM2) // defer lerping one more time
    {
      e->lerpstart = 0;
      e->previouspose = posenum;
      e->currentpose = posenum;
      e->lerpflags -= LERP_RESETANIM2;
    } else {
      e->lerpstart = cl.time;
      e->previouspose = e->currentpose;
      e->currentpose = posenum;
    }
  }

  // set up values
  if (r_lerpmodels.value &&
      !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2)) {
    if (e->lerpflags & LERP_FINISH && numposes == 1)
      lerpdata->blend = CLAMP(
          0, (cl.time - e->lerpstart) / (e->lerpfinish - e->lerpstart), 1);
    else
      lerpdata->blend = CLAMP(0, (cl.time - e->lerpstart) / e->lerptime, 1);
    lerpdata->pose1 = e->previouspose;
    lerpdata->pose2 = e->currentpose;
  } else // don't lerp
  {
    lerpdata->blend = 1;
    lerpdata->pose1 = posenum;
    lerpdata->pose2 = posenum;
  }
}

/*
=================
R_SetupEntityTransform -- johnfitz -- set up transform part of lerpdata
=================
*/
void R_SetupEntityTransform(entity_t *e, lerpdata_t *lerpdata) {
  float blend;
  vec3_t d;
  int i;

  // if LERP_RESETMOVE, kill any lerps in progress
  if (e->lerpflags & LERP_RESETMOVE) {
    e->movelerpstart = 0;
    VectorCopy(e->origin, e->previousorigin);
    VectorCopy(e->origin, e->currentorigin);
    VectorCopy(e->angles, e->previousangles);
    VectorCopy(e->angles, e->currentangles);
    e->lerpflags -= LERP_RESETMOVE;
  } else if (!VectorCompare(e->origin, e->currentorigin) ||
             !VectorCompare(
                 e->angles,
                 e->currentangles)) // origin/angles changed, start new lerp
  {
    e->movelerpstart = cl.time;
    VectorCopy(e->currentorigin, e->previousorigin);
    VectorCopy(e->origin, e->currentorigin);
    VectorCopy(e->currentangles, e->previousangles);
    VectorCopy(e->angles, e->currentangles);
  }

  // set up values
  if (r_lerpmove.value && e != &cl.viewent && e->lerpflags & LERP_MOVESTEP) {
    if (e->lerpflags & LERP_FINISH)
      blend = CLAMP(
          0, (cl.time - e->movelerpstart) / (e->lerpfinish - e->movelerpstart),
          1);
    else
      blend = CLAMP(0, (cl.time - e->movelerpstart) / 0.1, 1);

    // translation
    VectorSubtract(e->currentorigin, e->previousorigin, d);
    lerpdata->origin[0] = e->previousorigin[0] + d[0] * blend;
    lerpdata->origin[1] = e->previousorigin[1] + d[1] * blend;
    lerpdata->origin[2] = e->previousorigin[2] + d[2] * blend;

    // rotation
    VectorSubtract(e->currentangles, e->previousangles, d);
    for (i = 0; i < 3; i++) {
      if (d[i] > 180)
        d[i] -= 360;
      if (d[i] < -180)
        d[i] += 360;
    }
    lerpdata->angles[0] = e->previousangles[0] + d[0] * blend;
    lerpdata->angles[1] = e->previousangles[1] + d[1] * blend;
    lerpdata->angles[2] = e->previousangles[2] + d[2] * blend;
  } else // don't lerp
  {
    VectorCopy(e->origin, lerpdata->origin);
    VectorCopy(e->angles, lerpdata->angles);
  }
}

/*
=================
R_AliasWorldLightDir -- NZP

Derive a real WORLD-space light direction from the baked lightmap so model
shading/specular reacts to where light actually is (the old code faked the
direction from the entity's yaw). Probes lightmap brightness around the entity
(the brighter side = the light), folds in nearby dlights, then biases the
result upward (sky/sun) so highlights sit on top. The caller rotates this into
the model's local frame.
=================
*/
static void R_AliasWorldLightDir(vec3_t origin, vec3_t out) {
  vec3_t saved, p;
  float bxp, bxn, byp, byn, avg, inv, tilt;
  const float r = 24.0f; // horizontal probe radius (units)
  int i;

  VectorCopy(lightcolor, saved); // R_LightPoint clobbers the global lightcolor

  VectorCopy(origin, p);
  p[0] += r;
  bxp = R_LightPoint(p);
  VectorCopy(origin, p);
  p[0] -= r;
  bxn = R_LightPoint(p);
  VectorCopy(origin, p);
  p[1] += r;
  byp = R_LightPoint(p);
  VectorCopy(origin, p);
  p[1] -= r;
  byn = R_LightPoint(p);

  VectorCopy(saved, lightcolor); // restore the entity's own light color

  // relative gradient: divide the brightness difference by the local average, so
  // outdoor near-uniform light stays put while indoor lamp directionality survives.
  avg = (bxp + bxn + byp + byn) * 0.25f;
  inv = (avg > 1.0f) ? 1.0f / avg : 1.0f;
  out[0] = (bxp - bxn) * inv; // points toward the brighter (lit) side
  out[1] = (byp - byn) * inv;
  out[2] = 0.0f;

  // fold in nearby dynamic lights (muzzle flash, power-ups) as real directions,
  // scaled to the same ~0..1 space as the relative gradient
  for (i = 0; i < MAX_DLIGHTS; i++) {
    vec3_t d;
    float w, len;
    if (cl_dlights[i].die < cl.time || cl_dlights[i].radius <= 0.0f)
      continue;
    VectorSubtract(cl_dlights[i].origin, origin, d);
    len = VectorLength(d);
    w = (cl_dlights[i].radius - len) /
        cl_dlights[i].radius; // 1=on top, 0=at edge
    if (w > 0.0f && len > 0.0f)
      VectorMA(out, w * 0.8f / len, d,
               out); // unit dir * closeness (mild: don't hijack the dir)
  }

  // constant upward baseline so light reads as from above; gradient/dlights tilt it.
  // r_modeldirtilt: higher = more top-down, lower = more horizontal.
  tilt = (r_modeldirtilt.value > 0.01f) ? r_modeldirtilt.value : 1.5f;
  out[2] = tilt;
  VectorNormalize(out);
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and
rewritten
=================
*/
void R_SetupAliasLighting(entity_t *e) {
  vec3_t dist;
  float add;
  int i;
  int quantizedangle;

  R_LightPoint(e->origin);

  // match the world lightmap "lightscale" so models don't render bright
  // relative to scaled-down walls. (ambient/floor handled below at line ~72.)
  {
    extern float map_lightscale;
    extern cvar_t r_lightscale;
    float ls = map_lightscale * r_lightscale.value;
    if (ls != 1.0f)
      VectorScale(lightcolor, ls, lightcolor);
  }

  // add dlights
  for (i = 0; i < MAX_DLIGHTS; i++) {
    if (cl_dlights[i].die >= cl.time) {
      VectorSubtract(currententity->origin, cl_dlights[i].origin, dist);
      add = cl_dlights[i].radius - VectorLength(dist);
      if (add > 0)
        VectorMA(lightcolor, add, cl_dlights[i].color, lightcolor);
    }
  }

  // cypress -- limit light value on all ents, not
  // just viewmodel.
  add = 72.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
  if (add > 0.0f) {
    lightcolor[0] += add / 3.0f;
    lightcolor[1] += add / 3.0f;
    lightcolor[2] += add / 3.0f;
  }

  // clamp lighting so it doesn't overbright as much (96)
  // if (overbright)
  // {
  // 	add = 288.0f / (lightcolor[0] + lightcolor[1] + lightcolor[2]);
  // 	if (add < 1.0f)
  // 		VectorScale(lightcolor, add, lightcolor);
  // }

  // hack up the brightness when fullbrights but no overbrights (256)
  if (gl_fullbrights.value && !gl_overbright_models.value)
    if (e->model->flags & MOD_FBRIGHTHACK) {
      lightcolor[0] = 256.0f;
      lightcolor[1] = 256.0f;
      lightcolor[2] = 256.0f;
    }

  // cypress -- re-te EF_FULLBRIGHT support
  // TODO: potentially just block dlights from colorizing
  // the mystery-box glow's EF_FULLBRIGHT doesn't survive the entity-state delta, so
  // also match by model name ("mglow") to keep it fullbright (pairs with the fog exemption).
  if ((e->effects & EF_FULLBRIGHT) ||
      (e->model && strstr(e->model->name, "mglow") != NULL)) {
    lightcolor[0] = 400.0f;
    lightcolor[1] = 400.0f;
    lightcolor[2] = 400.0f;
  }

  quantizedangle =
      ((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1);
  shadedots =
      r_avertexnormal_dots[quantizedangle]; // CPU (non-GLSL) fallback path

  {
    // real environmental light (always on): a world-space direction from the lightmap,
    // rotated into the model's local frame (alias normals are un-rotated by the entity).
    vec3_t wl;
    R_AliasWorldLightDir(e->origin, wl);

    // temporally smooth the world dir per-entity: the raw gradient snaps frame-to-frame
    // (animated lightstyles / edge traces), so glide toward the new dir.
    if (!e->lightdir_init || r_modeldirsmooth.value <= 0.0f) {
      VectorCopy(wl, e->lightdir_smooth);
      e->lightdir_init = true;
    } else {
      float frac = 1.0f - expf(-(float)host_frametime / r_modeldirsmooth.value);
      if (frac < 0.0f)
        frac = 0.0f;
      if (frac > 1.0f)
        frac = 1.0f;
      e->lightdir_smooth[0] += (wl[0] - e->lightdir_smooth[0]) * frac;
      e->lightdir_smooth[1] += (wl[1] - e->lightdir_smooth[1]) * frac;
      e->lightdir_smooth[2] += (wl[2] - e->lightdir_smooth[2]) * frac;
    }
    VectorCopy(e->lightdir_smooth, wl);
    VectorNormalize(wl);

    // rotate the world light into model space using the exact transpose of
    // R_RotateForEntity's rotation (negated pitch), so diffuse is correct at any angle.
    {
      float p = e->angles[0] * (M_PI / 180.0f);
      float y = e->angles[1] * (M_PI / 180.0f);
      float r = e->angles[2] * (M_PI / 180.0f);
      float cy = cosf(y), sy = sinf(y);
      float cp = cosf(p), sp = sinf(p);
      float cr = cosf(r), sr = sinf(r);
      float ax, ay, az, bx, bz;
      // Rz(-yaw)
      ax = cy * wl[0] + sy * wl[1];
      ay = -sy * wl[0] + cy * wl[1];
      az = wl[2];
      // Ry(pitch)
      bx = cp * ax + sp * az;
      bz = -sp * ax + cp * az;
      // Rx(-roll)
      shadevector[0] = bx;
      shadevector[1] = cr * ay + sr * bz;
      shadevector[2] = -sr * ay + cr * bz;
      VectorNormalize(shadevector);
    }

    // specular light dir in view space so the gloss anchors to the world; metal "up"
    // defaults to sky, r_modelspecworld tilts it toward the map light (0=sky, 1=map).
    {
      vec3_t upview, lightview;
      float wmix = r_modelspecworld.value;
      if (wmix < 0.0f)
        wmix = 0.0f;
      if (wmix > 1.0f)
        wmix = 1.0f;
      // world up (0,0,1) expressed in view space
      upview[0] = vright[2];
      upview[1] = vup[2];
      upview[2] = -vpn[2];
      VectorNormalize(upview);
      // map light dir in view space
      lightview[0] = DotProduct(wl, vright);
      lightview[1] = DotProduct(wl, vup);
      lightview[2] = -DotProduct(wl, vpn);
      VectorNormalize(lightview);
      alias_spec_lightview[0] = upview[0] * (1.0f - wmix) + lightview[0] * wmix;
      alias_spec_lightview[1] = upview[1] * (1.0f - wmix) + lightview[1] * wmix;
      alias_spec_lightview[2] = upview[2] * (1.0f - wmix) + lightview[2] * wmix;
      VectorNormalize(alias_spec_lightview);
      // push the key forward toward the muzzle so the top sheen sits on the front
      // of the gun, not reflecting off the rear near the camera.
      alias_spec_lightview[2] -= r_modelspecfwd.value;
      VectorNormalize(alias_spec_lightview);
    }
  }
  VectorScale(lightcolor, 1.0f / 200.0f, lightcolor);
}

/*
=================
R_DrawTransparentAliasModel
blubs: used for semitransparent fullbright models (like their sprite
counterparts)
=================
*/

void R_DrawTransparentAliasModel(entity_t *e) {
  /*
  model_t		*clmodel;
  vec3_t		mins, maxs;
  aliashdr_t	*paliashdr;
  float		an;
  int			anim;

  clmodel = e->model;

  VectorAdd (e->origin, clmodel->mins, mins);
  VectorAdd (e->origin, clmodel->maxs, maxs);

  if (R_CullBox(mins, maxs))
          return;

  VectorCopy (e->origin, r_entorigin);
  VectorSubtract (r_origin, r_entorigin, modelorg);

  //
  // get lighting information
  // LordHavoc: .lit support begin
  //ambientlight = shadelight = R_LightPoint (e->origin); // LordHavoc: original
code, removed shadelight and ambientlight R_LightPoint(e->origin); // LordHavoc:
lightcolor is all that matters from this
  // LordHavoc: .lit support end
  lightcolor[0] = lightcolor[1] = lightcolor[2] = 256;
  shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT /
360.0))) & (SHADEDOT_QUANT - 1)];
  // LordHavoc: .lit support begin
  //shadelight = shadelight / 200.0; // LordHavoc: original code
  VectorScale(lightcolor, 1.0f / 200.0f, lightcolor);
  // LordHavoc: .lit support end
  an = e->angles[1]/180*M_PI;
  shadevector[0] = cosf(-an);
  shadevector[1] = sinf(-an);
  shadevector[2] = 1;
  VectorNormalize (shadevector);
  // locate the proper data//
  paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
  c_alias_polys += paliashdr->numtris;
  // draw all the triangles//
  sceGumPushMatrix();
  R_InterpolateEntity(e,0);
  const ScePspFVector3 translation =
  {
          paliashdr->scale_origin[0], paliashdr->scale_origin[1],
paliashdr->scale_origin[2]
  };
  sceGumTranslate(&translation);
  const ScePspFVector3 scaling =
  {
          paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]
  };
  sceGumScale(&scaling);

  //for models(pink transparency)
  nsceGuEnable(GU_BLEND);
  sceGuEnable(GU_ALPHA_TEST);
  sceGuAlphaFunc(GU_GREATER, 0, 0xff);

  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);

  //st1x:now quake transparency is working
  //force_fullbright
  //sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);

  sceGuShadeModel(GU_SMOOTH);
  sceGumUpdateMatrix();
  IgnoreInterpolatioFrame(e, paliashdr);
  anim = (int)(cl.time*10) & 3;
  GL_Bind(paliashdr->gl_texturenum[e->skinnum][anim]);
  //Rendering block
  if (r_i_model_animation.value)
  {
          R_SetupAliasBlendedFrame (e->frame, paliashdr, e, e->angles[0],
e->angles[1]);
  }
  else
  {
          if (r_ipolations.value)//blubs: seems like we don't even use
InterpolatedFrames.
          {
                  if (r_asynch.value)
                  {
                          if (e->interpolation >= r_ipolations.value)
                          {
                                  e->last_frame = e->current_frame;
                                  e->current_frame = e->frame;
                                  e->interpolation = 1;
                          }
                  }
                  else
                  {
                          if (e->frame != e->current_frame)
                          {
                                  e->last_frame = e->current_frame;
                                  e->current_frame = e->frame;
                                  e->interpolation = 1;
                          }
                  }
                  R_SetupAliasInterpolatedFrame
(e->current_frame,e->last_frame,e->interpolation,paliashdr);
          }
          else
                  R_SetupAliasFrame (e->frame, paliashdr, e->angles[0],
e->angles[1]);
  }
  sceGumPopMatrix();
  sceGumUpdateMatrix();

  //st1x:now quake transparency is working
  sceGuAlphaFunc(GU_GREATER, 0, 0xff);
  sceGuDisable(GU_ALPHA_TEST);
  sceGuTexFunc(GU_TFX_REPLACE , GU_TCC_RGBA);
  sceGuShadeModel(GU_FLAT);

//	else if(ISGLOW(e))
  {
          sceGuDepthMask(GU_FALSE);
          //sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
          //sceGuDisable (GU_BLEND);
  }
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

  sceGuDisable(GU_BLEND);
  */

  aliashdr_t *paliashdr;
  int i, anim, skinnum;
  gltexture_t *tx, *fb;
  lerpdata_t lerpdata;
  qboolean alphatest = !!(e->model->flags & MF_HOLEY);

  //
  // setup pose/lerp data -- do it first so we don't miss updates due to culling
  //
  paliashdr = (aliashdr_t *)Mod_Extradata(e->model);
  R_SetupAliasFrame(paliashdr, e->frame, &lerpdata);
  R_SetupEntityTransform(e, &lerpdata);

  //
  // cull it
  //
  if (R_CullModelForEntity(e))
    return;

  //
  // transform it
  //
  glPushMatrix();
  R_RotateForEntity(lerpdata.origin, lerpdata.angles, e->scale);

  glTranslatef(paliashdr->scale_origin[0], paliashdr->scale_origin[1],
               paliashdr->scale_origin[2]);
  glScalef(paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

  //
  // random stuff
  //
#ifndef VITA
  if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
    glShadeModel(GL_SMOOTH);
  if (gl_affinemodels.value)
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
#endif
  //
  // set up lighting
  //
  rs_aliaspolys += paliashdr->numtris;
  R_SetupAliasLighting(e);

  //
  // set up textures
  //
  GL_DisableMultitexture();
  anim = (int)(cl.time * 10) & 3;
  skinnum = e->skinnum;
  if ((skinnum >= paliashdr->numskins) || (skinnum < 0)) {
    Con_DPrintf("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum,
                e->model->name);
    // ericw -- display skin 0 for winquake compatibility
    skinnum = 0;
  }
  tx = paliashdr->gltextures[skinnum][anim];

  //
  // draw it
  //
  GL_Bind(tx);
  glEnable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glDepthMask(GL_FALSE);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  Fog_DisableGFog();
  GL_DrawAliasFrame(paliashdr, lerpdata);
  Fog_EnableGFog();

cleanup:
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#ifndef VITA
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glShadeModel(GL_FLAT);
#endif
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glColor3f(1, 1, 1);
  glPopMatrix();
}

/*
=====================
R_DrawZombieLimb

=====================
*/
// Blubs Z hacks: need this declaration.
qmodel_t *Mod_FindName(char *name);

void R_DrawZombieLimb(entity_t *e, int which) {

  // entity_t *e;
  qmodel_t *clmodel;
  aliashdr_t *paliashdr;
  entity_t *limb_ent;
  lerpdata_t lerpdata;

  // e = &cl_entities[ent];
  // clmodel = e->model;

  if (which == 1)
    limb_ent = &cl_entities[e->z_head];
  else if (which == 2)
    limb_ent = &cl_entities[e->z_larm];
  else if (which == 3)
    limb_ent = &cl_entities[e->z_rarm];
  else
    return;

  clmodel = limb_ent->model;
  if (clmodel == NULL)
    return;

  VectorCopy(e->origin, r_entorigin);
  VectorSubtract(r_origin, r_entorigin, modelorg);

  // locate the proper data
  paliashdr = (aliashdr_t *)Mod_Extradata(clmodel); // e->model
  rs_aliaspolys += paliashdr->numtris;
  R_SetupAliasFrame(paliashdr, e->frame, &lerpdata);
  R_SetupEntityTransform(e, &lerpdata);

  glPushMatrix();

  R_RotateForEntity(lerpdata.origin, lerpdata.angles, e->scale);

  paliashdr = (aliashdr_t *)Mod_Extradata(e->model);
  rs_aliaspolys += paliashdr->numtris;

  if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
    glShadeModel(GL_SMOOTH);
  if (gl_affinemodels.value)
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

  overbright = gl_overbright_models.value;
  shading = true;

  // blubs disabled
  /*if (r_i_model_transform.value)
          R_BlendedRotateForEntity (e, 0);
  else
          R_RotateForEntity (e, 0);*/

  rs_aliaspolys += paliashdr->numtris;
  R_SetupAliasLighting(e);

  glTranslatef(paliashdr->scale_origin[0], paliashdr->scale_origin[1],
               paliashdr->scale_origin[2]);

  GL_DisableMultitexture();

  glScalef(paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

  // R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
  // R_SetupEntityTransform (e, &lerpdata);
  GL_DrawAliasFrame(paliashdr, lerpdata);

  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

#ifndef VITA
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glShadeModel(GL_FLAT);
#endif
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  // t3 += Sys_FloatTime();
  glPopMatrix();
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/

// extern int doZHack;
extern qboolean model_is_zombie(char name[MAX_QPATH]);
void R_DrawAliasModel(entity_t *e) {
  aliashdr_t *paliashdr;
  int i, anim, skinnum;
  char specChar;
  gltexture_t *tx, *fb, *sp;
  lerpdata_t lerpdata;
  qboolean alphatest = !!(e->model->flags & MF_HOLEY);
  // qmodel_t		*clmodel;
  vec3_t mins, maxs;

  // clmodel = e->model;

  //
  // setup pose/lerp data -- do it first so we don't miss updates due to culling
  //
  /*specChar = clmodel->name[strlen(clmodel->name) - 5];
  if(doZHack && specChar == '#')
  {
          if(clmodel->name[strlen(clmodel->name) - 6] == 'c')
                  paliashdr = (aliashdr_t *)
  Mod_Extradata(Mod_FindName("models/ai/zcfull.mdl")); else paliashdr =
  (aliashdr_t *) Mod_Extradata(Mod_FindName("models/ai/zfull.mdl"));
  }
  else*/
  paliashdr = (aliashdr_t *)Mod_Extradata(e->model);

  R_SetupAliasFrame(paliashdr, e->frame, &lerpdata);
  R_SetupEntityTransform(e, &lerpdata);

  //
  // cull it
  //
  if (R_CullModelForEntity(e))
    return;

  //
  // transform it
  //
  glPushMatrix();
  R_RotateForEntity(lerpdata.origin, lerpdata.angles, ENTSCALE_DEFAULT);
  /* //sB needs fixing in Quakespasm but fuck GL bro
  //specChar = clmodel->name[strlen(clmodel->name) - 5];
  if(doZHack && specChar == '#')
  {
          if(clmodel->name[strlen(clmodel->name) - 6] == 'c')
                  paliashdr = (aliashdr_t *)
  Mod_Extradata(Mod_FindName("models/ai/zcfull.mdl")); else paliashdr =
  (aliashdr_t *) Mod_Extradata(Mod_FindName("models/ai/zfull.mdl"));
  }
  else*/
  paliashdr = (aliashdr_t *)Mod_Extradata(e->model);

  rs_aliaspolys += paliashdr->numtris;

  // glPushMatrix ();

  //
  // random stuff
  //
#ifndef VITA
  if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
    glShadeModel(GL_SMOOTH);
  if (gl_affinemodels.value)
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
#endif
  overbright = gl_overbright_models.value;
  shading = true;

  // Special handling of view model scaling: none needed now that we render with
  // a separate viewmodel projection matrix.
  float scale = 1.0f;
  if (e->scale != ENTSCALE_DEFAULT && e->scale != 0)
    scale *= ENTSCALE_DECODE(e->scale);
  glTranslatef(paliashdr->scale_origin[0] * scale,
               paliashdr->scale_origin[1] * scale,
               paliashdr->scale_origin[2] * scale);
  glScalef(paliashdr->scale[0] * scale, paliashdr->scale[1] * scale,
           paliashdr->scale[2] * scale);

  //
  // set up for alpha blending
  //
  if (r_drawflat_cheatsafe ||
      r_lightmap_cheatsafe) // no alpha in drawflat or lightmap mode
    entalpha = 1;
  else
    entalpha = ENTALPHA_DECODE(e->alpha);
  if (entalpha == 0)
    goto cleanup;
  if (entalpha < 1) {
    if (!gl_texture_env_combine)
      overbright =
          false; // overbright can't be done in a single pass without combiners
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
  } else if (alphatest)
    glEnable(GL_ALPHA_TEST);

  //
  // set up lighting
  //
  rs_aliaspolys += paliashdr->numtris;
  R_SetupAliasLighting(e);

  //
  // set up textures
  //
  GL_DisableMultitexture();

  anim = (int)(cl.time * 10) & 3;
  skinnum = e->skinnum;
  if (((skinnum >= paliashdr->numskins) || (skinnum < 0)) &&
      !model_is_zombie(e->model->name)) {
    Con_DPrintf("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum,
                e->model->name);
    // ericw -- display skin 0 for winquake compatibility
    skinnum = 0;
  }
  tx = paliashdr->gltextures[skinnum][anim];
  fb = paliashdr->fbtextures[skinnum][anim];
  sp = paliashdr->spectextures[skinnum][anim]; // specular map

  if (e->colormap != vid.colormap && !gl_nocolors.value) {
    i = e - cl_entities;
    if (i >= 1 && i<=cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
      tx = playertextures[i - 1];
  }
  if (!gl_fullbrights.value)
    fb = NULL;

  //
  // draw it
  //
  if (r_drawflat_cheatsafe) {
    glDisable(GL_TEXTURE_2D);
    GL_DrawAliasFrame(paliashdr, lerpdata);
    glEnable(GL_TEXTURE_2D);
    srand((int)(cl.time * 1000)); // restore randomness
  } else if (r_fullbright_cheatsafe) {
    GL_Bind(tx);
    shading = false;
    glColor4f(1, 1, 1, entalpha);
    GL_DrawAliasFrame(paliashdr, lerpdata);
    if (fb) {
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
      GL_Bind(fb);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE);
      glDepthMask(GL_FALSE);
      glColor3f(entalpha, entalpha, entalpha);
      Fog_StartAdditive();
      GL_DrawAliasFrame(paliashdr, lerpdata);
      Fog_StopAdditive();
      glDepthMask(GL_TRUE);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDisable(GL_BLEND);
    }
  } else if (r_lightmap_cheatsafe) {
    glDisable(GL_TEXTURE_2D);
    shading = false;
    glColor3f(1, 1, 1);
    GL_DrawAliasFrame(paliashdr, lerpdata);
    glEnable(GL_TEXTURE_2D);
  }
  // use the GLSL fast path if available (r_alias_program != 0). A cached model can lose
  // its VBO across a map reload (meshvbo==0) -> garbage/vertex explosion, so rebuild it
  // here and keep the GLSL path rather than falling back to (citron-corrupting) immediate mode.
  else if (r_alias_program != 0) {
    GLMesh_EnsureVertexBuffer(currententity->model, paliashdr);
    GL_DrawAliasFrame_GLSL(paliashdr, lerpdata, tx, fb, sp);
  } else if (overbright) {
    if (gl_texture_env_combine && gl_mtexable && gl_texture_env_add &&
        fb) // case 1: everything in one pass
    {
      GL_Bind(tx);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
      glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
      glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
      glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
      glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
      GL_EnableMultitexture(); // selects TEXTURE1
      GL_Bind(fb);
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
      glEnable(GL_BLEND);
      GL_DrawAliasFrame(paliashdr, lerpdata);
      glDisable(GL_BLEND);
      GL_DisableMultitexture();
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    } else if (gl_texture_env_combine) // case 2: overbright in one pass, then
                                       // fullbright pass
    {
      // first pass
      GL_Bind(tx);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
      glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
      glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
      glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
      glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
      GL_DrawAliasFrame(paliashdr, lerpdata);
      glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      // second pass
      if (fb) {
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        GL_Bind(fb);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        shading = false;
        glColor3f(entalpha, entalpha, entalpha);
        Fog_StartAdditive();
        GL_DrawAliasFrame(paliashdr, lerpdata);
        Fog_StopAdditive();
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      }
    } else // case 3: overbright in two passes, then fullbright pass
    {
      // first pass
      GL_Bind(tx);
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
      GL_DrawAliasFrame(paliashdr, lerpdata);
      // second pass -- additive with black fog, to double the object colors but
      // not the fog color
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE);
      glDepthMask(GL_FALSE);
      Fog_StartAdditive();
      GL_DrawAliasFrame(paliashdr, lerpdata);
      Fog_StopAdditive();
      glDepthMask(GL_TRUE);
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDisable(GL_BLEND);
      // third pass
      if (fb) {
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        GL_Bind(fb);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        shading = false;
        glColor3f(entalpha, entalpha, entalpha);
        Fog_StartAdditive();
        GL_DrawAliasFrame(paliashdr, lerpdata);
        Fog_StopAdditive();
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      }
    }
  } else {
    if (gl_mtexable && gl_texture_env_add &&
        fb) // case 4: fullbright mask using multitexture
    {
      GL_DisableMultitexture(); // selects TEXTURE0
      GL_Bind(tx);
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
      GL_EnableMultitexture(); // selects TEXTURE1
      GL_Bind(fb);
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
      glEnable(GL_BLEND);
      GL_DrawAliasFrame(paliashdr, lerpdata);
      glDisable(GL_BLEND);
      GL_DisableMultitexture();
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    } else // case 5: fullbright mask without multitexture
    {
      // first pass
      GL_Bind(tx);
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
      GL_DrawAliasFrame(paliashdr, lerpdata);
      // second pass
      if (fb) {
        GL_Bind(fb);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        shading = false;
        glColor3f(entalpha, entalpha, entalpha);
        Fog_StartAdditive();
        GL_DrawAliasFrame(paliashdr, lerpdata);
        Fog_StopAdditive();
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
      }
    }
  }

  // glPopMatrix ();
  /*
  if (doZHack == 0 && specChar == '#')//if we're drawing zombie, also draw its
  limbs in one call
          {
                  if(e->z_head)
                          R_DrawZombieLimb(e,1);
                  if(e->z_larm)
                          R_DrawZombieLimb(e,2);
                  if(e->z_rarm)
                          R_DrawZombieLimb(e,3);
          }*/

cleanup:
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#ifndef VITA
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glShadeModel(GL_FLAT);
#endif
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  if (alphatest)
    glDisable(GL_ALPHA_TEST);
  glColor3f(1, 1, 1);
  glPopMatrix();
}

// johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.3 // skew along x axis. lower than glquake's -0.7 -> grounded under the model
#define SHADOW_SKEW_Y 0    // skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0    // 0=completely flat
#define SHADOW_HEIGHT 0.1  // how far above the floor to render the shadow
// johnfitz

/*
=============
GL_DrawAliasShadow -- johnfitz -- rewritten

TODO: orient shadow onto "lightplane" (a global mplane_t*)
=============
*/
void GL_DrawAliasShadow(entity_t *e) {
  float shadowmatrix[16] = {1,
                            0,
                            0,
                            0,
                            0,
                            1,
                            0,
                            0,
                            SHADOW_SKEW_X,
                            SHADOW_SKEW_Y,
                            SHADOW_VSCALE,
                            0,
                            0,
                            0,
                            SHADOW_HEIGHT,
                            1};
  float lheight, height, alpha;
  aliashdr_t *paliashdr;
  lerpdata_t lerpdata;

  if (!r_aliasshadow_program)
    return;
  if (R_CullModelForEntity(e))
    return;
  if (e == &cl.viewent || e->model->flags & MOD_NOSHADOW)
    return;
  if (ENTALPHA_DECODE(e->alpha) == 0)
    return;

  paliashdr = (aliashdr_t *)Mod_Extradata(e->model);
  R_SetupAliasFrame(paliashdr, e->frame, &lerpdata);
  R_SetupEntityTransform(e, &lerpdata);
  R_LightPoint(e->origin);
  lheight = currententity->origin[2] - lightspot[2];
  height = lheight;
  if (height < -8.0f || height > 64.0f)	// skip models well off the floor (hanging lamps etc)
    return;
  alpha = 0.85f * (1.0f - height / 64.0f);	// dark + easily seen, fades only as it lifts off the floor
  if (alpha <= 0.02f)
    return;
  if (alpha > 0.85f) alpha = 0.85f;

  // flatten the model onto the floor plane via the matrix stack (the GLSL shadow
  // program reads it, unlike the old fixed-function path that ghosted on citron)
  glPushMatrix();
  glTranslatef(lerpdata.origin[0], lerpdata.origin[1], lerpdata.origin[2]);
  glTranslatef(0, 0, -lheight);
  glMultMatrixf(shadowmatrix);
  glTranslatef(0, 0, lheight);
  glRotatef(lerpdata.angles[1], 0, 0, 1);
  glRotatef(-lerpdata.angles[0], 0, 1, 0);
  glRotatef(lerpdata.angles[2], 1, 0, 0);
  glTranslatef(paliashdr->scale_origin[0], paliashdr->scale_origin[1],
               paliashdr->scale_origin[2]);
  glScalef(paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

  GL_UseProgramFunc(r_aliasshadow_program);
  GL_BindBuffer(GL_ARRAY_BUFFER, e->model->meshvbo);
  GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, e->model->meshindexesvbo);
  GL_EnableVertexAttribArrayFunc(pose1VertexAttrIndex);
  GL_EnableVertexAttribArrayFunc(pose2VertexAttrIndex);
  GL_VertexAttribPointerFunc(pose1VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE,
                             sizeof(meshxyz_t),
                             GLARB_GetXYZOffset(paliashdr, lerpdata.pose1));
  GL_VertexAttribPointerFunc(pose2VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE,
                             sizeof(meshxyz_t),
                             GLARB_GetXYZOffset(paliashdr, lerpdata.pose2));
  GL_Uniform1fFunc(shadow_blendLoc, lerpdata.blend);
  GL_Uniform1fFunc(shadow_alphaLoc, alpha);

  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);	// flattened mesh winding is arbitrary

  glDrawElements(GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT,
                 (void *)(intptr_t)e->model->vboindexofs);

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  GL_DisableVertexAttribArrayFunc(pose1VertexAttrIndex);
  GL_DisableVertexAttribArrayFunc(pose2VertexAttrIndex);
  GL_BindBuffer(GL_ARRAY_BUFFER, 0);
  GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  GL_UseProgramFunc(0);
  glPopMatrix();
}

/*
=============
GL_DrawAliasBlobShadow -- soft round contact shadow pooled on the floor under a
model (replaces the ugly skewed-silhouette projection). Dark center fading to a
transparent rim = a soft grounded shadow that always reads clean.
=============
*/
void GL_DrawAliasBlobShadow(entity_t *e) {
  vec3_t floorpos;
  float height, m, rad;
  int i;
  const int segs = 18;

  if (e == &cl.viewent || (e->model->flags & MOD_NOSHADOW))
    return;
  if (R_CullModelForEntity(e))
    return;
  if (ENTALPHA_DECODE(e->alpha) == 0)
    return;

  R_LightPoint(e->origin);          // sets lightspot to the surface below the model
  height = e->origin[2] - lightspot[2];
  if (height < -8.0f || height > 96.0f) // not standing near a floor -> no shadow
    return;

  // darkness (0..0.8) -> floor multiplier m (1=unchanged .. 0.2=strongly darkened)
  {
    float dark = 0.8f * (1.0f - height / 96.0f);
    if (dark <= 0.02f) return;
    if (dark > 0.8f) dark = 0.8f;
    m = 1.0f - dark;
  }

  rad = (e->model->maxs[0] - e->model->mins[0] +
         e->model->maxs[1] - e->model->mins[1]) * 0.25f; // mean horizontal half-extent
  if (rad < 18.0f) rad = 18.0f;
  if (rad > 40.0f) rad = 40.0f;

  floorpos[0] = e->origin[0];
  floorpos[1] = e->origin[1];
  floorpos[2] = lightspot[2] + 0.6f;  // just above the floor to avoid z-fighting

  GL_UseProgramFunc(0);		// unbind the world GLSL shader or immediate-mode draws nothing
  GL_DisableMultitexture();
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);	// a single floor-plane disc must show regardless of winding/view
  glEnable(GL_BLEND);
  glBlendFunc(GL_ZERO, GL_SRC_COLOR);	// MULTIPLY: dst *= src -> can only darken, never brighten
  glDepthMask(GL_FALSE);
  glShadeModel(GL_SMOOTH);

  // solid dark core (inner 55%): multiply the floor toward m
  glBegin(GL_TRIANGLE_FAN);
  glColor3f(m, m, m);
  glVertex3f(floorpos[0], floorpos[1], floorpos[2]);
  for (i = 0; i <= segs; i++) {
    float a = (float)i / segs * 2.0f * M_PI;
    glVertex3f(floorpos[0] + cos(a) * rad * 0.55f, floorpos[1] + sin(a) * rad * 0.55f, floorpos[2]);
  }
  glEnd();

  // soft halo: ring from the dark core edge (m) out to white (1=no change) = soft fade
  glBegin(GL_TRIANGLE_STRIP);
  for (i = 0; i <= segs; i++) {
    float a = (float)i / segs * 2.0f * M_PI;
    float c = cos(a), s = sin(a);
    glColor3f(m, m, m);
    glVertex3f(floorpos[0] + c * rad * 0.55f, floorpos[1] + s * rad * 0.55f, floorpos[2]);
    glColor3f(1, 1, 1);
    glVertex3f(floorpos[0] + c * rad, floorpos[1] + s * rad, floorpos[2]);
  }
  glEnd();

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_TEXTURE_2D);
  glColor4f(1, 1, 1, 1);
}

/*
=================
R_DrawAliasModel_ShowTris -- johnfitz
=================
*/
void R_DrawAliasModel_ShowTris(entity_t *e) {
  aliashdr_t *paliashdr;
  lerpdata_t lerpdata;

  if (R_CullModelForEntity(e))
    return;

  paliashdr = (aliashdr_t *)Mod_Extradata(e->model);
  R_SetupAliasFrame(paliashdr, e->frame, &lerpdata);
  R_SetupEntityTransform(e, &lerpdata);

  glPushMatrix();
  R_RotateForEntity(lerpdata.origin, lerpdata.angles, e->scale);
  glTranslatef(paliashdr->scale_origin[0], paliashdr->scale_origin[1],
               paliashdr->scale_origin[2]);
  glScalef(paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

  shading = false;
  glColor3f(1, 1, 1);
  GL_DrawAliasFrame(paliashdr, lerpdata);

  glPopMatrix();
}
