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
// chase.c -- chase camera code

#include "quakedef.h"

cvar_t chase_back = {"chase_back", "100", CVAR_NONE};
cvar_t chase_up = {"chase_up", "16", CVAR_NONE};
cvar_t chase_right = {"chase_right", "0", CVAR_NONE};
cvar_t chase_active = {"chase_active", "0", CVAR_ARCHIVE};
cvar_t chase_side = {"chase_side", "1", CVAR_ARCHIVE}; // 1=right-shoulder, -1=left-shoulder
// mirrors Chase_IsFirstPersonForced() so server-side QC (aim convergence,
// third-person weapon entity) can tell when R3/sniper has forced first-person.
cvar_t chase_firstperson = {"chase_firstperson", "0", CVAR_NONE};

// Camera smoothing - static variables to interpolate position
static vec3_t chase_dest;       // Target camera position
static vec3_t chase_angles_dest; // Target camera angles
static qboolean chase_initialized = false;

// First-person ADS toggle while in third-person mode
static qboolean ads_firstperson_active = false;  // R3 toggled to first-person while ADS
static qboolean was_aiming = false;  // Track previous ADS state to detect un-aiming

/*
==============
Chase_ToggleSide_f
Toggle between right and left shoulder view.
Only works when chase_active is enabled.
==============
*/
// min seconds between accepted toggle presses -- the controller path can deliver one
// press as multiple keydowns, firing a toggle twice so it undoes itself.
#define CHASE_TOGGLE_DEBOUNCE 0.25

static void Chase_ToggleSide_f(void) {
  static double last_toggle = 0;

  // Only toggle when in third-person mode
  if (!chase_active.value) {
    Con_Printf("Shoulder swap only works in third-person mode\n");
    return;
  }

  if (realtime - last_toggle < CHASE_TOGGLE_DEBOUNCE)
    return;
  last_toggle = realtime;

  // Toggle between right (1) and left (-1)
  if (chase_side.value > 0)
    Cvar_SetValue("chase_side", -1);
  else
    Cvar_SetValue("chase_side", 1);

  Con_Printf("Camera shoulder: %s\n", chase_side.value > 0 ? "right" : "left");
}

/*
==============
Chase_L3Action_f
Combined L3 button action: shoulder swap when ADS in third-person, otherwise sprint.
This allows L3 to serve double duty on controllers.
==============
*/
static void Chase_L3Action_f(void) {
  static double last_swap = 0;

  // If in third-person mode AND aiming (STAT_ZOOM == 1), toggle shoulder
  if (chase_active.value && cl.stats[STAT_ZOOM] == 1) {
    // Debounce so one press is one swap (see CHASE_TOGGLE_DEBOUNCE note).
    if (realtime - last_swap < CHASE_TOGGLE_DEBOUNCE)
      return;
    last_swap = realtime;
    // Toggle shoulder swap
    if (chase_side.value > 0)
      Cvar_SetValue("chase_side", -1);
    else
      Cvar_SetValue("chase_side", 1);
  } else {
    // Otherwise, execute sprint (impulse 23)
    Cmd_ExecuteString("impulse 23", src_command);
  }
}

/*
==============
Chase_R3Action_f
R3 button action: Toggle between first-person and third-person view while ADS.
When in third-person mode and aiming, pressing R3 switches to first-person aim.
Press R3 again or un-aim to return to third-person.
==============
*/
static void Chase_R3Action_f(void) {
  static double last_r3 = 0;

  // Only works when in third-person mode
  if (!chase_active.value) {
    return;
  }

  // debounce so one press is one toggle (else it fires twice and never engages,
  // leaving the offset third-person camera with bullets beside the crosshair).
  if (realtime - last_r3 < CHASE_TOGGLE_DEBOUNCE)
    return;
  last_r3 = realtime;

  // Toggle first-person when aiming (STAT_ZOOM == 1)
  if (cl.stats[STAT_ZOOM] == 1) {
    ads_firstperson_active = !ads_firstperson_active;
  }
}

/*
==============
Chase_IsFirstPersonForced
Returns true if we should force first-person view:
- Sniper ADS (STAT_ZOOM == 2)
- R3 toggled first-person while regular ADS
- Holding a primed grenade (STAT_HOLDING_GRENADE)
Called from view.c to determine if chase camera should be used
==============
*/
qboolean Chase_IsFirstPersonForced(void) {
  qboolean result = false;

  // Only relevant in chase (third-person) mode.
  if (chase_active.value) {
    // STAT_ZOOM: 1 = ADS, 2 = sniper zoom; STAT_HOLDING_GRENADE: primed grenade.
    qboolean is_aiming = (cl.stats[STAT_ZOOM] == 1);
    qboolean is_sniper_zoom = (cl.stats[STAT_ZOOM] == 2);
    qboolean is_holding_grenade = (cl.stats[STAT_HOLDING_GRENADE] != 0);

    // If we stopped aiming (and not holding grenade), reset the R3 toggle.
    if (was_aiming && !is_aiming && !is_sniper_zoom && !is_holding_grenade)
      ads_firstperson_active = false;
    was_aiming = is_aiming || is_sniper_zoom;

    // Force first person for: primed grenade, sniper ADS, or R3-toggled ADS.
    if (is_holding_grenade || is_sniper_zoom || (is_aiming && ads_firstperson_active))
      result = true;
  }

  // Mirror the state into a cvar so server-side QC can hide the third-person
  // weapon entity and skip the third-person aim convergence while forced FP.
  if (chase_firstperson.value != (result ? 1.0f : 0.0f))
    Cvar_SetValue("chase_firstperson", result ? 1 : 0);

  return result;
}

/*
==============
Chase_Init
==============
*/
void Chase_Init(void) {
  Cvar_RegisterVariable(&chase_firstperson);
  Cvar_RegisterVariable(&chase_back);
  Cvar_RegisterVariable(&chase_up);
  Cvar_RegisterVariable(&chase_right);
  Cvar_RegisterVariable(&chase_active);
  Cvar_RegisterVariable(&chase_side);

  // Register shoulder toggle command
  Cmd_AddCommand("toggle_chase_side", Chase_ToggleSide_f);

  // Register combined L3 action: shoulder swap when ADS, sprint otherwise
  // Bind LTHUMB to "l3_action" for smart dual-purpose L3
  Cmd_AddCommand("l3_action", Chase_L3Action_f);

  // Register R3 action: first-person toggle while ADS in third-person
  // Bind RTHUMB to "r3_action" for temporary first-person ADS
  Cmd_AddCommand("r3_action", Chase_R3Action_f);

  // Set default bindings for L3/R3 actions if not already bound
  // K_LTHUMB = 243, K_RTHUMB = 244 (from keys.h)
  extern void Key_SetBinding(int keynum, const char *binding);
  Key_SetBinding(243, "l3_action");   // LTHUMB -> l3_action (sprint or shoulder swap)
  Key_SetBinding(244, "r3_action");   // RTHUMB -> r3_action (first-person ADS toggle)
}

/*
==============
TraceLine

TODO: impact on bmodels, monsters
==============
*/
void TraceLine(vec3_t start, vec3_t end, vec3_t impact) {
  trace_t trace;

  memset(&trace, 0, sizeof(trace));
  SV_RecursiveHullCheck(cl.worldmodel->hulls, 0, 0, 1, start, end, &trace);

  VectorCopy(trace.endpos, impact);
}

/*
==============
Chase_UpdateForClient -- johnfitz -- orient client based on camera. called after
input
==============
*/
void Chase_UpdateForClient(void) {
  // place camera

  // assign client angles to camera

  // see where camera points

  // adjust client angles to point at the same place
}

/*
==============
Chase_UpdateForDrawing -- johnfitz -- orient camera based on client. called
before drawing

TODO: stay at least 8 units away from all walls in this leaf
==============
*/
void Chase_UpdateForDrawing(void) {
  int i;
  vec3_t forward, up, right;
  vec3_t ideal;
  vec3_t eye_org;
  float back_val, up_val, right_val;
  float pitch;
  trace_t trace;

  // 1. Safety Check: Ensure the player entity actually exists
  if (cl.viewentity < 0 || cl.viewentity >= MAX_EDICTS)
    return;

  // 2. Safety Check: Ensure worldmodel is valid
  if (!cl.worldmodel || !cl.worldmodel->hulls)
    return;

  // Get player pitch for look-down adjustment
  pitch = cl.viewangles[PITCH];

  // 3. Get the direction the player is looking (use only YAW for camera orbit)
  // This prevents camera from going underground when looking down
  vec3_t cam_angles;
  cam_angles[PITCH] = 0; // Ignore pitch for camera positioning
  cam_angles[YAW] = cl.viewangles[YAW];
  cam_angles[ROLL] = 0;
  AngleVectors(cam_angles, forward, right, up);

  // 4. Set the anchor to the player's eye level
  VectorCopy(cl_entities[cl.viewentity].origin, eye_org);
  eye_org[2] += cl.viewheight;

  // over-the-shoulder offsets by state. STAT_ZOOM: 1=ADS 2=zoom 3=sprint;
  // STAT_HOLDING_GRENADE 1=primed; chase_side 1=right -1=left.
  float is_left = (chase_side.value < 0) ? 1.0f : 0.0f;

  if (cl.stats[STAT_ZOOM] == 1) // Aiming Down Sights
  {
    back_val = 45.0f;  // Pull in closer when aiming
    up_val = 4.0f;     // Lower camera for ADS
    // Left shoulder uses smaller offset to keep crosshair aligned
    right_val = is_left ? -18.0f : 22.0f;
  }
  else if (cl.stats[STAT_HOLDING_GRENADE]) // Holding grenade - zoom in for better throwing view
  {
    back_val = 50.0f;  // Closer than normal but not as close as ADS
    up_val = 10.0f;    // Slightly higher to see trajectory better
    right_val = is_left ? -16.0f : 20.0f;
  }
  else // Normal or sprinting - use same distance to prevent snap
  {
    back_val = 70.0f;
    up_val = 14.0f;
    right_val = is_left ? -15.0f : 18.0f;
  }

  // adjust up offset by pitch: raise the camera when looking down (avoids going
  // underground), lower it slightly when looking up.
  float pitch_factor = pitch / 90.0f; // -1 to 1
  up_val += pitch_factor * 20.0f; // Add up to 20 units when looking straight down

  // 7. Calculate the "Ideal" position behind the player
  for (i = 0; i < 3; i++) {
    ideal[i] = eye_org[i] - (forward[i] * back_val) + (right[i] * right_val);
  }
  ideal[2] += up_val;

  // 8. Wall Clipping: Trace from player to ideal, only adjust if blocked
  memset(&trace, 0, sizeof(trace));
  trace.fraction = 1.0f;
  SV_RecursiveHullCheck(cl.worldmodel->hulls, 0, 0, 1, eye_org, ideal, &trace);

  vec3_t target_pos;
  if (trace.fraction < 0.95f && !trace.startsolid) {
    // Hit a wall - use interpolated position with pullback
    for (i = 0; i < 3; i++) {
      target_pos[i] = eye_org[i] + (ideal[i] - eye_org[i]) * (trace.fraction * 0.85f);
    }
  } else {
    VectorCopy(ideal, target_pos);
  }

  // 9. Camera Smoothing: Interpolate to target position
  if (!chase_initialized) {
    VectorCopy(target_pos, chase_dest);
    chase_initialized = true;
  }

  // Smooth interpolation factor (higher = faster, max 1.0)
  // Use faster smoothing when ADS to reduce jitter
  float smooth = (cl.stats[STAT_ZOOM] == 1) ? 0.6f : 0.25f;
  for (i = 0; i < 3; i++) {
    chase_dest[i] += (target_pos[i] - chase_dest[i]) * smooth;
    r_refdef.vieworg[i] = chase_dest[i];
  }

  // 10. Set camera angles to match player view (with pitch)
  // Don't try to calculate look-at angles - just use player angles directly
  r_refdef.viewangles[PITCH] = cl.viewangles[PITCH];
  r_refdef.viewangles[YAW] = cl.viewangles[YAW];
  r_refdef.viewangles[ROLL] = 0;

  // Clamp pitch to prevent camera flip
  if (r_refdef.viewangles[PITCH] > 80)
    r_refdef.viewangles[PITCH] = 80;
  if (r_refdef.viewangles[PITCH] < -80)
    r_refdef.viewangles[PITCH] = -80;
}

/*
==============
Chase_Reset
Reset camera smoothing (call when map changes or chase mode toggles)
==============
*/
void Chase_Reset(void) {
  chase_initialized = false;
}

/*
==============
GrenadeTrajectory_Draw
Draw grenade trajectory arc and impact circle when in third-person
and player is holding a primed grenade.
==============
*/
#define GTRAJ_GRAVITY     800.0f   // Match sv_gravity
#define GTRAJ_SPEED       1400.0f  // Match nade.velocity = v_forward * 1400
#define GTRAJ_TIMESTEP    0.04f    // Simulation step (~25 steps/sec)
#define GTRAJ_MAXSTEPS    60       // Max simulation steps (~2.4 sec)
#define GTRAJ_IMPACT_SEGS 24       // Segments for impact circle
#define GTRAJ_IMPACT_RAD  50.0f    // Radius of impact circle

void GrenadeTrajectory_Draw(void) {
  int i, step, total_steps;
  vec3_t forward, right, up;
  vec3_t launch_pos, vel, prev_pos, next_pos;
  vec3_t hit_pos, hit_norm;
  vec3_t arc_pts[GTRAJ_MAXSTEPS + 1];
  trace_t trace;
  float hit_frac;

  // Only draw in third-person when holding grenade
  if (!chase_active.value)
    return;
  if (!cl.stats[STAT_HOLDING_GRENADE])
    return;

  // Safety checks
  if (cl.viewentity < 0 || cl.viewentity >= MAX_EDICTS)
    return;
  if (!cl.worldmodel || !cl.worldmodel->hulls)
    return;

  // Get player view angles - this matches how QuakeC throws grenades
  AngleVectors(cl.viewangles, forward, right, up);

  // Launch point: player eye position + forward offset (matches QuakeC)
  VectorCopy(cl_entities[cl.viewentity].origin, launch_pos);
  launch_pos[2] += cl.viewheight;
  VectorMA(launch_pos, 12.0f, forward, launch_pos);

  // Initial velocity in player's forward direction (matches QuakeC: velocity = v_forward * 1400)
  VectorScale(forward, GTRAJ_SPEED, vel);

  VectorCopy(launch_pos, prev_pos);
  VectorCopy(launch_pos, hit_pos);
  hit_norm[0] = 0; hit_norm[1] = 0; hit_norm[2] = 1;
  hit_frac = 1.0f;

  // Store first arc point
  VectorCopy(prev_pos, arc_pts[0]);
  total_steps = 0;

  // Simulate trajectory
  for (step = 1; step <= GTRAJ_MAXSTEPS; step++) {
    // Semi-implicit Euler: apply gravity then move
    vel[2] -= GTRAJ_GRAVITY * GTRAJ_TIMESTEP;
    VectorMA(prev_pos, GTRAJ_TIMESTEP, vel, next_pos);

    // Trace this segment
    memset(&trace, 0, sizeof(trace));
    SV_RecursiveHullCheck(cl.worldmodel->hulls, 0, 0, 1, prev_pos, next_pos, &trace);

    VectorCopy(trace.endpos, arc_pts[step]);
    total_steps = step;

    if (trace.fraction < 1.0f) {
      // Hit something
      VectorCopy(trace.endpos, hit_pos);
      VectorCopy(trace.plane.normal, hit_norm);
      hit_frac = trace.fraction;
      break;
    }

    VectorCopy(next_pos, prev_pos);
  }

  if (total_steps < 1)
    return;

  // --- Draw the arc ---
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);  // Draw on top
  glLineWidth(3.0f);

  glBegin(GL_LINE_STRIP);
  for (i = 0; i <= total_steps; i++) {
    // Fade from orange-yellow to red
    float t = (float)i / (float)total_steps;
    float r = 1.0f;
    float g = 0.6f * (1.0f - t);
    float b = 0.1f * (1.0f - t);
    float a = 0.85f * (1.0f - t * 0.3f);
    glColor4f(r, g, b, a);
    glVertex3fv(arc_pts[i]);
  }
  glEnd();

  // --- Draw impact circle ---
  if (hit_frac < 1.0f) {
    vec3_t circle_right, circle_fwd;
    float angle;

    // Build local coordinate frame on surface
    if (fabs(hit_norm[2]) < 0.9f) {
      vec3_t world_up = {0, 0, 1};
      CrossProduct(hit_norm, world_up, circle_right);
      VectorNormalize(circle_right);
    } else {
      vec3_t world_fwd = {1, 0, 0};
      CrossProduct(hit_norm, world_fwd, circle_right);
      VectorNormalize(circle_right);
    }
    CrossProduct(circle_right, hit_norm, circle_fwd);

    // Filled translucent red disc
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(1.0f, 0.0f, 0.0f, 0.25f);
    // Center vertex (slightly above surface)
    glVertex3f(hit_pos[0] + hit_norm[0] * 2,
               hit_pos[1] + hit_norm[1] * 2,
               hit_pos[2] + hit_norm[2] * 2);
    for (i = 0; i <= GTRAJ_IMPACT_SEGS; i++) {
      angle = (float)i * (2.0f * M_PI / GTRAJ_IMPACT_SEGS);
      vec3_t rim;
      rim[0] = hit_pos[0] + hit_norm[0] * 2
             + circle_right[0] * cos(angle) * GTRAJ_IMPACT_RAD
             + circle_fwd[0] * sin(angle) * GTRAJ_IMPACT_RAD;
      rim[1] = hit_pos[1] + hit_norm[1] * 2
             + circle_right[1] * cos(angle) * GTRAJ_IMPACT_RAD
             + circle_fwd[1] * sin(angle) * GTRAJ_IMPACT_RAD;
      rim[2] = hit_pos[2] + hit_norm[2] * 2
             + circle_right[2] * cos(angle) * GTRAJ_IMPACT_RAD
             + circle_fwd[2] * sin(angle) * GTRAJ_IMPACT_RAD;
      glVertex3fv(rim);
    }
    glEnd();

    // Bright red outline
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glColor4f(1.0f, 0.1f, 0.1f, 0.9f);
    for (i = 0; i < GTRAJ_IMPACT_SEGS; i++) {
      angle = (float)i * (2.0f * M_PI / GTRAJ_IMPACT_SEGS);
      vec3_t rim;
      rim[0] = hit_pos[0] + hit_norm[0] * 2
             + circle_right[0] * cos(angle) * GTRAJ_IMPACT_RAD
             + circle_fwd[0] * sin(angle) * GTRAJ_IMPACT_RAD;
      rim[1] = hit_pos[1] + hit_norm[1] * 2
             + circle_right[1] * cos(angle) * GTRAJ_IMPACT_RAD
             + circle_fwd[1] * sin(angle) * GTRAJ_IMPACT_RAD;
      rim[2] = hit_pos[2] + hit_norm[2] * 2
             + circle_right[2] * cos(angle) * GTRAJ_IMPACT_RAD
             + circle_fwd[2] * sin(angle) * GTRAJ_IMPACT_RAD;
      glVertex3fv(rim);
    }
    glEnd();
  }

  // Restore GL state
  glLineWidth(1.0f);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);
  glColor4f(1, 1, 1, 1);
}
