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

#include "quakedef.h"
#include "bgmusic.h"
#include <dirent.h>

#ifdef VITA
#include <psp2/io/fcntl.h>
#endif

#ifdef VITA
#include <psp2/touch.h>
#endif // VITA

// anti-enum propaganda
#define OPT_CSETTING_LSENS 420
#define OPT_CSETTING_LACC 421
#define OPT_GSETTING_MAXFPS 422
#define OPT_GSETTING_FOV 423
#define OPT_GSETTING_GAMMA 424
#define OPT_CSETTING_GSEX 425
#define OPT_CSETTING_GSEY 426
#define OPT_CSETTING_RUMBLE 427
// end propagating

// Skips the static bg image while the security-camera feed is live behind the
// menu.
#ifdef VITA
#define Draw_BgMenu()                                                          \
  do {                                                                         \
    extern cvar_t cam_tour;                                                    \
    if (!cam_tour.value)                                                       \
      Draw_StretchPic(0, 0, menu_bk, vid.width, vid.height);                   \
  } while (0)
#else
#define Draw_BgMenu()                                                          \
  do {                                                                         \
    extern cvar_t cam_tour;                                                    \
    if (!cam_tour.value)                                                       \
      Draw_StretchPic(0, vid.height * 0.5, menu_bk, vid.width / 2,             \
                      vid.height / 2);                                         \
  } while (0)
#endif

extern cvar_t waypoint_mode;
extern cvar_t in_aimassist;
extern cvar_t joy_invert;
extern cvar_t crosshair;
extern cvar_t scr_showfps;
extern cvar_t scr_dynamic_fov;
extern cvar_t host_maxfps;
extern cvar_t r_fullbright;
extern cvar_t gl_texturemode;
extern cvar_t scr_fov;
extern cvar_t motioncam;
extern cvar_t gyromode;
extern cvar_t gyrosensx;
extern cvar_t gyrosensy;
extern cvar_t in_rumble;
extern cvar_t in_rumble_scale;
extern cvar_t chase_active;

cvar_t cl_enablereartouchpad = {"cl_enablereartouchpad", "0", CVAR_ARCHIVE};

extern int loadingScreen;
extern int ShowBlslogo;

extern char *loadname2;
extern char *loadnamespec;
extern qboolean loadscreeninit;

extern float cl_forwardspeed;

char *game_build_date;

qpic_t *menu_bk;
qpic_t *start_bk;
qpic_t *pause_bk;
qpic_t *social_badges;

void (*vid_menucmdfn)(void); // johnfitz
void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);

qboolean paused_hack;
qboolean paused_client; // true = joined client's pause menu
                        // (Resume/Settings/Leave Game)
int M_Paused_Cusor;

typedef struct {
  int occupied;
  int map_allow_game_settings;
  int map_use_thumbnail;
  char *map_name;
  char *map_name_pretty;
  char *map_desc_1;
  char *map_desc_2;
  char *map_desc_3;
  char *map_desc_4;
  char *map_desc_5;
  char *map_desc_6;
  char *map_desc_7;
  char *map_desc_8;
  char *map_author;
  char *map_thumbnail_path;
} usermap_t;

usermap_t custom_maps[50];

#define BASE_MAP_COUNT 4
char *base_maps[] = {"ndu", "nzp_warehouse", "nzp_warehouse2",
                     "christmas_special"};

enum m_state_e m_state;

int old_m_state;

void M_Start_Menu_f(void);

void M_Menu_Main_f(void);
void M_Paused_Menu_f(void);
void M_Menu_SinglePlayer_f(void);
void M_Menu_Maps_f(void);
void M_Menu_Restart_f(void);
void M_Menu_Exit_f(void);
void M_Menu_MultiPlayer_f(void);
void M_Menu_Setup_f(void);
void M_Menu_Net_f(void);
void M_Menu_LanConfig_f(void);
void M_Menu_GameOptions_f(void);
void M_Menu_Search_f(void);
void M_Menu_ServerList_f(void);
void M_Menu_Achievement_f(void);
void M_Menu_Loadout_f(void);
void M_Loadout_Draw(void);
void M_Loadout_Key(int key);
void M_Menu_Options_f(void);
void M_Menu_Keys_f(void);
void M_Control_Settings_f(void);
void M_Graphics_Settings_f(void);
void M_Menu_Video_f(void);
void M_Menu_Credits_f(void);
void M_Menu_Gamemodes_f(void);
void M_Menu_Quit_f(void);

void M_Main_Draw(void);
void M_SinglePlayer_Draw(void);
void M_Menu_Maps_Draw(void);
void M_Achievement_Draw(void);
void M_MultiPlayer_Draw(void);
void M_Setup_Draw(void);
void M_Net_Draw(void);
void M_LanConfig_Draw(void);
void M_GameOptions_Draw(void);
void M_Search_Draw(void);
void M_ServerList_Draw(void);
void M_Options_Draw(void);
void M_OSK_Draw(void);
void M_OSK_Open(char *target, int maxlen, qboolean numeric);
void M_OSK_Keydown(int key);
extern qboolean menu_osk_active;
void M_Keys_Draw(void);
void M_Control_Settings_Draw(void);
void M_Graphics_Settings_Draw(void);
void M_Video_Draw(void);
void M_Credits_Draw(void);
void M_Gamemodes_Draw(void);
void M_Quit_Draw(void);

void M_Main_Key(int key);
void M_SinglePlayer_Key(int key);
void M_Load_Key(int key);
void M_Save_Key(int key);
void M_Achievement_Key(int key);
void M_MultiPlayer_Key(int key);
void M_Setup_Key(int key);
void M_Net_Key(int key);
void M_LanConfig_Key(int key);
void M_GameOptions_Key(int key);
void M_Search_Key(int key);
void M_ServerList_Key(int key);
void M_Options_Key(int key);
void M_Keys_Key(int key);
void M_Control_Settings_Key(int key);
void M_Graphics_Settings_Key(int key);
void M_Video_Key(int key);
void M_Credits_Key(int key);
void M_Gamemodes_Key(int key);
void M_Quit_Key(int key);

qboolean m_entersound; // play after drawing a frame, so caching
                       // won't disrupt the sound
qboolean m_recursiveDraw;

enum m_state_e m_return_state;
qboolean m_return_onerror;
char m_return_reason[32];

#define StartingGame (m_multiplayer_cursor == 1)
#define JoiningGame (m_multiplayer_cursor == 0)
#define IPXConfig (m_net_cursor == 0)
#define TCPIPConfig (m_net_cursor == 1)

#define LINE_HEIGHT 2
#define LINE_COLOR 14

void M_ConfigureNetSubsystem(void);

//
// Macros to make menu design for NX & VITA easier
//
int menu_offset_y;

#ifdef VITA

#define OFFSET_SPACING 19

#define MENU_INITVARS()                                                        \
  int y = 0;                                                                   \
  menu_offset_y = y + 70;
#define DRAW_HEADER(title)                                                     \
  Draw_ColoredStringScale(10, y + 10, title, 1, 1, 1, 1, 4.0f);
#define DRAW_VERSIONSTRING()                                                   \
  Draw_ColoredStringScale(vid.width - getTextWidth(game_build_date, 2), y + 5, \
                          game_build_date, 1, 1, 1, 1, 2.0f);
#define DRAW_MENUOPTION(id, txt, cursor, divider)                              \
  {                                                                            \
    menu_offset_y += OFFSET_SPACING;                                           \
    if (cursor == id)                                                          \
      Draw_ColoredStringScale(10, menu_offset_y, txt, 1, 0, 0, 1, 2.0f);       \
    else                                                                       \
      Draw_ColoredStringScale(10, menu_offset_y, txt, 1, 1, 1, 1, 2.0f);       \
    if (divider == true) {                                                     \
      menu_offset_y += OFFSET_SPACING + 4;                                     \
      Draw_FillByColor(10, menu_offset_y, 325, 4, 220, 220, 220, 255);         \
      menu_offset_y -= OFFSET_SPACING / 3;                                     \
    }                                                                          \
  }
#define DRAW_BLANKOPTION(txt, divider)                                         \
  {                                                                            \
    menu_offset_y += OFFSET_SPACING;                                           \
    Draw_ColoredStringScale(10, menu_offset_y, txt, 0.5, 0.5, 0.5, 1, 2.0f);   \
    if (divider == true) {                                                     \
      menu_offset_y += OFFSET_SPACING + 4;                                     \
      Draw_FillByColor(10, menu_offset_y, 325, 4, 220, 220, 220, 255);         \
      menu_offset_y -= OFFSET_SPACING / 3;                                     \
    }                                                                          \
  }
#define DRAW_DESCRIPTION(txt)                                                  \
  Draw_ColoredStringScale(10, y + 475, txt, 1, 1, 1, 1, 2.0f);
#define DRAW_BACKBUTTON(id, cursor)                                            \
  {                                                                            \
    if (cursor == id)                                                          \
      Draw_ColoredStringScale(10, 500, "Back", 1, 0, 0, 1, 2.0f);              \
    else                                                                       \
      Draw_ColoredStringScale(10, 500, "Back", 1, 1, 1, 1, 2.0f);              \
  }
#define DRAW_MAPTHUMB(img)                                                     \
  Draw_StretchPic(x_map_info_disp + 245, y + 68, img, 450, 255);
#define DRAW_MAPDESC(id, txt)                                                  \
  Draw_ColoredStringScale(x_map_info_disp + 245, y + 329 + (18 * id), txt, 1,  \
                          1, 1, 1, 2.0f);
#define DRAW_MAPAUTHOR(id, txt)                                                \
  Draw_ColoredStringScale(x_map_info_disp + 245, y + 329 + (18 * id), txt, 1,  \
                          1, 0, 1, 2.0f);
#define DRAW_CREDITLINE(id, txt)                                               \
  Draw_ColoredStringScale(10, menu_offset_y + (OFFSET_SPACING * id), txt, 1,   \
                          1, 1, 1, 2.0f);
#define DRAW_SETTINGSVALUE(id, txt)                                            \
  Draw_ColoredStringScale(400, y + 70 + (OFFSET_SPACING * (id + 1)), txt, 1,   \
                          1, 1, 1, 2.0f);
#define DRAW_SLIDER(id, r)                                                     \
  M_DrawSlider(408, y + 70 + (OFFSET_SPACING * (id + 1)), r, 2.0f);

#else

#define OFFSET_SPACING 15

#define MENU_INITVARS()                                                        \
  int y = vid.height * 0.5;                                                    \
  menu_offset_y = y + 55;
#define DRAW_HEADER(title)                                                     \
  Draw_ColoredStringScale(10, y + 10, title, 1, 1, 1, 1, 3.0f);
#define DRAW_VERSIONSTRING()                                                   \
  Draw_ColoredStringScale(635 - getTextWidth(game_build_date, 1), y + 10,      \
                          game_build_date, 1, 1, 1, 1, 1.0f);
/* NZP PC-style: the selected row gets a black panel + red border sized to fit
   the TEXT, with red text. Unselected rows are plain white text. */
#define DRAW_MENUOPTION(id, txt, cursor, divider)                              \
  {                                                                            \
    menu_offset_y += OFFSET_SPACING;                                           \
    if (cursor == id) {                                                        \
      int _bw = (int)getTextWidth(txt, 1.5f) + 12;                             \
      Draw_FillByColor(6, menu_offset_y - 4, _bw, 20, 0, 0, 0, 230);           \
      Draw_FillByColor(6, menu_offset_y - 4, _bw, 2, 150, 0, 0, 255);          \
      Draw_FillByColor(6, menu_offset_y + 14, _bw, 2, 150, 0, 0, 255);         \
      Draw_FillByColor(6, menu_offset_y - 4, 2, 20, 150, 0, 0, 255);           \
      Draw_FillByColor(4 + _bw, menu_offset_y - 4, 2, 20, 150, 0, 0, 255);     \
      Draw_ColoredStringScale(11, menu_offset_y, txt, 1, 0, 0, 1, 1.5f);       \
    } else                                                                     \
      Draw_ColoredStringScale(10, menu_offset_y, txt, 1, 1, 1, 1, 1.5f);       \
    if (divider == true) {                                                     \
      menu_offset_y += OFFSET_SPACING + 4;                                     \
      Draw_FillByColor(10, menu_offset_y, 240, 3, 220, 220, 220, 255);         \
      menu_offset_y -= OFFSET_SPACING / 3;                                     \
    }                                                                          \
  }
#define DRAW_BLANKOPTION(txt, divider)                                         \
  {                                                                            \
    menu_offset_y += OFFSET_SPACING;                                           \
    Draw_ColoredStringScale(10, menu_offset_y, txt, 0.5, 0.5, 0.5, 1, 1.5f);   \
    if (divider == true) {                                                     \
      menu_offset_y += OFFSET_SPACING + 4;                                     \
      Draw_FillByColor(10, menu_offset_y, 240, 3, 220, 220, 220, 255);         \
      menu_offset_y -= OFFSET_SPACING / 3;                                     \
    }                                                                          \
  }
#define DRAW_DESCRIPTION(txt)                                                  \
  Draw_ColoredStringScale(10, y + 305, txt, 1, 1, 1, 1, 1.5f);
#define DRAW_BACKBUTTON(id, cursor)                                            \
  {                                                                            \
    if (cursor == id) {                                                        \
      int _bb = (int)getTextWidth("Back", 1.5f) + 12;                          \
      Draw_FillByColor(6, y + 331, _bb, 20, 0, 0, 0, 230);                     \
      Draw_FillByColor(6, y + 331, _bb, 2, 150, 0, 0, 255);                    \
      Draw_FillByColor(6, y + 349, _bb, 2, 150, 0, 0, 255);                    \
      Draw_FillByColor(6, y + 331, 2, 20, 150, 0, 0, 255);                     \
      Draw_FillByColor(4 + _bb, y + 331, 2, 20, 150, 0, 0, 255);               \
      Draw_ColoredStringScale(11, y + 335, "Back", 1, 0, 0, 1, 1.5f);          \
    } else                                                                     \
      Draw_ColoredStringScale(10, y + 335, "Back", 1, 1, 1, 1, 1.5f);          \
  }
#define DRAW_MAPTHUMB(img)                                                     \
  Draw_StretchPic(x_map_info_disp + 280, y + 45, img, 280, 160);
#define DRAW_MAPDESC(id, txt)                                                  \
  Draw_ColoredStringScale(x_map_info_disp + 280, y + 218 + (15 * id), txt, 1,  \
                          1, 1, 1, 1.45f);
#define DRAW_MAPAUTHOR(id, txt)                                                \
  Draw_ColoredStringScale(x_map_info_disp + 280, y + 218 + (15 * id), txt, 1,  \
                          1, 0, 1, 1.25f);
#define DRAW_CREDITLINE(id, txt)                                               \
  Draw_ColoredStringScale(10, menu_offset_y + ((OFFSET_SPACING - 2) * id),     \
                          txt, 1, 1, 1, 1, 1.25f);
#define DRAW_SETTINGSVALUE(id, txt)                                            \
  Draw_ColoredStringScale(300, y + 55 + (OFFSET_SPACING * (id + 1)), txt, 1,   \
                          1, 1, 1, 1.5f);
#define DRAW_SLIDER(id, r)                                                     \
  M_DrawSlider(308, y + 55 + (OFFSET_SPACING * (id + 1)), r, 1.0f);

#endif // VITA

/*
================
M_DrawCharacter

Draws one solid graphics character
================
*/
void M_DrawCharacter(int cx, int line, int num) {
  Draw_Character(cx, line, num);
}

void M_Print(int cx, int cy, const char *str) {
  while (*str) {
    M_DrawCharacter(cx, cy, (*str) + 128);
    str++;
    cx += 8;
  }
}

void M_PrintWhite(int cx, int cy, const char *str) {
  while (*str) {
    M_DrawCharacter(cx, cy, *str);
    str++;
    cx += 8;
  }
}

void M_DrawTransPic(int x, int y, qpic_t *pic) {
  Draw_Pic(
      x, y,
      pic); // johnfitz -- simplified becuase centering is handled elsewhere
}

void M_DrawPic(int x, int y, qpic_t *pic) {
  Draw_Pic(
      x, y,
      pic); // johnfitz -- simplified becuase centering is handled elsewhere
}

void M_DrawTransPicTranslate(int x, int y, qpic_t *pic, int top,
                             int bottom) // johnfitz -- more parameters
{
  Draw_TransPicTranslate(
      x, y, pic, top,
      bottom); // johnfitz -- simplified becuase centering is handled elsewhere
}

void M_DrawTextBox(int x, int y, int width, int lines) {
  qpic_t *p;
  int cx, cy;
  int n;

  // draw left side
  cx = x;
  cy = y;
  p = Draw_CachePic("gfx/box_tl.lmp");
  M_DrawTransPic(cx, cy, p);
  p = Draw_CachePic("gfx/box_ml.lmp");
  for (n = 0; n < lines; n++) {
    cy += 8;
    M_DrawTransPic(cx, cy, p);
  }
  p = Draw_CachePic("gfx/box_bl.lmp");
  M_DrawTransPic(cx, cy + 8, p);

  // draw middle
  cx += 8;
  while (width > 0) {
    cy = y;
    p = Draw_CachePic("gfx/box_tm.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_mm.lmp");
    for (n = 0; n < lines; n++) {
      cy += 8;
      if (n == 1)
        p = Draw_CachePic("gfx/box_mm2.lmp");
      M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_bm.lmp");
    M_DrawTransPic(cx, cy + 8, p);
    width -= 2;
    cx += 16;
  }

  // draw right side
  cy = y;
  p = Draw_CachePic("gfx/box_tr.lmp");
  M_DrawTransPic(cx, cy, p);
  p = Draw_CachePic("gfx/box_mr.lmp");
  for (n = 0; n < lines; n++) {
    cy += 8;
    M_DrawTransPic(cx, cy, p);
  }
  p = Draw_CachePic("gfx/box_br.lmp");
  M_DrawTransPic(cx, cy + 8, p);
}

//=============================================================================

#define MENU_MAIN 0
#define MENU_SINGLEPLAYER 1
#define MENU_DEFAULT 2
#define MENU_PAUSE 3
#define MENU_START 4

// naievil: TODO -- fading cyclical background?

void Menu_Background_Draw(int type) {
  qpic_t *bg;
  int i;

  switch (type) {
  case MENU_MAIN:
    bg = Draw_CachePic("gfx/menu/menu_background.tga");
    break;
  default:
    bg = Draw_CachePic("gfx/menu/menu_background.tga");
    break;
  }

  if (key_dest != key_menu_pause && old_m_state != m_paused_menu)
    Draw_StretchPic(0, vid.height * 0.5, bg, vid.width / 2, vid.height / 2);
}

//=============================================================================

int m_save_demonum;

/*
================
M_ToggleMenu_f
================
*/
void M_ToggleMenu_f(void) {
  m_entersound = true;

  if (key_dest == key_menu) {
    if (m_state != m_main) {
      M_Menu_Main_f();
      return;
    }

    // IN_Activate();
    key_dest = key_game;
    m_state = m_none;
    return;
  }
  if (key_dest == key_console) {
    Con_ToggleConsole_f();
  } else if (sv.active && (svs.maxclients > 1 || key_dest == key_game)) {
    paused_client = false;
    M_Paused_Menu_f();
  } else if (cls.state == ca_connected && key_dest == key_game) {
    // Joined client (sv.active is false): give them a pause menu over the live
    // game too, instead of dumping them to the main menu background.
    paused_client = true;
    M_Paused_Menu_f();
  } else {
    M_Menu_Main_f();
  }
}

void M_Paused_Menu_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_paused_menu;
  m_entersound = true;
  M_Paused_Cusor = 0;

  loadingScreen = 0;
  loadscreeninit = false;
}

#define Max_Paused_Items 4

void M_Paused_Menu_Draw(void) {
  paused_hack = true;
  MENU_INITVARS();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Header
  DRAW_HEADER("PAUSED");

  if (paused_client) {
    // Joined client: no Restart/End Game (host-only); offer Leave Game instead.
    DRAW_MENUOPTION(0, "Resume Carnage", M_Paused_Cusor, false);
    DRAW_MENUOPTION(1, "Settings", M_Paused_Cusor, false);
    DRAW_MENUOPTION(2, "Leave Game", M_Paused_Cusor, false);
  } else {
    DRAW_MENUOPTION(0, "Resume Carnage", M_Paused_Cusor, false);
    DRAW_MENUOPTION(1, "Restart", M_Paused_Cusor, false);
    DRAW_MENUOPTION(2, "Settings", M_Paused_Cusor, false);
    DRAW_MENUOPTION(3, "End Game", M_Paused_Cusor, false);
  }
}

void M_Paused_Menu_Key(int key) {
  switch (key) {
  case K_BBUTTON:
  case K_ESCAPE:
    paused_hack = false;
    key_dest = key_game;
    m_state = m_none;
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (++M_Paused_Cusor >= (paused_client ? 3 : Max_Paused_Items))
      M_Paused_Cusor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (--M_Paused_Cusor < 0)
      M_Paused_Cusor = (paused_client ? 3 : Max_Paused_Items) - 1;
    break;

  case K_ENTER:
  case K_ABUTTON:
    m_entersound = true;

    if (paused_client) {
      switch (M_Paused_Cusor) {
      case 0:
        // Resume
        paused_hack = false;
        key_dest = key_game;
        m_state = m_none;
        break;
      case 1:
        M_Menu_Options_f();
        break;
      case 2:
        // Leave Game: disconnect from the host and return to the main menu.
        paused_hack = false;
        Cbuf_AddText("disconnect\n");
        M_Menu_Main_f();
        break;
      }
      break;
    }

    switch (M_Paused_Cusor) {
    case 0:
      // Resume
      paused_hack = false;
      key_dest = key_game;
      m_state = m_none;
      break;
    case 1:
      // Restart
      M_Menu_Restart_f();
      break;
    case 2:
      //
      M_Menu_Options_f();
      break;
    case 3:
      paused_hack = false;
      // This is supposed to be to exit the map
      M_Menu_Exit_f();
      break;
    }
  }
}

void M_Start_Menu_f() {
  key_dest = key_menu;
  m_state = m_start;
  m_entersound = true;
  loadingScreen = 0;
}

static void M_Start_Menu_Draw() {
  qpic_t *start_bk;

  start_bk = Draw_CachePic("gfx/lscreen/lscreen.tga");
  Draw_StretchPic(0, 0, start_bk, vid.width, vid.height);
  // Use the useprint canvas because it's easier to draw things scaled well not
  // in console
  char *s = "Press start";
  GL_SetCanvas(CANVAS_USEPRINT);
  Draw_String((vid.width / 2 - (strlen(s) * 8)) / 2, vid.height * 0.85, s);
}
void M_Start_Key(int key) {
  switch (key) {
  default:
  case K_ESCAPE:
    S_LocalSound("sounds/menu/navigate.wav");
    Cbuf_AddText("togglemenu\n");
    M_Menu_Main_f();
  }
}

//=============================================================================
/* MAIN MENU */

int m_main_cursor;

#ifdef VITA
#define MAIN_ITEMS 6
#else
#define MAIN_ITEMS 7 // + Loadout + Achievements
#endif

extern cvar_t cam_tour, cam_menu;
qboolean cam_menu_pending = false;

// Drives the security-camera background of the main menu. Called every frame.
void CamMenu_Update(void) {
  if (!cam_menu.value) {
    // feature disabled: don't get stuck on the boot-loaded cam map
    if (cam_tour.value)
      Cvar_SetValue("cam_tour", 0);
    if (cls.state == ca_connected && sv.active && strstr(sv.name, "ndu_cam") &&
        key_dest != key_menu)
      M_Menu_Main_f();
    return;
  }

  // idle on the MAIN menu with no game -> load the cam map behind it. Gated to
  // m_main so it can't fire during a real map launch (which happens in m_maps).
  if (cls.state == ca_disconnected && key_dest == key_menu &&
      m_state == m_main && !cam_menu_pending) {
    extern int cam_loading;
    cam_loading = 1; // keep the menu on screen, hide the reload's load screen
    Cbuf_AddText("cam_tour 1\nmap ndu_cam\n");
    cam_menu_pending = true;
    return;
  }

  if (cls.state != ca_connected || cls.signon != SIGNONS)
    return;

  cam_menu_pending = false;
  {
    extern int cam_loading;
    cam_loading = 0;
  }

  if (!(sv.active && strstr(sv.name, "ndu_cam"))) {
    if (cam_tour.value)
      Cvar_SetValue("cam_tour", 0); // a real map loaded -> drop the cam effect
    return;
  }

  // on the cam map (boot-loaded or reloaded) -> ensure the effect is on and
  // pop the menu over it
  if (!cam_tour.value)
    Cvar_SetValue("cam_tour", 1);
  if (key_dest != key_menu) {
    M_Menu_Main_f();
    Cbuf_AddText("music tensioned_by_the_damned\nmusic_loop 1\n");
  }
}

void M_Menu_Main_f(void) {
  if (key_dest != key_menu) {
    m_save_demonum = cls.demonum;
    cls.demonum = -1;
  }
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_main;
  m_entersound = true;

  // load saved achievement unlock/progress at startup (was VITA-only).
  void Load_Achivements(void);
  Load_Achivements();
}

void M_Main_Draw(void) {
  MENU_INITVARS();

  // Social Badges
  social_badges = Draw_CachePic("gfx/menu/social.tga");

  // Menu Background (Draw_BgMenu skips itself when the cam feed is live)
  menu_bk = Draw_CachePic("gfx/menu/menu_background.tga");
  Draw_BgMenu();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 1);

  // Version String
  DRAW_VERSIONSTRING();

  // Header
  DRAW_HEADER("MAIN MENU");

#ifdef VITA
  DRAW_MENUOPTION(0, "Solo", m_main_cursor, false);
  DRAW_BLANKOPTION("Co-Op", true);
  DRAW_MENUOPTION(1, "Settings", m_main_cursor, false);
  DRAW_MENUOPTION(2, "Achievements", m_main_cursor, true);
  DRAW_MENUOPTION(3, "Credits", m_main_cursor, true);
  DRAW_MENUOPTION(4, "Exit", m_main_cursor, false);
#else
  DRAW_MENUOPTION(0, "Solo", m_main_cursor, false);
  DRAW_MENUOPTION(1, "Co-Op", m_main_cursor, true);
  DRAW_MENUOPTION(2, "Loadout", m_main_cursor, false);
  DRAW_MENUOPTION(3, "Achievements", m_main_cursor, false);
  DRAW_MENUOPTION(4, "Settings", m_main_cursor, false);
  DRAW_MENUOPTION(5, "Gamemodes", m_main_cursor, false);
  DRAW_MENUOPTION(6, "Credits", m_main_cursor, true);
#endif // VITA

  switch (m_main_cursor) {
  case 0:
    DRAW_DESCRIPTION("Take on the Hordes by yourself.");
    break;
#ifdef VITA
  case 1:
    DRAW_DESCRIPTION("Adjust Controls and Graphics.");
    break;
  case 2:
    DRAW_DESCRIPTION("View Locked/Unlocked Achievements.");
    break;
  case 3:
    DRAW_DESCRIPTION("View Credits for NZ:P.");
    break;
  case 4:
    DRAW_DESCRIPTION("Return to LiveArea.");
    break;
#else
  case 1:
    DRAW_DESCRIPTION("Fight the undead with friends.");
    break;
  case 2:
    DRAW_DESCRIPTION("Equip up to 3 unlocked Perks.");
    break;
  case 3:
    DRAW_DESCRIPTION("View Locked/Unlocked Achievements.");
    break;
  case 4:
    DRAW_DESCRIPTION("Adjust Controls and Graphics.");
    break;
  case 5:
    DRAW_DESCRIPTION("Select a Game Mode.");
    break;
  case 6:
    DRAW_DESCRIPTION("View Credits for NZ:P.");
    break;
#endif // VITA
  default:
    break;
  }

#ifdef VITA

  Draw_SubPic(915, 510, 26, 26, 32, 0, 64, 32, social_badges); // YouTube
  Draw_ColoredStringScale(848, 510 + 6, "@nzpteam", 1, 1, 0, 1, 1.0f);

  Draw_SubPic(915, 510 - 26 - 5, 26, 26, 0, 32, 32, 64,
              social_badges); // Twitter
  Draw_ColoredStringScale(840, 510 - 25, "/NZPTeam", 1, 1, 0, 1, 1.0f);

  Draw_SubPic(915, 510 - 52 - 10, 26, 26, 32, 32, 64, 64,
              social_badges); // Patreon
  Draw_ColoredStringScale(818, 510 - 52 - 3, "/cypressimplex", 1, 1, 0, 1,
                          1.0f);

#else

  Draw_SubPic(610, y + 330, 22, 22, 32, 0, 64, 32, social_badges); // YouTube
  Draw_ColoredStringScale(548, y + 337, "@nzpteam", 1, 1, 0, 1, 1.0f);

  Draw_SubPic(610, y + 302, 22, 22, 0, 32, 32, 64, social_badges); // Twitter
  Draw_ColoredStringScale(542, y + 309, "/NZPTeam", 1, 1, 0, 1, 1.0f);

  Draw_SubPic(610, y + 274, 22, 22, 32, 32, 64, 64, social_badges); // Patreon
  Draw_ColoredStringScale(520, y + 280, "/cypressimplex", 1, 1, 0, 1, 1.0f);

#endif // VITA
}

void M_Main_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    IN_Activate();
    key_dest = key_game;
    m_state = m_none;
    cls.demonum = m_save_demonum;
    if (!fitzmode) /* QuakeSpasm customization: */
      break;
    if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected)
      CL_NextDemo();
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (++m_main_cursor >= MAIN_ITEMS)
      m_main_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (--m_main_cursor < 0)
      m_main_cursor = MAIN_ITEMS - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;

    switch (m_main_cursor) {
#ifdef VITA
    case 0:
      M_Menu_SinglePlayer_f();
      break;
    case 1:
      M_Menu_MultiPlayer_f();
      break;
    case 2:
      M_Menu_Options_f();
      break;
    case 3:
      M_Menu_Achievement_f();
      break;
    case 4:
      M_Menu_Credits_f();
      break;
    case 5:
      M_Menu_Quit_f();
      break;
#else
    // Switch layout: Solo, Co-Op, Loadout, Achievements, Settings, Gamemodes,
    // Credits
    case 0:
      M_Menu_SinglePlayer_f();
      break;
    case 1:
      M_Menu_MultiPlayer_f();
      break;
    case 2:
      M_Menu_Loadout_f();
      break;
    case 3:
      M_Menu_Achievement_f();
      break;
    case 4:
      M_Menu_Options_f();
      break;
    case 5:
      M_Menu_Gamemodes_f();
      break;
    case 6:
      M_Menu_Credits_f();
      break;
#endif
    }
  }
}

qboolean wasInMenus;

#ifdef VITA

char *restartMessage[] = {

    " Are you sure you want",
    "  to restart this game? ", // msg:0
    "                               ", "   X :Yes    O : No       "};

#else

char *restartMessage[] = {

    " Are you sure you want",
    "  to restart this game? ", // msg:0
    "                               ", "   A :Yes    B : No       "};

#endif // VITA

void M_Menu_Restart_f(void) {
  wasInMenus = (key_dest == key_menu_pause);
  key_dest = key_menu;
  m_state = m_restart;
  m_entersound = true;
}

extern int textstate;
void M_Restart_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    m_state = m_paused_menu;
    m_entersound = true;
    break;

  case K_ENTER:
  case K_ABUTTON:
    key_dest = key_game;
    m_state = m_none;
    paused_hack = false;
    // Cbuf_AddText ("restart\n");
    textstate = 0;
    // only the host (active local server) may restart; on a joined client
    // sv.active is false and Soft_Restart corrupts their view. The host's
    // restart respawns everyone.
    if (sv.active)
      PR_ExecuteProgramNamed(pr_global_struct->Soft_Restart, "Soft_Restart");
    break;
  default:
    break;
  }
}

void M_Restart_Draw(void) {
  m_state = m_paused_menu;
  m_recursiveDraw = true;
  M_Draw();
  m_state = m_restart;

  GL_SetCanvas(CANVAS_MENU);

  M_DrawTextBox(56, 76, 24, 4);
  M_Print(64, 84, restartMessage[0]);
  M_Print(64, 92, restartMessage[1]);
  M_Print(64, 100, restartMessage[2]);
  M_Print(64, 108, restartMessage[3]);
}

//=============================================================================
/* EXIT MENU */

char *exitMessage[] = {

    "Are you sure you want to",
    " quit to the Main Menu? ", // msg:0
    "                                  ", "   A :Yes    B : No       "};

void M_Menu_Exit_f(void) {
  wasInMenus = (key_dest == key_menu_pause);
  key_dest = key_menu;
  paused_hack = false;
  m_state = m_exit;
  m_entersound = true;
}

void M_Exit_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    m_state = m_paused_menu;
    m_entersound = true;
    break;

  case K_ENTER:
  case K_ABUTTON:
    Cbuf_AddText("disconnect\n");
    paused_hack = false;
    Cbuf_AddText("music tensioned_by_the_damned\n");
    Cbuf_AddText("music_loop 1\n");
    M_Menu_Main_f();
    break;

  default:
    break;
  }
}

void M_Exit_Draw(void) {
  m_state = m_paused_menu;
  m_recursiveDraw = true;
  M_Draw();
  m_state = m_exit;

  GL_SetCanvas(CANVAS_MENU);

  M_DrawTextBox(56, 76, 24, 4);
  M_Print(64, 84, exitMessage[0]);
  M_Print(64, 92, exitMessage[1]);
  M_Print(64, 100, exitMessage[2]);
  M_Print(64, 108, exitMessage[3]);
}

//=============================================================================
/* SINGLE PLAYER MENU */
#ifdef VITA
int x_map_info_disp = 200;
#else
int x_map_info_disp = 0;
#endif

int m_singleplayer_cursor;
#define SINGLEPLAYER_ITEMS 8

void M_Menu_SinglePlayer_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_singleplayer;
  m_entersound = true;
}

void M_SinglePlayer_Draw(void) {
  qpic_t *menu_ndu = Draw_CachePic("gfx/menu/nacht_der_untoten.tga");
  qpic_t *menu_kn = Draw_CachePic("gfx/menu/kino_der_toten.tga");
  qpic_t *menu_dr = Draw_CachePic("gfx/menu/custom/derriese.png");
  qpic_t *menu_wh = Draw_CachePic("gfx/menu/nzp_warehouse.tga");
  qpic_t *menu_wh2 = Draw_CachePic("gfx/menu/nzp_warehouse2.tga");
  // qpic_t* menu_wn 	= Draw_CachePic("gfx/menu/wahnsinn.tga");
  qpic_t *menu_ch = Draw_CachePic("gfx/menu/christmas_special.tga");
  qpic_t *menu_custom = Draw_CachePic("gfx/menu/custom.tga");

  MENU_INITVARS();
  paused_hack = false;

  // Menu Background
  Draw_BgMenu();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Header
  DRAW_HEADER("SOLO");

  DRAW_MENUOPTION(0, "Nacht der Untoten", m_singleplayer_cursor, false);
  DRAW_MENUOPTION(1, "Kino der Toten", m_singleplayer_cursor, false);
  DRAW_MENUOPTION(2, "Der Riese", m_singleplayer_cursor, true);
  DRAW_MENUOPTION(3, "Warehouse", m_singleplayer_cursor, false);
  DRAW_MENUOPTION(4, "Warehouse (Classic)", m_singleplayer_cursor, false);
  DRAW_MENUOPTION(5, "Christmas Special", m_singleplayer_cursor, true);
  DRAW_MENUOPTION(6, "Custom Maps", m_singleplayer_cursor, false);
  DRAW_BACKBUTTON(7, m_singleplayer_cursor);

  // Map description & pic
  switch (m_singleplayer_cursor) {
  case 0:
    DRAW_MAPTHUMB(menu_ndu);
    DRAW_MAPDESC(0, "Desolate bunker located on a Ge-");
    DRAW_MAPDESC(1, "rman airfield, stranded after a");
    DRAW_MAPDESC(2, "brutal plane crash surrounded by");
    DRAW_MAPDESC(3, "hordes of undead. Exploit myste-");
    DRAW_MAPDESC(4, "rious forces at play and hold o-");
    DRAW_MAPDESC(5, "ut against relentless waves. Der");
    DRAW_MAPDESC(6, "Anstieg ist jetzt. Will you fall");
    DRAW_MAPDESC(7, "to the overwhelming onslaught?");
    break;
  case 1:
    DRAW_MAPTHUMB(menu_kn);
    DRAW_MAPDESC(0, "A WW2-era theatre left to rot in");
    DRAW_MAPDESC(1, "West Berlin. See what secrets");
    DRAW_MAPDESC(2, "await in this Theatre of the");
    DRAW_MAPDESC(3, "Damned. The long-requested map,");
    DRAW_MAPDESC(4, "brought in from old archives and");
    DRAW_MAPDESC(5, "moderately restored.");
    break;
  case 2:
    DRAW_MAPTHUMB(menu_dr);
    DRAW_MAPDESC(0, "A remake of the WaW classic. All");
    DRAW_MAPDESC(1, "the perks, Pack-a-Punch, telepor-");
    DRAW_MAPDESC(2, "ters, hellhounds and the full");
    DRAW_MAPDESC(3, "arsenal await. Find the teddies");
    DRAW_MAPDESC(4, "and survive the Giant.");
    break;
  case 3:
    DRAW_MAPTHUMB(menu_wh2);
    DRAW_MAPDESC(0, "Four nameless marines find them-");
    DRAW_MAPDESC(1, "selves at a forsaken warehouse,");
    DRAW_MAPDESC(2, "or is it something more? Fight");
    DRAW_MAPDESC(3, "your way to uncovering its sec-");
    DRAW_MAPDESC(4, "rets, though you may not like");
    DRAW_MAPDESC(5, "what you find..");
    break;
  case 4:
    DRAW_MAPTHUMB(menu_wh);
    DRAW_MAPDESC(0, "Old Warehouse full of Zombies!");
    DRAW_MAPDESC(1, "Fight your way to the Power");
    DRAW_MAPDESC(2, "Switch through the Hordes!");
    break;
  case 5:
    DRAW_MAPTHUMB(menu_ch);
    DRAW_MAPDESC(0, "No Santa this year. Though we're");
    DRAW_MAPDESC(1, "sure you will get presents from");
    DRAW_MAPDESC(2, "the undead! Will you accept them?");
    break;
  case 6:
    DRAW_MAPTHUMB(menu_custom);
    DRAW_MAPDESC(0, "Custom Maps made by Community");
    DRAW_MAPDESC(1, "Members on GitHub and the NZ:P");
    DRAW_MAPDESC(2, "Forum!");
    break;
  default:
    break;
  }
}

void M_SinglePlayer_Key(int key) {
#ifdef VITA // For some reasons, clicking on "Solo" on Vita causes double inputs
            // propagation
  static int fix_double_input = 0;
#endif
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (++m_singleplayer_cursor >= SINGLEPLAYER_ITEMS)
      m_singleplayer_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (--m_singleplayer_cursor < 0)
      m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;

    switch (m_singleplayer_cursor) {
    case 0:
      // Nacht Der Untoten
#ifdef VITA
      if (!fix_double_input) {
        fix_double_input++;
        break;
      }
#endif
      IN_Activate();
      key_dest = key_game;
      if (sv.active)
        Cbuf_AddText("disconnect\n");

      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("music_loop 0\n");
      Cbuf_AddText("music_stop\n");
      Cbuf_AddText("map ndu\n");
      loadingScreen = 1;
      loadname2 = "ndu";
      loadnamespec = "Nacht der Untoten";
      break;

    case 1:
      // Kino der Toten
      IN_Activate();
      key_dest = key_game;
      if (sv.active)
        Cbuf_AddText("disconnect\n");

      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("map nzp_kinodertoten\n");
      Cbuf_AddText("music_loop 0\n");
      Cbuf_AddText("music_stop\n");
      loadingScreen = 1;
      loadname2 = "nzp_kinodertoten";
      loadnamespec = "Kino der Toten";
      break;

    case 2:
      // Der Riese
      IN_Activate();
      key_dest = key_game;
      if (sv.active)
        Cbuf_AddText("disconnect\n");

      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("map derriese\n");
      Cbuf_AddText("music_loop 0\n");
      Cbuf_AddText("music_stop\n");
      loadingScreen = 1;
      loadname2 = "derriese";
      loadnamespec = "Der Riese";
      break;

    case 3:
      // Warehouse
      IN_Activate();
      key_dest = key_game;
      if (sv.active)
        Cbuf_AddText("disconnect\n");

      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("map nzp_warehouse2\n");
      Cbuf_AddText("music_loop 0\n");
      Cbuf_AddText("music_stop\n");
      loadingScreen = 1;
      loadname2 = "nzp_warehouse2";
      loadnamespec = "Warehouse";
      break;

    case 4:
      // Warehouse (Classic)
      IN_Activate();
      key_dest = key_game;
      if (sv.active)
        Cbuf_AddText("disconnect\n");

      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("map nzp_warehouse\n");
      Cbuf_AddText("music_loop 0\n");
      Cbuf_AddText("music_stop\n");
      loadingScreen = 1;
      loadname2 = "nzp_warehouse";
      loadnamespec = "Warehouse (Classic)";
      break;

    case 5:
      // Christmas Special
      IN_Activate();
      key_dest = key_game;
      if (sv.active)
        Cbuf_AddText("disconnect\n");

      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("map christmas_special\n");
      Cbuf_AddText("music_loop 0\n");
      Cbuf_AddText("music_stop\n");
      loadingScreen = 1;
      loadname2 = "christmas_special";
      loadnamespec = "Christmas Special";
      break;

    case 6:
      // Custom Maps
      M_Menu_Maps_f();
      break;

    case 7:
      // Back
      M_Menu_Main_f();
      break;
    }
  }
}

//-------------------------------------------------------
//----------------------ACHIEVMENTS----------------------
//-------------------------------------------------------

//=============================================================================
/* ACHIEVEMENT MENU */
// was #ifdef VITA -- enabled on Switch so the achievements menu + saving work.

int m_achievement_cursor;
int m_achievement_selected;
int m_achievement_scroll[2];
int total_unlocked_achievements;
int total_locked_achievements;

achievement_list_t achievement_list[MAX_ACHIEVEMENTS];
qpic_t *achievement_locked;

void Achievement_Init(void) {
// all 14 ported gameplay achievements (ids match the QC GiveAchievement
// calls) + "Never Too Low" (14) which unlocks the Scavenger loadout perk.
#define ACH(idx, image, nm, desc)                                              \
  achievement_list[idx].img = Draw_CachePic("gfx/achievement/" image ".tga");  \
  achievement_list[idx].unlocked = 0;                                          \
  achievement_list[idx].progress = 0;                                          \
  strcpy(achievement_list[idx].name, nm);                                      \
  strcpy(achievement_list[idx].description, desc);

  ACH(0, "ready", "Ready..", "Reach round 5")
  ACH(1, "steady", "Steady..", "Reach round 10")
  ACH(2, "go_hell_no", "Go? Hell No...", "Reach round 15")
  ACH(3, "where_legs_go", "Where Did Legs Go?", "Blow a zombie into a crawler")
  ACH(4, "the_f_bomb", "The F Bomb", "Nuke a single lonely zombie")
  ACH(5, "no_perks_no_problem", "No Perks, No Problem",
      "Reach round 16 without buying a Perk")
  ACH(6, "dipsomaniac", "Dipsomaniac", "Own all 8 Perk-a-Colas at once")
  ACH(7, "thanks_explosions", "Thanks, Explosions!",
      "Survive a lethal blast via PhD Flopper")
  ACH(8, "abstinence_program", "Abstinence Program",
      "Reach a high round without buying a Perk")
  ACH(9, "pro_gamer_move", "Pro Gamer Move", "Go down on round 1 with no ammo")
  ACH(10, "spinning_plates", "Spinning Plates",
      "Reach round 10 keeping every barricade up")
  ACH(11, "mbox_maniac", "Mystery Box Maniac", "Move the Mystery Box 10 times")
  ACH(12, "the_collector", "The Collector", "Buy every wall weapon on the map")
  ACH(13, "warmed_up", "Warmed Up", "Rebuild 15 barricade boards")
  ACH(14, "cache_and_carry", "Never Too Low",
      "Reload your weapon 100 times -> unlocks Scavenger")
  ACH(15, "blow_the_bank", "Bartering King",
      "Buy from the Mystery Box 10 times -> unlocks Bartering King")
  ACH(16, "tough_luck", "Well Aren't You Lucky?",
      "Get a mystery weapon from a Wall-Buy in Random")
  ACH(17, "slasher", "Don't Bring a Knife",
      "...to a Random Party. Kill 100 zombies in Random without even using a "
      "gun")
  ACH(18, "over_achiever", "Hold the Point",
      "Win 10 Hardpoint rounds with kills only inside the zone")
  ACH(19, "oops", "Too Close!",
      "Take damage from your own rocket inside the Hardpoint")
  ACH(20, "increase_firepower", "Cranked Up", "Reach Cranked level 5")
  ACH(21, "cheating_death", "Cheating Death",
      "Escape a Meltdown by getting a kill in Cranked")
  ACH(22, "instant_help", "Back For Blood",
      "Give 50 zombie thralls their skulls back in Skull Ball")
  ACH(23, "divide_and_conquer", "Traitor",
      "Your reanimated thrall turns on its own kind for the first time")
  ACH(24, "locked_in", "Locked In",
      "Get 50 headshot kills -> unlocks Deadeye")
  ACH(25, "packed_or_nothing", "Packed Or Nothing",
      "Get 1000 kills with Pack-a-Punched weapons")
  ACH(26, "greased_up", "Greased Up",
      "Get 50 turret kills on Kino -> unlocks Technician")
  ACH(27, "callused_skin", "Callused Skin",
      "Survive 50 zombie hits (heal back to 100%) -> unlocks Medical")
  ACH(28, "no_strings_attached", "No Strings Attached",
      "Get 100 thrall kills in Skull Ball -> unlocks Puppeteer")

#undef ACH

  achievement_locked = Draw_CachePic("gfx/achievement/achievement_locked.tga");

  m_achievement_scroll[0] = 0;
  m_achievement_scroll[1] = 0;

  // overlay saved unlock/progress now that the list is defined, so the
  // outcome is correct regardless of HUD_Init vs M_Init ordering.
  void Load_Achivements(void);
  Load_Achivements();
}

// persistence rewritten from the Vita sceIo* API (absent on Switch, so nothing
// saved) to standard stdio. One "unlocked progress" line per achievement.
void Save_Achivements(void);

void Load_Achivements(void) {
  FILE *f = fopen(va("%s/ach.dat", com_gamedir), "r");
  if (f) {
    for (int i = 0; i < MAX_ACHIEVEMENTS; i++) {
      if (fscanf(f, "%d %d", &achievement_list[i].unlocked,
                 &achievement_list[i].progress) != 2) {
        achievement_list[i].unlocked = 0;
        achievement_list[i].progress = 0;
      }
    }
    fclose(f);

    // reconcile progression counters with earned achievements: if one is
    // unlocked, force its counter past the threshold so the QC progress
    // centerprints don't re-fire.
    {
      extern void SaveProgression(void);
      qboolean fixed = false;
      if (achievement_list[14].unlocked &&
          Cvar_VariableValue("nzp_reloads") < 100) {
        Cvar_SetValue("nzp_reloads", 100);
        fixed = true;
      } // Never Too Low -> Scavenger
      if (achievement_list[15].unlocked &&
          Cvar_VariableValue("nzp_box_buys") < 10) {
        Cvar_SetValue("nzp_box_buys", 10);
        fixed = true;
      } // Bartering King
      if (achievement_list[24].unlocked &&
          Cvar_VariableValue("nzp_headshot_kills") < 50) {
        Cvar_SetValue("nzp_headshot_kills", 50);
        fixed = true;
      } // Locked In -> Deadeye
      if (achievement_list[17].unlocked &&
          Cvar_VariableValue("nzp_knife_kills") < 100) {
        Cvar_SetValue("nzp_knife_kills", 100);
        fixed = true;
      } // Don't Bring a Knife
      if (achievement_list[18].unlocked &&
          Cvar_VariableValue("nzp_hp_clean_rounds") < 10) {
        Cvar_SetValue("nzp_hp_clean_rounds", 10);
        fixed = true;
      } // Hold the Point
      if (achievement_list[26].unlocked &&
          Cvar_VariableValue("nzp_turret_kills") < 50) {
        Cvar_SetValue("nzp_turret_kills", 50);
        fixed = true;
      } // Greased Up -> Technician
      if (achievement_list[27].unlocked &&
          Cvar_VariableValue("nzp_survived_hits") < 50) {
        Cvar_SetValue("nzp_survived_hits", 50);
        fixed = true;
      } // Callused Skin -> Medical
      if (achievement_list[28].unlocked &&
          Cvar_VariableValue("nzp_thrall_kills") < 100) {
        Cvar_SetValue("nzp_thrall_kills", 100);
        fixed = true;
      } // No Strings Attached -> Puppeteer
      if (fixed)
        SaveProgression();
    }
  } else {
    // first run -- create the file with everything locked
    Save_Achivements();
  }
}

void Save_Achivements(void) {
  FILE *f = fopen(va("%s/ach.dat", com_gamedir), "w");
  if (!f)
    return;
  for (int i = 0; i < MAX_ACHIEVEMENTS; i++)
    fprintf(f, "%d %d\n", achievement_list[i].unlocked,
            achievement_list[i].progress);
  fclose(f);
}

int ach_confirm_reset = 0; // showing the "reset all?" warning dialog

// wipe all achievement unlocks + progression (reloads / Scavenger level &
// mags / equipped loadout) and persist. Used by the Reset (X) prompt.
void Achievements_ResetAll(void) {
  for (int i = 0; i < MAX_ACHIEVEMENTS; i++) {
    achievement_list[i].unlocked = 0;
    achievement_list[i].progress = 0;
  }
  Save_Achivements();
  Cvar_SetValue("nzp_reloads", 0);
  Cvar_SetValue("nzp_scav_level", 1);
  Cvar_SetValue("nzp_scav_mags", 0);
  Cvar_SetValue("nzp_loadout1", 0);
  Cvar_SetValue("nzp_loadout2", 0);
  Cvar_SetValue("nzp_loadout3", 0);
  Cvar_SetValue("nzp_headshot_kills", 0);
  Cvar_SetValue("nzp_deadeye_level", 1);
  Cvar_SetValue("nzp_pap_kills", 0);
  Cvar_SetValue("nzp_turret_kills", 0);
  Cvar_SetValue("nzp_survived_hits", 0);
  Cvar_SetValue("nzp_thrall_kills", 0);
}

void M_Menu_Achievement_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_achievement;
  m_entersound = true;
  ach_confirm_reset = 0;
  Load_Achivements();
}

// Move the cursor to the next/prev DEFINED achievement (skips empty slots).
static void M_Achievement_Step(int dir) {
  int guard = 0;
  do {
    m_achievement_cursor += dir;
    if (m_achievement_cursor < 0)
      m_achievement_cursor = MAX_ACHIEVEMENTS - 1;
    if (m_achievement_cursor >= MAX_ACHIEVEMENTS)
      m_achievement_cursor = 0;
  } while (!achievement_list[m_achievement_cursor].name[0] &&
           ++guard < MAX_ACHIEVEMENTS);
}

#define ACH_PER_PAGE 8

// word-wrap a description to maxchars per line and draw each line, keeping long
// achievement text inside the right-hand column instead of off-screen.
static void M_DrawWrappedDescription(int x, int ytop, int linespace,
                                     const char *txt, float scale,
                                     int maxchars) {
  char line[128];
  int ll = 0, lineno = 0, i = 0;

  if (!txt)
    return;

  while (1) {
    while (txt[i] == ' ')
      i++; // skip spaces
    if (!txt[i])
      break;

    int ws = i;
    while (txt[i] && txt[i] != ' ')
      i++; // span one word
    int wlen = i - ws;

    // Flush the current line if this word won't fit.
    if (ll > 0 && ll + 1 + wlen > maxchars) {
      line[ll] = 0;
      Draw_ColoredStringScale(x, ytop + linespace * lineno, line, 1, 1, 1, 1,
                              scale);
      lineno++;
      ll = 0;
    }

    if (ll > 0 && ll < (int)sizeof(line) - 1)
      line[ll++] = ' ';
    for (int k = 0; k < wlen && ll < (int)sizeof(line) - 1; k++)
      line[ll++] = txt[ws + k];
  }

  if (ll > 0) {
    line[ll] = 0;
    Draw_ColoredStringScale(x, ytop + linespace * lineno, line, 1, 1, 1, 1,
                            scale);
  }
}

void M_Achievement_Draw(void) {
  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.55);

  int total = 0, unlocked = 0;
  for (int i = 0; i < MAX_ACHIEVEMENTS; i++)
    if (achievement_list[i].name[0]) {
      total++;
      if (achievement_list[i].unlocked)
        unlocked++;
    }

  int npages = (total + ACH_PER_PAGE - 1) / ACH_PER_PAGE;
  if (npages < 1)
    npages = 1;
  int page = m_achievement_cursor / ACH_PER_PAGE;

  DRAW_HEADER("ACHIEVEMENTS");
  Draw_ColoredStringScale(300, y + 18, va("%i / %i", unlocked, total), 1, 1, 0,
                          1, 2.0f);
  Draw_ColoredStringScale(300, y + 40,
                          va("Page %i/%i  -  L/R", page + 1, npages), 0.8, 0.8,
                          0.8, 1, 1.25f);
  Draw_ColoredStringScale(300, y + 56, "X: Reset Progress", 0.8, 0.8, 0.8, 1,
                          1.25f);

  // Show one page of achievements (all ids are contiguous so page*N is the
  // index).
  int first = page * ACH_PER_PAGE;
  for (int i = first; i < first + ACH_PER_PAGE && i < MAX_ACHIEVEMENTS; i++) {
    if (!achievement_list[i].name[0])
      continue;

    // List shows NAMES ONLY -- the DONE/LOCKED/progress status moved to the
    // right-hand panel so long names can't collide with the artwork card.
    DRAW_MENUOPTION(i, achievement_list[i].name, m_achievement_cursor, false);
  }

  // Selected achievement: a framed artwork card + status + description,
  // centered in the right-hand column (now clear of the short name list).
  if (m_achievement_cursor >= 0 && m_achievement_cursor < MAX_ACHIEVEMENTS &&
      achievement_list[m_achievement_cursor].name[0]) {
    int ci = m_achievement_cursor;
    int ax = 322, ay = y + 78, aw = 220, ah = 124;

    // Border frame (black backdrop + red edges) to match the menu's theme,
    // so the icon reads as a framed card instead of a bare png.
    Draw_FillByColor(ax - 5, ay - 5, aw + 10, ah + 10, 0, 0, 0, 235);
    Draw_FillByColor(ax - 5, ay - 5, aw + 10, 3, 150, 0, 0, 255);
    Draw_FillByColor(ax - 5, ay + ah + 2, aw + 10, 3, 150, 0, 0, 255);
    Draw_FillByColor(ax - 5, ay - 5, 3, ah + 10, 150, 0, 0, 255);
    Draw_FillByColor(ax + aw + 2, ay - 5, 3, ah + 10, 150, 0, 0, 255);
    Draw_StretchPic(ax, ay, achievement_list[ci].img, aw, ah);

    // Status line under the card.
    char *st;
    float sr = 1, sg = 1, sb = 1;
    if (achievement_list[ci].unlocked) {
      st = "UNLOCKED";
      sr = 0.4f;
      sg = 1.0f;
      sb = 0.4f;
    } else if (ci == 14) {
      st = va("%i / 100", (int)Cvar_VariableValue("nzp_reloads"));
      sr = 1;
      sg = 0.85f;
      sb = 0.3f;
    } else if (ci == 15) {
      st = va("%i / 10", (int)Cvar_VariableValue("nzp_box_buys"));
      sr = 1;
      sg = 0.85f;
      sb = 0.3f;
    } else if (ci == 17) {
      st = va("%i / 100", (int)Cvar_VariableValue("nzp_knife_kills"));
      sr = 1;
      sg = 0.85f;
      sb = 0.3f;
    } else if (ci == 18) {
      st = va("%i / 10", (int)Cvar_VariableValue("nzp_hp_clean_rounds"));
      sr = 1;
      sg = 0.85f;
      sb = 0.3f;
    } else {
      st = "LOCKED";
      sr = 1.0f;
      sg = 0.4f;
      sb = 0.4f;
    }
    Draw_ColoredStringScale(ax, ay + ah + 8, st, sr, sg, sb, 1, 1.5f);

    // Live progress bar for the counter-based achievements.
    float frac = -1.0f;
    if (achievement_list[ci].unlocked &&
        (ci == 14 || ci == 15 || ci == 17 || ci == 18))
      frac = 1.0f;
    else if (ci == 14)
      frac = Cvar_VariableValue("nzp_reloads") / 100.0f;
    else if (ci == 15)
      frac = Cvar_VariableValue("nzp_box_buys") / 10.0f;
    else if (ci == 17)
      frac = Cvar_VariableValue("nzp_knife_kills") / 100.0f;
    else if (ci == 18)
      frac = Cvar_VariableValue("nzp_hp_clean_rounds") / 10.0f;
    if (frac >= 0.0f) {
      if (frac > 1.0f)
        frac = 1.0f;
      Draw_FillByColor(ax, ay + ah + 28, aw, 10, 25, 25, 25, 220);
      Draw_FillByColor(ax, ay + ah + 28, (int)(aw * frac), 10, 80, 200, 80,
                       255);
    }

    // Word-wrapped description below.
    M_DrawWrappedDescription(ax, ay + ah + 46, 15,
                             achievement_list[ci].description, 1.3f, 26);
  }

  // Reset confirmation dialog overlay.
  if (ach_confirm_reset) {
    Draw_FillByColor(90, y + 95, 460, 96, 0, 0, 0, 235);
    Draw_FillByColor(90, y + 95, 460, 3, 150, 0, 0, 255);
    Draw_FillByColor(90, y + 188, 460, 3, 150, 0, 0, 255);
    Draw_ColoredStringScale(108, y + 105, "Reset ALL achievements & progress?",
                            1, 0.3, 0.3, 1, 1.5f);
    Draw_ColoredStringScale(108, y + 132, "This cannot be undone.", 1, 1, 1, 1,
                            1.25f);
    Draw_ColoredStringScale(108, y + 160,
                            "Press X again to RESET   -   B to cancel", 1, 1, 0,
                            1, 1.25f);
  }
}

void M_Achievement_Key(int key) {
  // While the reset warning is up: re-press X to confirm (unambiguous -- the
  // Switch swaps A/B so an "A: Yes" prompt mis-fires), anything else cancels.
  if (ach_confirm_reset) {
    if (key == K_XBUTTON) {
      Achievements_ResetAll();
      S_LocalSound("sounds/menu/navigate.wav");
    }
    ach_confirm_reset = 0;
    return;
  }

  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    M_Achievement_Step(-1);
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    M_Achievement_Step(1);
    break;

  case K_LEFTARROW:
  case K_LSHOULDER: // previous page
    S_LocalSound("sounds/menu/navigate.wav");
    m_achievement_cursor -= ACH_PER_PAGE;
    if (m_achievement_cursor < 0)
      m_achievement_cursor = 0;
    break;

  case K_RIGHTARROW:
  case K_RSHOULDER: // next page
    S_LocalSound("sounds/menu/navigate.wav");
    m_achievement_cursor += ACH_PER_PAGE;
    if (m_achievement_cursor >= MAX_ACHIEVEMENTS)
      m_achievement_cursor = MAX_ACHIEVEMENTS - 1;
    while (m_achievement_cursor > 0 &&
           !achievement_list[m_achievement_cursor].name[0])
      m_achievement_cursor--;
    break;

  case K_XBUTTON: // open the reset-everything confirmation
    ach_confirm_reset = 1;
    S_LocalSound("sounds/menu/navigate.wav");
    break;
  }
}

//=============================================================================
/* LOADOUT MENU -- NZP */
// equip up to 3 unlocked perks, stored in archived cvars nzp_loadout1/2/3 (read
// by the QC at match start). Perks unlock via achievements (Scavenger <- "Never
// Too Low").

extern cvar_t nzp_loadout1, nzp_loadout2, nzp_loadout3;
extern cvar_t nzp_skin_m1911; // M1911 skin: 0=None, 1=Gold

#define NUM_LOADOUT_SLOTS 3
#define LOADOUT_PERK_COUNT 7 // 0 = None, 1 = Scavenger, 2 = Bartering King, 3 = Deadeye, 4 = Puppeteer, 5 = Technician, 6 = Medical

#define LOADOUT_SKIN_ROW                                                       \
  NUM_LOADOUT_SLOTS // row index of the M1911 Skin selector
#define LOADOUT_PSEL_ROW                                                       \
  (NUM_LOADOUT_SLOTS + 1) // row index of "Perk Selection >"
#define LOADOUT_BACK_ROW (NUM_LOADOUT_SLOTS + 2) // row index of Back

void M_Menu_PerkSelection_f(void); // forward decl (defined below)
#define M1911_SKIN_COUNT 2 // None / Gold

int loadout_cursor;

const char *M1911_SkinName(int id) {
  switch (id) {
  case 1:
    return "Gold";
  default:
    return "None";
  }
}

const char *M1911_SkinDesc(int id) {
  switch (id) {
  case 1:
    return "Golden M1911 - solid gold finish.";
  default:
    return "Standard M1911 (default).";
  }
}

// Cycle the equipped M1911 skin (all unlocked for now).
void M1911_CycleSkin(int dir) {
  int cur = (int)nzp_skin_m1911.value;
  cur += dir;
  if (cur < 0)
    cur = M1911_SKIN_COUNT - 1;
  if (cur >= M1911_SKIN_COUNT)
    cur = 0;
  Cvar_SetValue("nzp_skin_m1911", cur);
  S_LocalSound("sounds/menu/navigate.wav");
}

const char *Loadout_PerkName(int id) {
  switch (id) {
  case 1:
    return "Scavenger";
  case 2:
    return "Bartering King";
  case 3:
    return "Deadeye";
  case 4:
    return "Puppeteer";
  case 5:
    return "Technician";
  case 6:
    return "Medical";
  default:
    return "None";
  }
}

// Name WITH its current level appended (for the menu display).
const char *Loadout_PerkDisplayName(int id) {
  int lvl;
  if (id == 1) { // Scavenger
    lvl = (int)Cvar_VariableValue("nzp_scav_level");
    if (lvl >= 4)
      return "Scavenger Pro";
    if (lvl == 3)
      return "Scavenger 3";
    if (lvl == 2)
      return "Scavenger 2";
    return "Scavenger";
  }
  if (id == 2) { // Bartering King
    lvl = (int)Cvar_VariableValue("nzp_barter_level");
    if (lvl >= 4)
      return "Bartering King Pro";
    if (lvl == 3)
      return "Bartering King 3";
    if (lvl == 2)
      return "Bartering King 2";
    return "Bartering King";
  }
  if (id == 3) { // Deadeye
    lvl = (int)Cvar_VariableValue("nzp_deadeye_level");
    if (lvl >= 4)
      return "Deadeye Pro";
    if (lvl == 3)
      return "Deadeye 3";
    if (lvl == 2)
      return "Deadeye 2";
    return "Deadeye";
  }
  return Loadout_PerkName(id);
}

const char *Loadout_PerkDesc(int id) {
  switch (id) {
  case 1:
    return "Zombies have a chance to drop a spare magazine.";
  case 2:
    return "Reduces the Mystery Box & wall-buy cost.";
  case 3:
    return "Headshot kills award bonus points.";
  case 4:
    return "Kills have a 1% chance to turn the zombie into a Thrall.";
  case 5:
    return "Press D-Pad Down to place a mini-turret (5m Cooldown).";
  case 6:
    return "Kills have a 5% chance to restore full health.";
  default:
    return "Empty slot. Left/Right to equip a Perk.";
  }
}

qboolean Loadout_PerkUnlocked(int id) {
  if (id == 0)
    return true; // None is always available
  if (id == 1)
    return achievement_list[14].unlocked; // Scavenger <- Never Too Low
  if (id == 2)
    return achievement_list[15].unlocked; // Bartering King <- 10 box buys
  if (id == 3)
    return achievement_list[24].unlocked; // Deadeye <- Locked In (50 headshots)
  if (id == 4)
    return achievement_list[28].unlocked; // Puppeteer <- No Strings Attached
  if (id == 5)
    return achievement_list[26].unlocked; // Technician <- Greased Up
  if (id == 6)
    return achievement_list[27].unlocked; // Medical <- Callused Skin
  return false;
}

cvar_t *Loadout_SlotCvar(int slot) {
  if (slot == 0)
    return &nzp_loadout1;
  if (slot == 1)
    return &nzp_loadout2;
  return &nzp_loadout3;
}

// Cycle a slot to the next unlocked, non-duplicate perk (None may repeat).
void Loadout_CycleSlot(int slot, int dir) {
  cvar_t *cv = Loadout_SlotCvar(slot);
  int cur = (int)cv->value;
  for (int tries = 0; tries < LOADOUT_PERK_COUNT; tries++) {
    cur += dir;
    if (cur < 0)
      cur = LOADOUT_PERK_COUNT - 1;
    if (cur >= LOADOUT_PERK_COUNT)
      cur = 0;
    if (!Loadout_PerkUnlocked(cur))
      continue;     // skip locked perks
    if (cur != 0) { // no duplicate non-None perks
      qboolean dup = false;
      for (int s = 0; s < NUM_LOADOUT_SLOTS; s++)
        if (s != slot && (int)Loadout_SlotCvar(s)->value == cur)
          dup = true;
      if (dup)
        continue;
    }
    Cvar_SetValue(cv->name, cur);
    S_LocalSound("sounds/menu/navigate.wav");
    return;
  }
}

void M_Menu_Loadout_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_loadout;
  m_entersound = true;
}

void M_Loadout_Draw(void) {
  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  DRAW_HEADER("LOADOUT");

  for (int i = 0; i < NUM_LOADOUT_SLOTS; i++) {
    int id = (int)Loadout_SlotCvar(i)->value;
    DRAW_MENUOPTION(i, va("Perk %i:   %s", i + 1, Loadout_PerkDisplayName(id)),
                    loadout_cursor, false);
  }
  DRAW_MENUOPTION(
      LOADOUT_SKIN_ROW,
      va("M1911 Skin:   %s", M1911_SkinName((int)nzp_skin_m1911.value)),
      loadout_cursor, false);
  DRAW_MENUOPTION(LOADOUT_PSEL_ROW, "Perk Selection >", loadout_cursor, false);
  DRAW_MENUOPTION(LOADOUT_BACK_ROW, "Back", loadout_cursor, false);

  if (loadout_cursor < NUM_LOADOUT_SLOTS) {
    DRAW_DESCRIPTION(
        Loadout_PerkDesc((int)Loadout_SlotCvar(loadout_cursor)->value));
  } else if (loadout_cursor == LOADOUT_SKIN_ROW) {
    DRAW_DESCRIPTION(M1911_SkinDesc((int)nzp_skin_m1911.value));
  } else if (loadout_cursor == LOADOUT_PSEL_ROW) {
    DRAW_DESCRIPTION("Pick which perks spawn on NDU / Kino / Der Riese.");
  } else {
    DRAW_DESCRIPTION("Return to the Main Menu.");
  }
}

void M_Loadout_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (--loadout_cursor < 0)
      loadout_cursor = LOADOUT_BACK_ROW;
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (++loadout_cursor > LOADOUT_BACK_ROW)
      loadout_cursor = 0;
    break;

  case K_LEFTARROW:
    if (loadout_cursor < NUM_LOADOUT_SLOTS)
      Loadout_CycleSlot(loadout_cursor, -1);
    else if (loadout_cursor == LOADOUT_SKIN_ROW)
      M1911_CycleSkin(-1);
    break;

  case K_RIGHTARROW:
    if (loadout_cursor < NUM_LOADOUT_SLOTS)
      Loadout_CycleSlot(loadout_cursor, 1);
    else if (loadout_cursor == LOADOUT_SKIN_ROW)
      M1911_CycleSkin(1);
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    if (loadout_cursor == LOADOUT_BACK_ROW)
      M_Menu_Main_f();
    else if (loadout_cursor == LOADOUT_PSEL_ROW)
      M_Menu_PerkSelection_f(); // open the per-map perk selection
    else if (loadout_cursor == LOADOUT_SKIN_ROW)
      M1911_CycleSkin(1); // A also cycles the skin
    else
      Loadout_CycleSlot(loadout_cursor, 1); // A also cycles the slot
    break;
  }
}

/* PERK SELECTION MENU -- NZP */
// per-map override of each machine's perk (closest to furthest), in archived
// cvars nzp_psel_<map><slot>. Choice ids: 0=Default, 1=None, 2..9 = a specific
// perk.

#define PSEL_CHOICE_COUNT 10

const char *PSel_PerkName(int id) {
  switch (id) {
  case 0:
    return "Default";
  case 1:
    return "None";
  case 2:
    return "Quick Revive";
  case 3:
    return "Juggernog";
  case 4:
    return "Speed Cola";
  case 5:
    return "Double Tap";
  case 6:
    return "PhD Flopper";
  case 7:
    return "Stamin-Up";
  case 8:
    return "Deadshot";
  case 9:
    return "Mule Kick";
  }
  return "?";
}

typedef struct {
  const char *title;
  int slotcount;
  int cvarbase;    // index into psel_cvar[] of this map's slot 0
  int defaults[8]; // default perk id per slot (closest -> furthest)
} pselmap_t;

// Slot order = closest-to-furthest by gameplay reachability (hand-verified, NOT
// raw distance -- e.g. NDU Jug is on the top floor so it's last).
static pselmap_t psel_maps[3] = {
    {"NDU", 4, 0, {2, 9, 4, 3, 0, 0, 0, 0}}, // Revive, Mule, Speed, Jug
    {"Kino",
     5,
     4,
     {2, 3, 9, 5, 4, 0, 0, 0}}, // Revive, Jug, Mule, Double, Speed
    {"Der Riese",
     8,
     9,
     {4, 5, 6, 9, 8, 3, 2,
      7}}, // Speed, Double, PhD, Mule, Deadshot, Jug, Revive, Stamin-Up
};

cvar_t psel_cvar[17] = {
    {"nzp_psel_ndu1", "0", CVAR_ARCHIVE},
    {"nzp_psel_ndu2", "0", CVAR_ARCHIVE},
    {"nzp_psel_ndu3", "0", CVAR_ARCHIVE},
    {"nzp_psel_ndu4", "0", CVAR_ARCHIVE},
    {"nzp_psel_kino1", "0", CVAR_ARCHIVE},
    {"nzp_psel_kino2", "0", CVAR_ARCHIVE},
    {"nzp_psel_kino3", "0", CVAR_ARCHIVE},
    {"nzp_psel_kino4", "0", CVAR_ARCHIVE},
    {"nzp_psel_kino5", "0", CVAR_ARCHIVE},
    {"nzp_psel_der1", "0", CVAR_ARCHIVE},
    {"nzp_psel_der2", "0", CVAR_ARCHIVE},
    {"nzp_psel_der3", "0", CVAR_ARCHIVE},
    {"nzp_psel_der4", "0", CVAR_ARCHIVE},
    {"nzp_psel_der5", "0", CVAR_ARCHIVE},
    {"nzp_psel_der6", "0", CVAR_ARCHIVE},
    {"nzp_psel_der7", "0", CVAR_ARCHIVE},
    {"nzp_psel_der8", "0", CVAR_ARCHIVE},
};

void PSel_RegisterCvars(void) {
  for (int i = 0; i < 17; i++)
    Cvar_RegisterVariable(&psel_cvar[i]);
}

int perksel_map;    // 0..2, which map we're configuring
int perksel_cursor; // 0 = map row, 1..slotcount = slots, slotcount+1 = Back

static int PSel_SlotVal(int mapidx, int slot) {
  return (int)psel_cvar[psel_maps[mapidx].cvarbase + slot].value;
}
static void PSel_SetSlot(int mapidx, int slot, int val) {
  Cvar_SetValue(psel_cvar[psel_maps[mapidx].cvarbase + slot].name, val);
}

static const char *PSel_Ordinal(int slot, int count) {
  if (slot == 0)
    return "1st (closest)";
  if (slot == count - 1)
    return va("%ith (furthest)", slot + 1);
  if (slot == 1)
    return "2nd";
  if (slot == 2)
    return "3rd";
  return va("%ith", slot + 1);
}

void M_Menu_PerkSelection_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_perksel;
  m_entersound = true;
  perksel_cursor = 0;
}

void M_PerkSelection_Draw(void) {
  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  DRAW_HEADER("PERK SELECTION");

  pselmap_t *mp = &psel_maps[perksel_map];
  int back_row = mp->slotcount + 1;

  DRAW_MENUOPTION(0, va("Map:   %s", mp->title), perksel_cursor, false);
  for (int s = 0; s < mp->slotcount; s++) {
    int v = PSel_SlotVal(perksel_map, s);
    const char *val = (v == 0)
                          ? va("Default (%s)", PSel_PerkName(mp->defaults[s]))
                          : PSel_PerkName(v);
    DRAW_MENUOPTION(s + 1, va("%s:   %s", PSel_Ordinal(s, mp->slotcount), val),
                    perksel_cursor, false);
  }
  DRAW_MENUOPTION(back_row, "Back", perksel_cursor, false);

  if (perksel_cursor == 0) {
    DRAW_DESCRIPTION("Choose which map to configure. Left/Right to change.");
  } else if (perksel_cursor == back_row) {
    DRAW_DESCRIPTION("Return to the Loadout menu.");
  } else {
    DRAW_DESCRIPTION(
        "Left/Right: Default (map perk) -> None (empty) -> a specific Perk.");
  }
}

void M_PerkSelection_Key(int key) {
  pselmap_t *mp = &psel_maps[perksel_map];
  int back_row = mp->slotcount + 1;

  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Loadout_f();
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (--perksel_cursor < 0)
      perksel_cursor = back_row;
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (++perksel_cursor > back_row)
      perksel_cursor = 0;
    break;

  case K_LEFTARROW:
    if (perksel_cursor == 0) {
      if (--perksel_map < 0)
        perksel_map = 2;
      S_LocalSound("sounds/menu/navigate.wav");
      if (perksel_cursor > psel_maps[perksel_map].slotcount + 1)
        perksel_cursor = psel_maps[perksel_map].slotcount + 1;
    } else if (perksel_cursor <= mp->slotcount) {
      int s = perksel_cursor - 1;
      int v = PSel_SlotVal(perksel_map, s) - 1;
      if (v < 0)
        v = PSEL_CHOICE_COUNT - 1;
      PSel_SetSlot(perksel_map, s, v);
      S_LocalSound("sounds/menu/navigate.wav");
    }
    break;

  case K_RIGHTARROW:
    if (perksel_cursor == 0) {
      if (++perksel_map > 2)
        perksel_map = 0;
      S_LocalSound("sounds/menu/navigate.wav");
      if (perksel_cursor > psel_maps[perksel_map].slotcount + 1)
        perksel_cursor = psel_maps[perksel_map].slotcount + 1;
    } else if (perksel_cursor <= mp->slotcount) {
      int s = perksel_cursor - 1;
      int v = PSel_SlotVal(perksel_map, s) + 1;
      if (v >= PSEL_CHOICE_COUNT)
        v = 0;
      PSel_SetSlot(perksel_map, s, v);
      S_LocalSound("sounds/menu/navigate.wav");
    }
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    if (perksel_cursor == back_row)
      M_Menu_Loadout_f();
    else if (perksel_cursor == 0) {
      if (++perksel_map > 2)
        perksel_map = 0;
      S_LocalSound("sounds/menu/navigate.wav");
      if (perksel_cursor > psel_maps[perksel_map].slotcount + 1)
        perksel_cursor = psel_maps[perksel_map].slotcount + 1;
    } else {
      int s = perksel_cursor - 1;
      int v = PSel_SlotVal(perksel_map, s) + 1;
      if (v >= PSEL_CHOICE_COUNT)
        v = 0;
      PSel_SetSlot(perksel_map, s, v);
    }
    break;
  }
}

int m_maps_cursor;
int MAP_ITEMS;
int user_maps_num = 0;
int current_custom_map_page;
int custom_map_pages;
int multiplier;
char user_levels[256][MAX_QPATH];

void M_Menu_Maps_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_maps;
  m_entersound = true;
  MAP_ITEMS = 13;
  current_custom_map_page = 1;
}
extern vrect_t scr_vrect;
void M_Menu_Maps_Draw(void) {
  qpic_t *menu_cuthum;

  MENU_INITVARS();

  // Menu Background
  Draw_BgMenu();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Header
  DRAW_HEADER("CUSTOM MAPS");

  int line_increment;
  line_increment = 0;

  if (current_custom_map_page > 1)
    multiplier = (current_custom_map_page - 1) * 15;
  else
    multiplier = 0;

  for (int i = 0; i < 15; i++) {
    if (custom_maps[i + multiplier].occupied == false)
      continue;

    if (custom_maps[i + multiplier].map_name_pretty != 0) {
      DRAW_MENUOPTION(i, custom_maps[i + multiplier].map_name_pretty,
                      m_maps_cursor, false);
    } else {
      DRAW_MENUOPTION(i, custom_maps[i + multiplier].map_name, m_maps_cursor,
                      false);
    }

    if (m_maps_cursor == i) {
      if (custom_maps[i + multiplier].map_use_thumbnail == 1) {
        menu_cuthum =
            Draw_CachePic(custom_maps[i + multiplier].map_thumbnail_path);

        if (menu_cuthum != NULL)
          DRAW_MAPTHUMB(menu_cuthum);
      }

      if (custom_maps[i + multiplier].map_desc_1 != 0) {
        if (strcmp(custom_maps[i + multiplier].map_desc_1, " ") != 0) {
          DRAW_MAPDESC(0, custom_maps[i + multiplier].map_desc_1);
        }
      }
      if (custom_maps[i + multiplier].map_desc_2 != 0) {
        if (strcmp(custom_maps[i + multiplier].map_desc_2, " ") != 0) {
          line_increment++;
          DRAW_MAPDESC(1, custom_maps[i + multiplier].map_desc_2);
        }
      }
      if (custom_maps[i + multiplier].map_desc_3 != 0) {
        if (strcmp(custom_maps[i + multiplier].map_desc_3, " ") != 0) {
          line_increment++;
          DRAW_MAPDESC(2, custom_maps[i + multiplier].map_desc_3);
        }
      }
      if (custom_maps[i + multiplier].map_desc_4 != 0) {
        if (strcmp(custom_maps[i + multiplier].map_desc_4, " ") != 0) {
          line_increment++;
          DRAW_MAPDESC(3, custom_maps[i + multiplier].map_desc_4);
        }
      }
      if (custom_maps[i + multiplier].map_desc_5 != 0) {
        if (strcmp(custom_maps[i + multiplier].map_desc_5, " ") != 0) {
          line_increment++;
          DRAW_MAPDESC(4, custom_maps[i + multiplier].map_desc_5);
        }
      }
      if (custom_maps[i + multiplier].map_desc_6 != 0) {
        if (strcmp(custom_maps[i + multiplier].map_desc_6, " ") != 0) {
          line_increment++;
          DRAW_MAPDESC(5, custom_maps[i + multiplier].map_desc_6);
        }
      }
      if (custom_maps[i + multiplier].map_desc_7 != 0) {
        if (strcmp(custom_maps[i + multiplier].map_desc_7, " ") != 0) {
          line_increment++;
          DRAW_MAPDESC(6, custom_maps[i + multiplier].map_desc_7);
        }
      }
      if (custom_maps[i + multiplier].map_desc_8 != 0) {
        if (strcmp(custom_maps[i + multiplier].map_desc_8, " ") != 0) {
          line_increment++;
          DRAW_MAPDESC(7, custom_maps[i + multiplier].map_desc_8);
        }
      }
      if (custom_maps[i + multiplier].map_author != 0) {
        if (strcmp(custom_maps[i + multiplier].map_author, " ") != 0) {
          line_increment++;
          DRAW_MAPAUTHOR(line_increment,
                         custom_maps[i + multiplier].map_author);
        }
      }
    }
  }

#ifdef VITA

  menu_offset_y = 441;

#else

  menu_offset_y = y + 285;

#endif // VITA

  if (current_custom_map_page != custom_map_pages) {
    DRAW_MENUOPTION(15, "Next Page", m_maps_cursor, false);
  } else {
    DRAW_BLANKOPTION("Next Page", false);
  }

  if (current_custom_map_page != 1) {
    DRAW_MENUOPTION(16, "Previous Page", m_maps_cursor, false);
  } else {
    DRAW_BLANKOPTION("Previous Page", false);
  }

  DRAW_BACKBUTTON(17, m_maps_cursor);
}

void M_Menu_Maps_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_SinglePlayer_f();
    break;
  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    m_maps_cursor++;

    if (m_maps_cursor < 14 &&
        custom_maps[m_maps_cursor + multiplier].occupied == false) {
      m_maps_cursor = 15;
    }

    if (m_maps_cursor == 15 && current_custom_map_page == custom_map_pages)
      m_maps_cursor = 16;

    if (m_maps_cursor == 16 && current_custom_map_page == 1)
      m_maps_cursor = 17;

    if (m_maps_cursor >= 18)
      m_maps_cursor = 0;
    break;
  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    m_maps_cursor--;

    if (m_maps_cursor < 0)
      m_maps_cursor = 17;

    if (m_maps_cursor == 16 && current_custom_map_page == 1)
      m_maps_cursor = 15;

    if (m_maps_cursor == 15 && current_custom_map_page == custom_map_pages)
      m_maps_cursor = 14;

    if (m_maps_cursor <= 14 &&
        custom_maps[m_maps_cursor + multiplier].occupied == false) {
      for (int i = 14; i > -1; i--) {
        if (custom_maps[i + multiplier].occupied == true) {
          m_maps_cursor = i;
          break;
        }
      }
    }
    break;
  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;

    if (m_maps_cursor == 17) {
      M_Menu_SinglePlayer_f();
    } else if (m_maps_cursor == 16) {
      current_custom_map_page--;
      m_maps_cursor = 0;
    } else if (m_maps_cursor == 15) {
      current_custom_map_page++;
      m_maps_cursor = 0;
    } else {
      if (sv.active)
        Cbuf_AddText("disconnect\n");

      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("music_loop 0\n");
      Cbuf_AddText("music_stop\n");

      char map_selection[MAX_QPATH];
      strcpy(map_selection, custom_maps[m_maps_cursor + multiplier].map_name);
      Cbuf_AddText(
          va("map %s\n", custom_maps[m_maps_cursor + multiplier].map_name));

      loadingScreen = 1;
      loadname2 = custom_maps[m_maps_cursor + multiplier].map_name;
      if (custom_maps[m_maps_cursor + multiplier].map_name_pretty != 0)
        loadnamespec = custom_maps[m_maps_cursor + multiplier].map_name_pretty;
      else
        loadnamespec = custom_maps[m_maps_cursor + multiplier].map_name;
    }
    break;
  }
}

//==================== Map Find System By Crow_bar =============================
// Modified by Naievil for Nintendo Switch release

extern char cwd[MAX_OSPATH];

void Map_Finder(void) {
  DIR *dir;
  struct dirent *ent;
  char map_dir[MAX_OSPATH];
  qboolean breakaway;

  for (int i = 0; i < 50; i++) {
    if (custom_maps[i].occupied) {
      if (custom_maps[i].map_name) {
        free(custom_maps[i].map_name);
        custom_maps[i].map_name = NULL;
      }
      if (custom_maps[i].map_thumbnail_path) {
        free(custom_maps[i].map_thumbnail_path);
        custom_maps[i].map_thumbnail_path = NULL;
      }
      if (custom_maps[i].map_name_pretty) {
        free(custom_maps[i].map_name_pretty);
        custom_maps[i].map_name_pretty = NULL;
      }
      if (custom_maps[i].map_desc_1) {
        free(custom_maps[i].map_desc_1);
        custom_maps[i].map_desc_1 = NULL;
      }
      if (custom_maps[i].map_desc_2) {
        free(custom_maps[i].map_desc_2);
        custom_maps[i].map_desc_2 = NULL;
      }
      if (custom_maps[i].map_desc_3) {
        free(custom_maps[i].map_desc_3);
        custom_maps[i].map_desc_3 = NULL;
      }
      if (custom_maps[i].map_desc_4) {
        free(custom_maps[i].map_desc_4);
        custom_maps[i].map_desc_4 = NULL;
      }
      if (custom_maps[i].map_desc_5) {
        free(custom_maps[i].map_desc_5);
        custom_maps[i].map_desc_5 = NULL;
      }
      if (custom_maps[i].map_desc_6) {
        free(custom_maps[i].map_desc_6);
        custom_maps[i].map_desc_6 = NULL;
      }
      if (custom_maps[i].map_desc_7) {
        free(custom_maps[i].map_desc_7);
        custom_maps[i].map_desc_7 = NULL;
      }
      if (custom_maps[i].map_desc_8) {
        free(custom_maps[i].map_desc_8);
        custom_maps[i].map_desc_8 = NULL;
      }
      if (custom_maps[i].map_author) {
        free(custom_maps[i].map_author);
        custom_maps[i].map_author = NULL;
      }
    }
    custom_maps[i].occupied = false;
  }
  user_maps_num = 0;

  sprintf(map_dir, "%s/nzp/maps", cwd);
  dir = opendir(map_dir);
  if (dir == NULL) {
    Host_Error("Map_Finder: Failed to open dir %s\n", map_dir);
  } else {
    while ((ent = readdir(dir))) {
      if (!strcmp(COM_FileGetExtension(ent->d_name), "bsp") ||
          !strcmp(COM_FileGetExtension(ent->d_name), "BSP")) {
        // Attempt to fix operating system files (macOS, bleh)
        // from appearing in maps menu.
        if (ent->d_name[0] == '.' || ent->d_name[0] == '_')
          continue;

        breakaway = false;
        char ntype[32];
        COM_StripExtension(ent->d_name, ntype, sizeof(ntype));

        for (int j = 0; j < BASE_MAP_COUNT; j++) {
          if (breakaway == true)
            break;

          if (!strcmp(ntype, base_maps[j])) {
            // Con_Printf("ntype: %s\n base_map: %s\n", ntype, base_maps[j]);
            breakaway = true;
          }
        }

        if (breakaway == true) {
          // Con_Printf("Breaking away at ntype: %s\n", ntype);
          continue;
        } else {
          // Con_Printf("Success at ntype: %s\n", ntype);
          custom_maps[user_maps_num].occupied = true;
          custom_maps[user_maps_num].map_name = malloc(sizeof(char) * 32);
          sprintf(custom_maps[user_maps_num].map_name, "%s", ntype);

          char setting_path[MAX_OSPATH];
          FILE *setting_file;

          custom_maps[user_maps_num].map_thumbnail_path = malloc(256);

          sprintf(setting_path, "%s/%s.txt", map_dir,
                  custom_maps[user_maps_num].map_name);
          // thumbnails ship as .tga or .png: resolve the real file (prefer
          // .tga, fall back to .png, then the map's lscreen) since
          // Draw_CachePic needs the exact path.
          sprintf(custom_maps[user_maps_num].map_thumbnail_path,
                  "gfx/menu/custom/%s.tga",
                  custom_maps[user_maps_num].map_name);
          if (!COM_FileExists(custom_maps[user_maps_num].map_thumbnail_path,
                              NULL))
            sprintf(custom_maps[user_maps_num].map_thumbnail_path,
                    "gfx/menu/custom/%s.png",
                    custom_maps[user_maps_num].map_name);
          if (!COM_FileExists(custom_maps[user_maps_num].map_thumbnail_path,
                              NULL))
            sprintf(custom_maps[user_maps_num].map_thumbnail_path,
                    "gfx/lscreen/%s.png", custom_maps[user_maps_num].map_name);

          custom_maps[user_maps_num].map_name_pretty = NULL;
          custom_maps[user_maps_num].map_desc_1 = NULL;
          custom_maps[user_maps_num].map_desc_2 = NULL;
          custom_maps[user_maps_num].map_desc_3 = NULL;
          custom_maps[user_maps_num].map_desc_4 = NULL;
          custom_maps[user_maps_num].map_desc_5 = NULL;
          custom_maps[user_maps_num].map_desc_6 = NULL;
          custom_maps[user_maps_num].map_desc_7 = NULL;
          custom_maps[user_maps_num].map_desc_8 = NULL;
          custom_maps[user_maps_num].map_author = NULL;

          setting_file = fopen(setting_path, "r");

          if (setting_file != NULL) {
            int state;
            state = 0;
            int value;
            char buffer[64];
            int bufferlen = sizeof(buffer);

            while (fgets(buffer, bufferlen, setting_file)) {
              // strip newlines
              buffer[strcspn(buffer, "\r")] = 0;
              buffer[strcspn(buffer, "\n")] = 0;

              switch (state) {
              case 0:
                custom_maps[user_maps_num].map_name_pretty =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_name_pretty, buffer);
                break;
              case 1:
                custom_maps[user_maps_num].map_desc_1 =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_desc_1, buffer);
                break;
              case 2:
                custom_maps[user_maps_num].map_desc_2 =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_desc_2, buffer);
                break;
              case 3:
                custom_maps[user_maps_num].map_desc_3 =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_desc_3, buffer);
                break;
              case 4:
                custom_maps[user_maps_num].map_desc_4 =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_desc_4, buffer);
                break;
              case 5:
                custom_maps[user_maps_num].map_desc_5 =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_desc_5, buffer);
                break;
              case 6:
                custom_maps[user_maps_num].map_desc_6 =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_desc_6, buffer);
                break;
              case 7:
                custom_maps[user_maps_num].map_desc_7 =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_desc_7, buffer);
                break;
              case 8:
                custom_maps[user_maps_num].map_desc_8 =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_desc_8, buffer);
                break;
              case 9:
                custom_maps[user_maps_num].map_author =
                    malloc(strlen(buffer) + 1);
                strcpy(custom_maps[user_maps_num].map_author, buffer);
                break;
              case 10:
                value = 0;
                sscanf(buffer, "%d", &value);
                custom_maps[user_maps_num].map_use_thumbnail = value;
                break;
              case 11:
                value = 0;
                sscanf(buffer, "%d", &value);
                custom_maps[user_maps_num].map_allow_game_settings = value;
                break;
              default:
                break;
              }
              state++;
            }
            fclose(setting_file);
          }

          // if a thumbnail/lscreen exists on disk, force it on regardless of
          // the .txt flag so every map with artwork shows it.
          if (COM_FileExists(custom_maps[user_maps_num].map_thumbnail_path,
                             NULL))
            custom_maps[user_maps_num].map_use_thumbnail = 1;
          else
            custom_maps[user_maps_num].map_use_thumbnail = 0;

          if (custom_maps[user_maps_num].map_name_pretty == NULL) {
            custom_maps[user_maps_num].map_name_pretty =
                malloc(strlen(ntype) + 1);
            strcpy(custom_maps[user_maps_num].map_name_pretty, ntype);
          }

          user_maps_num = user_maps_num + 1;
        }
      }
    }
    closedir(dir);
  }
  custom_map_pages = (int)ceil((double)(user_maps_num + 1) / 15);
}

//=============================================================================
/* MULTIPLAYER MENU */

int m_multiplayer_cursor;
#define MULTIPLAYER_ITEMS 4

void M_Menu_MultiPlayer_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_multiplayer;
  m_entersound = true;
}

void M_MultiPlayer_Draw(void) {
  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  DRAW_HEADER("CO-OP");

  DRAW_MENUOPTION(0, "Join Game", m_multiplayer_cursor, false);
  DRAW_MENUOPTION(1, "Host Game", m_multiplayer_cursor, false);
  DRAW_MENUOPTION(2, "Player Setup", m_multiplayer_cursor, true);

  DRAW_BACKBUTTON(3, m_multiplayer_cursor);

  switch (m_multiplayer_cursor) {
  case 0:
    DRAW_DESCRIPTION("Join a lobby over local network.");
    break;
  case 1:
    DRAW_DESCRIPTION("Host a new lobby for other survivors.");
    break;
  case 2:
    DRAW_DESCRIPTION("Set your username and appearance.");
    break;
  default:
    break;
  }
}

void M_MultiPlayer_Key(int key) {
  extern int m_net_cursor;
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (++m_multiplayer_cursor >= MULTIPLAYER_ITEMS)
      m_multiplayer_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (--m_multiplayer_cursor < 0)
      m_multiplayer_cursor = MULTIPLAYER_ITEMS - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;
    switch (m_multiplayer_cursor) {
    case 0: // Join Game - go to LAN config for searching/joining
      m_net_cursor = 1;
      M_Menu_LanConfig_f();
      break;

    case 1: // Host Game - go directly to game options (skip LAN config)
      m_net_cursor = 1;
      M_Menu_GameOptions_f();
      break;

    case 2:
      M_Menu_Setup_f();
      break;

    case 3:
      M_Menu_Main_f();
      break;
    }
  }
}

//=============================================================================
/* SETUP MENU */

int setup_cursor = 4;
int setup_cursor_table[] = {40, 56, 80, 104, 140};

char setup_hostname[16];
char setup_myname[16];
int setup_oldtop;
int setup_oldbottom;
int setup_top;
int setup_bottom;

#define NUM_SETUP_CMDS 5

void M_Menu_Setup_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_setup;
  m_entersound = true;
  Q_strcpy(setup_myname, cl_name.string);
  Q_strcpy(setup_hostname, hostname.string);
  setup_top = setup_oldtop = ((int)cl_color.value) >> 4;
  setup_bottom = setup_oldbottom = ((int)cl_color.value) & 15;
}

void M_Setup_Draw(void) {
  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  DRAW_HEADER("PLAYER SETUP");

  DRAW_MENUOPTION(
      0,
      va("Hostname:    %s%s", setup_hostname,
         (setup_cursor == 0 && ((int)(realtime * 2) & 1)) ? "_" : ""),
      setup_cursor, false);
  DRAW_MENUOPTION(
      1,
      va("Your Name:   %s%s", setup_myname,
         (setup_cursor == 1 && ((int)(realtime * 2) & 1)) ? "_" : ""),
      setup_cursor, false);
  DRAW_MENUOPTION(2, va("Shirt Color: %i", setup_top), setup_cursor, false);
  DRAW_MENUOPTION(3, va("Pants Color: %i", setup_bottom), setup_cursor, true);
  DRAW_MENUOPTION(4, "Accept Changes", setup_cursor, false);

  if (setup_cursor == 0) {
    DRAW_DESCRIPTION("Press Y or A to type the server hostname.");
  } else if (setup_cursor == 1) {
    DRAW_DESCRIPTION("Press Y or A to type your multiplayer name.");
  } else if (setup_cursor == 2) {
    DRAW_DESCRIPTION("Use Left/Right arrow to change shirt color.");
  } else if (setup_cursor == 3) {
    DRAW_DESCRIPTION("Use Left/Right arrow to change pants color.");
  } else if (setup_cursor == 4) {
    DRAW_DESCRIPTION("Apply changes and return to Multiplayer menu.");
  }
}

void M_Setup_Key(int k) {
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_MultiPlayer_f();
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    setup_cursor--;
    if (setup_cursor < 0)
      setup_cursor = NUM_SETUP_CMDS - 1;
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    setup_cursor++;
    if (setup_cursor >= NUM_SETUP_CMDS)
      setup_cursor = 0;
    break;

  case K_LEFTARROW:
    if (setup_cursor < 2)
      return;
    S_LocalSound("sounds/menu/navigate.wav");
    if (setup_cursor == 2)
      setup_top = setup_top - 1;
    if (setup_cursor == 3)
      setup_bottom = setup_bottom - 1;
    break;
  case K_RIGHTARROW:
    if (setup_cursor < 2)
      return;
  forward:
    S_LocalSound("sounds/menu/navigate.wav");
    if (setup_cursor == 2)
      setup_top = setup_top + 1;
    if (setup_cursor == 3)
      setup_bottom = setup_bottom + 1;
    break;

  case K_YBUTTON:
    // open the on-screen keyboard for the text fields
    if (setup_cursor == 0)
      M_OSK_Open(setup_hostname, 15, false);
    else if (setup_cursor == 1)
      M_OSK_Open(setup_myname, 15, false);
    return;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    if (setup_cursor == 0) {
      M_OSK_Open(setup_hostname, 15, false);
      return;
    }
    if (setup_cursor == 1) {
      M_OSK_Open(setup_myname, 15, false);
      return;
    }

    if (setup_cursor == 2 || setup_cursor == 3)
      goto forward;

    // setup_cursor == 4 (OK)
    if (strcmp(cl_name.string, setup_myname) != 0)
      Cbuf_AddText(va("name \"%s\"\n", setup_myname));
    if (strcmp(hostname.string, setup_hostname) != 0)
      Cvar_Set("hostname", setup_hostname);
    if (setup_top != setup_oldtop || setup_bottom != setup_oldbottom)
      Cbuf_AddText(va("color %i %i\n", setup_top, setup_bottom));
    m_entersound = true;
    M_Menu_MultiPlayer_f();
    break;

  case K_BACKSPACE:
    if (setup_cursor == 0) {
      if (strlen(setup_hostname))
        setup_hostname[strlen(setup_hostname) - 1] = 0;
    }

    if (setup_cursor == 1) {
      if (strlen(setup_myname))
        setup_myname[strlen(setup_myname) - 1] = 0;
    }
    break;
  }

  if (setup_top > 13)
    setup_top = 0;
  if (setup_top < 0)
    setup_top = 13;
  if (setup_bottom > 13)
    setup_bottom = 0;
  if (setup_bottom < 0)
    setup_bottom = 13;
}

void M_Setup_Char(int k) {
  int l;

  switch (setup_cursor) {
  case 0:
    l = strlen(setup_hostname);
    if (l < 15) {
      setup_hostname[l + 1] = 0;
      setup_hostname[l] = k;
    }
    break;
  case 1:
    l = strlen(setup_myname);
    if (l < 15) {
      setup_myname[l + 1] = 0;
      setup_myname[l] = k;
    }
    break;
  }
}

qboolean M_Setup_TextEntry(void) {
  return (setup_cursor == 0 || setup_cursor == 1);
}

//=============================================================================
/* NET MENU */

int m_net_cursor;
int m_net_items;

const char *net_helpMessage[] = {
    /* .........1.........2.... */
    " Novell network LANs    ", " or Windows 95 DOS-box. ",
    "                        ", "(LAN=Local Area Network)",

    " Commonly used to play  ", " over the Internet, but ",
    " also used on a Local   ", " Area Network.          "};

void M_Menu_Net_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_net;
  m_entersound = true;
  m_net_items = 2;

  if (m_net_cursor >= m_net_items)
    m_net_cursor = 0;
  m_net_cursor--;
  M_Net_Key(K_DOWNARROW);
}

void M_Net_Draw(void) {
  int f;
  qpic_t *p;

  // M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
  // p = Draw_CachePic ("gfx/p_multi.lmp");
  // M_DrawPic ( (320-p->width)/2, 4, p);

  f = 32;

  // if (ipxAvailable)
  // 	p = Draw_CachePic ("gfx/netmen3.lmp");
  // else
  // 	p = Draw_CachePic ("gfx/dim_ipx.lmp");
  // M_DrawTransPic (72, f, p);

  f += 19;
  // if (tcpipAvailable)
  // 	p = Draw_CachePic ("gfx/netmen4.lmp");
  // else
  // 	p = Draw_CachePic ("gfx/dim_tcp.lmp");
  // M_DrawTransPic (72, f, p);

  f = (320 - 26 * 8) / 2;
  M_DrawTextBox(f, 96, 24, 4);
  f += 8;
  M_Print(f, 104, net_helpMessage[m_net_cursor * 4 + 0]);
  M_Print(f, 112, net_helpMessage[m_net_cursor * 4 + 1]);
  M_Print(f, 120, net_helpMessage[m_net_cursor * 4 + 2]);
  M_Print(f, 128, net_helpMessage[m_net_cursor * 4 + 3]);

  f = (int)(realtime * 10) % 6;
  // M_DrawTransPic (54, 32 + m_net_cursor * 20,Draw_CachePic(
  // va("gfx/menudot%i.lmp", f+1 ) ) );
}

void M_Net_Key(int k) {
again:
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_MultiPlayer_f();
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (++m_net_cursor >= m_net_items)
      m_net_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (--m_net_cursor < 0)
      m_net_cursor = m_net_items - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;
    M_Menu_LanConfig_f();
    break;
  }

  if (m_net_cursor == 0 && !ipxAvailable)
    goto again;
  if (m_net_cursor == 1 && !tcpipAvailable)
    goto again;
}

//=============================================================================
/* OPTIONS MENU */

enum {
  OPT_CUSTOMIZE = 0,
  OPT_CONSOLE,  // 1
  OPT_DEFAULTS, // 2
  OPT_SCALE,
  OPT_SCRSIZE,
  OPT_GAMMA,
  OPT_CONTRAST,
  OPT_MOUSESPEED,
  OPT_SBALPHA,
  OPT_SNDVOL,
  OPT_MUSICVOL,
  OPT_MUSICEXT,
  OPT_ALWAYRUN,
  OPT_INVMOUSE,
  OPT_ALWAYSMLOOK,
  OPT_LOOKSPRING,
  OPT_LOOKSTRAFE,
  OPT_AIMASSIST,
  // #ifdef _WIN32
  //	OPT_USEMOUSE,
  // #endif
  OPT_VIDEO, // This is the last before OPTIONS_ITEMS
  OPTIONS_ITEMS
};

enum {
  ALWAYSRUN_OFF = 0,
  ALWAYSRUN_VANILLA,
  ALWAYSRUN_QUAKESPASM,
  ALWAYSRUN_ITEMS
};

#define SLIDER_RANGE 10

int options_cursor;
void M_Menu_Options_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  old_m_state = m_state;
  m_state = m_options;
  m_entersound = true;
}

// stupid fucking hack because i am lazy
void M_AdjustSlidersAdvanced(int dir, int option) {
  S_LocalSound("sounds/menu/navigate.wav");

  switch (option) {
  case OPT_GSETTING_GAMMA: // gamma
    vid_gamma.value -= dir * 0.05;
    if (vid_gamma.value < 0.5)
      vid_gamma.value = 0.5;
    if (vid_gamma.value > 1)
      vid_gamma.value = 1;
    Cvar_SetValue("gamma", vid_gamma.value);
    break;
  case OPT_GSETTING_MAXFPS:
    host_maxfps.value = host_maxfps.value + dir * 5;
    if (host_maxfps.value < 30)
      host_maxfps.value = 30;
    if (host_maxfps.value > 65)
      host_maxfps.value = 65;
    Cvar_SetValue("host_maxfps", host_maxfps.value);
    break;
  case OPT_GSETTING_FOV:
    scr_fov.value += dir * 5;
    if (scr_fov.value < 50)
      scr_fov.value = 50;
    if (scr_fov.value > 120 && dir > 0)
      scr_fov.value = 120;
    Cvar_SetValue("fov", scr_fov.value);
    break;
  case OPT_CSETTING_LSENS:
    sensitivity.value += dir * 0.5;
    if (sensitivity.value < 1)
      sensitivity.value = 1;
    if (sensitivity.value > 11)
      sensitivity.value = 11;
    Cvar_SetValue("sensitivity", sensitivity.value);
    break;
  case OPT_CSETTING_GSEX:
    gyrosensx.value += dir * 0.05;
    if (gyrosensx.value < 0.5)
      gyrosensx.value = 0.5;
    if (gyrosensx.value > 1)
      gyrosensx.value = 1;
    Cvar_SetValue("gyrosensx", gyrosensx.value);
    break;
  case OPT_CSETTING_GSEY:
    gyrosensy.value += dir * 0.05;
    if (gyrosensy.value < 0.5)
      gyrosensy.value = 0.5;
    if (gyrosensy.value > 1)
      gyrosensy.value = 1;
    Cvar_SetValue("gyrosensy", gyrosensy.value);
    break;
  case OPT_CSETTING_RUMBLE:
    in_rumble_scale.value += dir * 0.25;
    if (in_rumble_scale.value < 0.25)
      in_rumble_scale.value = 0.25;
    if (in_rumble_scale.value > 1.5)
      in_rumble_scale.value = 1.5;
    Cvar_SetValue("in_rumble_scale", in_rumble_scale.value);
    break;
  /*case OPT_CSETTING_LACC:
          in_acceleration.value -= dir * 0.25;
          if (in_acceleration.value < 0.5)
                  in_acceleration.value = 0.5;
          if (in_acceleration.value > 2)
                  in_acceleration.value = 2;
          Cvar_SetValue ("acceleration", in_acceleration.value);
          break;*/
  default:
    break;
  }
}

void M_AdjustSliders(int dir) {
  float f, l;

  S_LocalSound("sounds/menu/navigate.wav");

  switch (options_cursor) {
  case OPT_SCALE: // console and menu scale
    l = ((vid.width + 31) / 32) / 10.0;
    f = scr_conscale.value + dir * .1;
    if (f < 1)
      f = 1;
    else if (f > l)
      f = l;
    Cvar_SetValue("scr_conscale", f);
    Cvar_SetValue("scr_menuscale", f);
    Cvar_SetValue("scr_sbarscale", f);
    break;
  case OPT_SCRSIZE: // screen size
    f = scr_viewsize.value + dir * 10;
    if (f > 120)
      f = 120;
    else if (f < 30)
      f = 30;
    Cvar_SetValue("viewsize", f);
    break;
  case OPT_GAMMA: // gamma
    f = vid_gamma.value - dir * 0.05;
    if (f < 0.5)
      f = 0.5;
    else if (f > 1)
      f = 1;
    Cvar_SetValue("gamma", f);
    break;
  case OPT_CONTRAST: // contrast
    f = vid_contrast.value + dir * 0.1;
    if (f < 1)
      f = 1;
    else if (f > 2)
      f = 2;
    Cvar_SetValue("contrast", f);
    break;
  case OPT_MOUSESPEED: // mouse speed
    f = sensitivity.value + dir * 0.5;
    if (f > 11)
      f = 11;
    else if (f < 1)
      f = 1;
    Cvar_SetValue("sensitivity", f);
    break;
  case OPT_SBALPHA: // statusbar alpha
    f = scr_sbaralpha.value - dir * 0.05;
    if (f < 0)
      f = 0;
    else if (f > 1)
      f = 1;
    Cvar_SetValue("scr_sbaralpha", f);
    break;
  case OPT_MUSICVOL: // music volume
    f = bgmvolume.value + dir * 0.1;
    if (f < 0)
      f = 0;
    else if (f > 1)
      f = 1;
    Cvar_SetValue("bgmvolume", f);
    break;
  case OPT_MUSICEXT: // enable external music vs cdaudio
    Cvar_Set("bgm_extmusic", bgm_extmusic.value ? "0" : "1");
    break;
  case OPT_SNDVOL: // sfx volume
    f = sfxvolume.value + dir * 0.1;
    if (f < 0)
      f = 0;
    else if (f > 1)
      f = 1;
    Cvar_SetValue("volume", f);
    break;

  case OPT_ALWAYRUN: // always run
    Cvar_SetValue("cl_alwaysrun", 0);
    Cvar_SetValue("cl_forwardspeed", 165);
    Cvar_SetValue("cl_backspeed", 140);
    break;

  case OPT_INVMOUSE: // invert mouse
    Cvar_SetValue("m_pitch", -m_pitch.value);
    break;

  case OPT_ALWAYSMLOOK:
    if (in_mlook.state & 1)
      Cbuf_AddText("-mlook");
    else
      Cbuf_AddText("+mlook");
    break;

  case OPT_LOOKSPRING: // lookspring
    Cvar_Set("lookspring", lookspring.value ? "0" : "1");
    break;

  case OPT_LOOKSTRAFE: // lookstrafe
    Cvar_Set("lookstrafe", lookstrafe.value ? "0" : "1");
    break;

  case OPT_AIMASSIST:
    Cvar_Set("in_aimassist", in_aimassist.value ? "0" : "1");
    break;
  }
}

void M_DrawSlider(int x, int y, float range, float scale) {
  int i;

  if (range < 0)
    range = 0;
  if (range > 1)
    range = 1;
  Draw_CharacterScale(x - 8 * scale, y, 128, scale);
  for (i = 0; i < SLIDER_RANGE; i++)
    Draw_CharacterScale(x + i * 8 * scale, y, 129, scale);
  Draw_CharacterScale(x + i * 8 * scale, y, 130, scale);
  Draw_CharacterScale(x + (SLIDER_RANGE - 1) * 8 * range * scale, y, 131,
                      scale);
}

void M_DrawCheckbox(int x, int y, int on) {
#if 0
	if (on)
		M_DrawCharacter (x, y, 131);
	else
		M_DrawCharacter (x, y, 129);
#endif
  if (on)
    M_Print(x, y, "on");
  else
    M_Print(x, y, "off");
}

#define OPTION_ITEMS 5

void M_Options_Draw(void) {
  MENU_INITVARS();

  // Menu Background
  if (paused_hack == false)
    Draw_BgMenu();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Header
  DRAW_HEADER("SETTINGS");

  DRAW_MENUOPTION(0, "Graphics Settings", options_cursor, false);
  DRAW_MENUOPTION(1, "Controls", options_cursor, false);
  DRAW_MENUOPTION(2, "Control Settings", options_cursor, true);
  DRAW_MENUOPTION(3, "Console", options_cursor, false);

  DRAW_BACKBUTTON(4, options_cursor);

  // Descriptions
  switch (options_cursor) {
  case 0:
    DRAW_DESCRIPTION("Adjust settings relating to Graphical Fidelity.");
    break;
  case 1:
    DRAW_DESCRIPTION("Customize your Control Scheme.");
    break;
  case 2:
    DRAW_DESCRIPTION("Adjust settings in relation to how NZ:P Controls.");
    break;
  case 3:
    DRAW_DESCRIPTION("Option the Console to input Commands.");
    break;
  default:
    break;
  }
}

extern qboolean console_enabled;
void M_Options_Key(int k) {
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:

    if (paused_hack == true) {
      M_Paused_Menu_f();
    } else {
      M_Menu_Main_f();
    }

    break;
  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;

    switch (options_cursor) {
    case 0:
      M_Graphics_Settings_f();
      break;
    case 1:
      M_Menu_Keys_f();
      break;
    case 2:
      M_Control_Settings_f();
      break;
    case 3:
      m_state = m_none;
      paused_hack = false;
      console_enabled = true;
      Con_ToggleConsole_f();
      break;
    case 4:

      if (paused_hack == true) {
        M_Paused_Menu_f();
      } else {
        M_Menu_Main_f();
      }

      break;
    default:
      break;
    }

    break;
  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    options_cursor--;
    if (options_cursor < 0) {
      options_cursor = OPTION_ITEMS - 1;
    }
    break;
  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    options_cursor++;
    if (options_cursor >= OPTION_ITEMS)
      options_cursor = 0;
    break;
  }
}

//
// NZ:P Graphics Settings
//

void M_Graphics_Settings_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_net; // lazy
  m_entersound = true;
}

static int gsettings_cursor;

#define GSETTINGS_ITEMS 19

extern cvar_t gl_motionblur;
extern cvar_t r_ssaa;     // supersampling AA
extern cvar_t r_bloom;    // bloom
extern cvar_t r_vignette; // vignette
extern cvar_t r_fog_color;
extern cvar_t r_zombie_eyecolor; // zombie eye-glow colour
extern cvar_t r_lightscale;      // lightmap brightness multiplier (live)
extern cvar_t r_lightambient;    // lightmap ambient floor 0..64 (live)
extern cvar_t r_lightgamma; // shadow-lift gamma (live), <1 lifts dark surfaces

extern cvar_t r_cg_contrast;
extern cvar_t r_cg_saturation;
extern cvar_t r_tonemap;
extern cvar_t r_filmgrain;
extern cvar_t r_ssao;
extern cvar_t r_quality;  // master quality preset
extern cvar_t nzp_weather_ndu;

#ifdef VITA
#define DRAW_COL_MENUOPTION(col, row, id, txt, cursor)                         \
  {                                                                            \
    int col_x = (col == 0) ? 10 : 480;                                         \
    int row_y = y + 70 + (19 * row);                                           \
    if (cursor == id)                                                          \
      Draw_ColoredStringScale(col_x, row_y, txt, 1, 0, 0, 1, 2.0f);            \
    else                                                                       \
      Draw_ColoredStringScale(col_x, row_y, txt, 1, 1, 1, 1, 2.0f);            \
  }
#define DRAW_COL_SETTINGSVALUE(col, row, id, txt)                              \
  {                                                                            \
    int col_val_x = (col == 0) ? 250 : 720;                                    \
    int row_y = y + 70 + (19 * row);                                           \
    Draw_ColoredStringScale(col_val_x, row_y, txt, 1, 1, 1, 1, 2.0f);          \
  }
#define DRAW_COL_SLIDER(col, row, id, r)                                       \
  {                                                                            \
    int col_val_x = (col == 0) ? 258 : 728;                                    \
    int row_y = y + 70 + (19 * row);                                           \
    M_DrawSlider(col_val_x, row_y, r, 2.0f);                                   \
  }
#else
#define DRAW_COL_MENUOPTION(col, row, id, txt, cursor)                         \
  {                                                                            \
    int col_x = (col == 0) ? 10 : 330;                                         \
    int row_y = y + 55 + (15 * row);                                           \
    if (cursor == id) {                                                        \
      int _bw = (int)getTextWidth(txt, 1.5f) + 12;                             \
      Draw_FillByColor(col_x - 4, row_y - 4, _bw, 20, 0, 0, 0, 230);           \
      Draw_FillByColor(col_x - 4, row_y - 4, _bw, 2, 150, 0, 0, 255);          \
      Draw_FillByColor(col_x - 4, row_y + 14, _bw, 2, 150, 0, 0, 255);         \
      Draw_FillByColor(col_x - 4, row_y - 4, 2, 20, 150, 0, 0, 255);           \
      Draw_FillByColor(col_x - 4 + _bw, row_y - 4, 2, 20, 150, 0, 0, 255);     \
      Draw_ColoredStringScale(col_x + 1, row_y, txt, 1, 0, 0, 1, 1.5f);        \
    } else                                                                     \
      Draw_ColoredStringScale(col_x, row_y, txt, 1, 1, 1, 1, 1.5f);            \
  }
#define DRAW_COL_SETTINGSVALUE(col, row, id, txt)                              \
  {                                                                            \
    int col_val_x = (col == 0) ? 200 : 520;                                    \
    int row_y = y + 55 + (15 * row);                                           \
    Draw_ColoredStringScale(col_val_x, row_y, txt, 1, 1, 1, 1, 1.5f);          \
  }
#define DRAW_COL_SLIDER(col, row, id, r)                                       \
  {                                                                            \
    int col_val_x = (col == 0) ? 208 : 528;                                    \
    int row_y = y + 55 + (15 * row);                                           \
    M_DrawSlider(col_val_x, row_y, r, 1.0f);                                   \
  }
#endif

void M_Graphics_Settings_Draw(void) {
  MENU_INITVARS();
  float r;

  // Menu Background
  if (paused_hack == false)
    Draw_BgMenu();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Header
  DRAW_HEADER("GRAPHICS SETTINGS");

  // Left column (col 0)
  DRAW_COL_MENUOPTION(0, 0, 0, "Quality Preset", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 1, 1, "Max FPS", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 2, 2, "Field of View", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 3, 3, "Brightness", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 4, 4, "Fullbright", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 5, 5, "Retro", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 6, 6, "Third Person", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 7, 7, "Fog Color", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 8, 8, "Zombie Eye Color", gsettings_cursor);
  DRAW_COL_MENUOPTION(0, 9, 9, "Shadow Lift", gsettings_cursor);

  // Right column (col 1)
  DRAW_COL_MENUOPTION(1, 0, 10, "Motion Blur", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 1, 11, "Supersampling", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 2, 12, "Bloom", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 3, 13, "Vignette", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 4, 14, "Contrast", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 5, 15, "Saturation", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 6, 16, "Filmic Tone", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 7, 17, "Ambient Occlusion", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 8, 18, "Weather Toggle", gsettings_cursor);
  DRAW_COL_MENUOPTION(1, 9, 19, "Show FPS", gsettings_cursor);

  DRAW_BACKBUTTON(20, gsettings_cursor);

  // Values / Sliders - Left column (col 0)
  DRAW_COL_SETTINGSVALUE(0, 0, 0,
    ((int)r_quality.value == 1) ? "Performance" :
    ((int)r_quality.value == 2) ? "Balanced" :
    ((int)r_quality.value == 3) ? "Quality" : "Custom");

  r = (host_maxfps.value - 30.0) * (1.0 / 35.0);
  DRAW_COL_SLIDER(0, 1, 1, r);

  r = (scr_fov.value - 50.0) * (1.0 / 70.0);
  DRAW_COL_SLIDER(0, 2, 2, r);

  r = (1.0 - vid_gamma.value) / 0.5;
  DRAW_COL_SLIDER(0, 3, 3, r);

  if (r_fullbright.value == 0) {
    DRAW_COL_SETTINGSVALUE(0, 4, 4, "Disabled");
  } else {
    DRAW_COL_SETTINGSVALUE(0, 4, 4, "Enabled");
  }

  if (!strcmp(gl_texturemode.string, "GL_LINEAR")) {
    DRAW_COL_SETTINGSVALUE(0, 5, 5, "Disabled");
  } else {
    DRAW_COL_SETTINGSVALUE(0, 5, 5, "Enabled");
  }

  if (chase_active.value == 0) {
    DRAW_COL_SETTINGSVALUE(0, 6, 6, "Disabled");
  } else {
    DRAW_COL_SETTINGSVALUE(0, 6, 6, "Enabled");
  }

  switch ((int)r_fog_color.value) {
  case 0:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Map Default");
    break;
  case 1:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "White");
    break;
  case 2:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Grey");
    break;
  case 3:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Black");
    break;
  case 4:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Red");
    break;
  case 5:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Green");
    break;
  case 6:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Blue");
    break;
  case 7:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Yellow");
    break;
  case 8:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Purple");
    break;
  default:
    DRAW_COL_SETTINGSVALUE(0, 7, 7, "Map Default");
    break;
  }

  switch ((int)r_zombie_eyecolor.value) {
  case 0:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Yellow");
    break;
  case 1:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Red");
    break;
  case 2:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Green");
    break;
  case 3:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Blue");
    break;
  case 4:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Orange");
    break;
  case 5:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Purple");
    break;
  case 6:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Cyan");
    break;
  case 7:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "White");
    break;
  case 8:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Pink");
    break;
  default:
    DRAW_COL_SETTINGSVALUE(0, 8, 8, "Yellow");
    break;
  }

  r = r_lightambient.value * (1.0 / 48.0);
  DRAW_COL_SLIDER(0, 9, 9, r);

  // Values / Sliders - Right column (col 1)
  if (gl_motionblur.value <= 0) {
    DRAW_COL_SETTINGSVALUE(1, 0, 10, "Disabled");
  } else {
    DRAW_COL_SETTINGSVALUE(1, 0, 10, "Enabled");
  }

  if (r_ssaa.value <= 1) {
    DRAW_COL_SETTINGSVALUE(1, 1, 11, "Disabled");
  } else {
    DRAW_COL_SETTINGSVALUE(1, 1, 11, va("%gx", r_ssaa.value));
  }

  DRAW_COL_SETTINGSVALUE(1, 2, 12, r_bloom.value ? "Enabled" : "Disabled");
  DRAW_COL_SETTINGSVALUE(1, 3, 13, r_vignette.value ? "Enabled" : "Disabled");

  r = (r_cg_contrast.value - 0.5) / 1.5;
  DRAW_COL_SLIDER(1, 4, 14, r);

  r = r_cg_saturation.value / 2.0;
  DRAW_COL_SLIDER(1, 5, 15, r);

  DRAW_COL_SETTINGSVALUE(1, 6, 16,
    (r_tonemap.value < 0.5) ? "Off" : (r_tonemap.value < 1.5) ? "Clean"
    : (r_tonemap.value < 2.5) ? "Cinematic" : (r_tonemap.value < 3.5) ? "Cold"
    : (r_tonemap.value < 4.5) ? "Noir" : "Vintage");
  DRAW_COL_SETTINGSVALUE(1, 7, 17,
    (r_ssao.value < 0.001) ? "Off" : (r_ssao.value < 2.0) ? "Low"
    : (r_ssao.value < 3.0) ? "Medium" : "High");
  DRAW_COL_SETTINGSVALUE(1, 8, 18,
    (nzp_weather_ndu.value != 0) ? "NDU: Rain" : "NDU: None");

  DRAW_COL_SETTINGSVALUE(1, 9, 19,
    (scr_showfps.value == 0) ? "Disabled" : "Enabled");

  // Descriptions
  switch (gsettings_cursor) {
  case 0:
    DRAW_DESCRIPTION("One-tap quality preset: scales SSAA, AO, bloom, motion blur.");
    break;
  case 1:
    DRAW_DESCRIPTION("Increase or Decrease Max Frames per Second.");
    break;
  case 2:
    DRAW_DESCRIPTION("Adjust Game Field of View.");
    break;
  case 3:
    DRAW_DESCRIPTION("Increase or Decrease Game Brightness.");
    break;
  case 4:
    DRAW_DESCRIPTION("Toggle all non-realtime lights.");
    break;
  case 5:
    DRAW_DESCRIPTION("Toggle gl_nearest texture filtering.");
    break;
  case 6:
    DRAW_DESCRIPTION("Toggle Over-the-Shoulder Third Person Camera.");
    break;
  case 7:
    DRAW_DESCRIPTION("Change Fog Color (Map Default or Color Override).");
    break;
  case 8:
    DRAW_DESCRIPTION("Change the glow color of zombie eyes.");
    break;
  case 9:
    DRAW_DESCRIPTION("Brighten dark areas and shadows (0 = map default).");
    break;
  case 10:
    DRAW_DESCRIPTION("PC-style motion blur trail on the 3D view.");
    break;
  case 11:
    DRAW_DESCRIPTION(
        "Supersample anti-aliasing. Higher = smoother but costlier.");
    break;
  case 12:
    DRAW_DESCRIPTION("Soft glow on bright areas (lights, fire, eyes).");
    break;
  case 13:
    DRAW_DESCRIPTION("Subtle darkening toward the screen corners.");
    break;
  case 14:
    DRAW_DESCRIPTION("Adjust image contrast (higher = punchier blacks).");
    break;
  case 15:
    DRAW_DESCRIPTION("Adjust color saturation (0.0 = black and white).");
    break;
  case 16:
    DRAW_DESCRIPTION("Cinematic tone/color grade preset (Left/Right to change).");
    break;
  case 17:
    DRAW_DESCRIPTION("Ambient occlusion (soft contact shadows). Requires SSAA on.");
    break;
  case 18:
    DRAW_DESCRIPTION("Toggle NDU weather (rain, puddles, lightning).");
    break;
  case 19:
    DRAW_DESCRIPTION("Toggle Framerate Overlay.");
    break;
  default:
    break;
  }
}

void M_Graphics_Settings_Key(int key) {
  int fog_val;
  int r;
  switch (key) {
  case K_ABUTTON:
    S_LocalSound("sounds/menu/navigate.wav");
    switch (gsettings_cursor) {
    case 0: { // Quality Preset: cycle Custom -> Performance -> Balanced -> Quality
      float q = (int)r_quality.value + 1;
      if (q > 3) q = 0;
      Cvar_SetValue("r_quality", q);
      break;
    }
    case 4:
      Cvar_SetValue("r_fullbright", r_fullbright.value ? 0 : 1);
      break;
    case 5:
      Cvar_Set("gl_texturemode", strcmp(gl_texturemode.string, "GL_LINEAR")
                                     ? "GL_LINEAR"
                                     : "GL_NEAREST_MIPMAP_LINEAR");
      break;
    case 6:
      Cvar_SetValue("chase_active", chase_active.value ? 0 : 1);
      break;
    case 7: // Fog Color - cycle forward on A button
      fog_val = ((int)r_fog_color.value + 1) % 9;
      Cvar_SetValue("r_fog_color", fog_val);
      break;
    case 8: { // Zombie Eye Color: cycle 0..8
      float e = (int)r_zombie_eyecolor.value + 1;
      if (e > 8)
        e = 0;
      Cvar_SetValue("r_zombie_eyecolor", e);
      break;
    }
    case 10:
      Cvar_SetValue("gl_motionblur", gl_motionblur.value > 0 ? 0 : 0.35);
      break;
    case 11: { // Supersampling: cycle 1 -> 2 -> 3 -> 4 -> off
      float s = r_ssaa.value + 1;
      if (s > 4)
        s = 1;
      Cvar_SetValue("r_ssaa", s);
      break;
    }
    case 12:
      Cvar_SetValue("r_bloom", r_bloom.value ? 0 : 1);
      break;
    case 13:
      Cvar_SetValue("r_vignette", r_vignette.value ? 0 : 1);
      break;
    case 16: { // Filmic Tone: cycle 0..5
      float t = r_tonemap.value + 1;
      if (t > 5) t = 0;
      Cvar_SetValue("r_tonemap", t);
      break;
    }
    case 17: { // Ambient Occlusion: Off -> Low -> Medium -> High
      float a = (r_ssao.value < 0.001) ? 1.5 : (r_ssao.value < 2.0) ? 2.5
              : (r_ssao.value < 3.0) ? 3.5 : 0.0;
      Cvar_SetValue("r_ssao", a);
      break;
    }
    case 18:
      Cvar_SetValue("nzp_weather_ndu", nzp_weather_ndu.value != 0 ? 0 : 1);
      break;
    case 19:
      Cvar_SetValue("scr_showfps", scr_showfps.value ? 0 : 1);
      break;
    case 20:
      M_Menu_Options_f();
      break;
    default:
      break;
    }
    break;
  case K_BBUTTON:
    M_Menu_Options_f();
    break;
  case K_LEFTARROW:
    switch (gsettings_cursor) {
    case 1:
      M_AdjustSlidersAdvanced(-1, OPT_GSETTING_MAXFPS);
      break;
    case 2:
      M_AdjustSlidersAdvanced(-1, OPT_GSETTING_FOV);
      break;
    case 3:
      M_AdjustSlidersAdvanced(-1, OPT_GSETTING_GAMMA);
      break;
    case 7: // Fog Color - cycle backward
      fog_val = (int)r_fog_color.value - 1;
      if (fog_val < 0)
        fog_val = 8;
      Cvar_SetValue("r_fog_color", fog_val);
      break;
    case 8: { // Zombie Eye Color - cycle backward
      float e = (int)r_zombie_eyecolor.value - 1;
      if (e < 0)
        e = 8;
      Cvar_SetValue("r_zombie_eyecolor", e);
      break;
    }
    case 9: { // Shadow Lift - (drives the ambient floor + lightmap gamma
              // together)
      float amt = (r_lightambient.value / 48.0) - 0.125;
      if (amt < 0)
        amt = 0;
      Cvar_SetValue("r_lightambient", amt * 48.0);
      Cvar_SetValue("r_lightgamma", 1.0 - amt * 0.35);
      break;
    }
    case 14:
      Cvar_SetValue("r_cg_contrast",
                    CLAMP(0.5, r_cg_contrast.value - 0.05, 2.0));
      break;
    case 15:
      Cvar_SetValue("r_cg_saturation",
                    CLAMP(0.0, r_cg_saturation.value - 0.05, 2.0));
      break;
    case 16: { // Filmic Tone - cycle backward
      float t = r_tonemap.value - 1;
      if (t < 0) t = 5;
      Cvar_SetValue("r_tonemap", t);
      break;
    }
    default:
      break;
    }
    break;
  case K_RIGHTARROW:
    switch (gsettings_cursor) {
    case 1:
      M_AdjustSlidersAdvanced(1, OPT_GSETTING_MAXFPS);
      break;
    case 2:
      M_AdjustSlidersAdvanced(1, OPT_GSETTING_FOV);
      break;
    case 3:
      M_AdjustSlidersAdvanced(1, OPT_GSETTING_GAMMA);
      break;
    case 7: // Fog Color - cycle forward
      fog_val = ((int)r_fog_color.value + 1) % 9;
      Cvar_SetValue("r_fog_color", fog_val);
      break;
    case 8: { // Zombie Eye Color - cycle forward
      float e = ((int)r_zombie_eyecolor.value + 1) % 9;
      Cvar_SetValue("r_zombie_eyecolor", e);
      break;
    }
    case 9: { // Shadow Lift + (drives the ambient floor + lightmap gamma
              // together)
      float amt = (r_lightambient.value / 48.0) + 0.125;
      if (amt > 1.0)
        amt = 1.0;
      Cvar_SetValue("r_lightambient", amt * 48.0);
      Cvar_SetValue("r_lightgamma", 1.0 - amt * 0.35);
      break;
    }
    case 14:
      Cvar_SetValue("r_cg_contrast",
                    CLAMP(0.5, r_cg_contrast.value + 0.05, 2.0));
      break;
    case 15:
      Cvar_SetValue("r_cg_saturation",
                    CLAMP(0.0, r_cg_saturation.value + 0.05, 2.0));
      break;
    case 16: { // Filmic Tone - cycle forward
      float t = r_tonemap.value + 1;
      if (t > 5) t = 0;
      Cvar_SetValue("r_tonemap", t);
      break;
    }
    default:
      break;
    }
    break;
  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    gsettings_cursor--;

    if (gsettings_cursor < 0)
      gsettings_cursor = GSETTINGS_ITEMS;
    break;
  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    gsettings_cursor++;

    if (gsettings_cursor > GSETTINGS_ITEMS)
      gsettings_cursor = 0;
    break;
  default:
    break;
  }
}

//
// NZ:P Control Settings
//

void M_Control_Settings_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_video; // lazy
  m_entersound = true;
}

static int csettings_cursor;

#ifdef VITA
void Vita_ToggleRearTouchPad(void) {
  if (cl_enablereartouchpad.value == 0) {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, 0);
    sceTouchDisableTouchForce(SCE_TOUCH_PORT_BACK);
  } else {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, 1);
    sceTouchEnableTouchForce(SCE_TOUCH_PORT_BACK);
  }
}
#endif

//
// PSVita requires an extra menu option for disabling of the rear touch pad.
//
#ifdef VITA
#define CSETTINGS_ITEMS 10
#else
#define CSETTINGS_ITEMS 11
#endif

void M_Control_Settings_Draw(void) {
  MENU_INITVARS();
  float r;

  // Menu Background
  if (paused_hack == false)
    Draw_BgMenu();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Header
  DRAW_HEADER("CONTROL SETTINGS");

  DRAW_MENUOPTION(0, "Draw Crosshair", csettings_cursor, false);
  DRAW_MENUOPTION(1, "Aim Assist", csettings_cursor, false);
  DRAW_MENUOPTION(2, "Look Sensitivity", csettings_cursor, false);
  DRAW_BLANKOPTION("Look Acceleration", false);
  DRAW_MENUOPTION(4, "Look Inversion", csettings_cursor, false);
  DRAW_MENUOPTION(5, "Gyroscopic Aim", csettings_cursor, false);
  DRAW_MENUOPTION(6, "Gyro Mode", csettings_cursor, false);
  DRAW_MENUOPTION(7, "Gyro Sensitivity X", csettings_cursor, false);
  DRAW_MENUOPTION(8, "Gyro Sensitivity Y", csettings_cursor, false);

#ifdef VITA

  DRAW_MENUOPTION(9, "Rear TouchPad", csettings_cursor, false);
  DRAW_BACKBUTTON(10, csettings_cursor);

#else

  DRAW_MENUOPTION(9, "Gun Rumble", csettings_cursor, false);
  DRAW_MENUOPTION(10, "Rumble Strength", csettings_cursor, false);
  DRAW_BACKBUTTON(11, csettings_cursor);

  // Gun Rumble
  if (in_rumble.value == 0) {
    DRAW_SETTINGSVALUE(9, "Disabled");
  } else {
    DRAW_SETTINGSVALUE(9, "Enabled");
  }

  // Rumble Strength
  r = (in_rumble_scale.value - 0.25) * (1.0 / 1.25);
  DRAW_SLIDER(10, r);

#endif // VITA

  // Draw Crosshair
  if (crosshair.value == 0) {
    DRAW_SETTINGSVALUE(0, "Disabled");
  } else {
    DRAW_SETTINGSVALUE(0, "Enabled");
  }

  // Aim Assist
  if (in_aimassist.value == 0) {
    DRAW_SETTINGSVALUE(1, "Disabled");
  } else {
    DRAW_SETTINGSVALUE(1, "Enabled");
  }

  // Look Sensitivity
  r = (sensitivity.value - 1) / 10;
  DRAW_SLIDER(2, r);

  // Look Inversion
  if (joy_invert.value == 0) {
    DRAW_SETTINGSVALUE(4, "Disabled");
  } else {
    DRAW_SETTINGSVALUE(4, "Enabled");
  }

  // Gyroscopic Aim
  if (motioncam.value == 0) {
    DRAW_SETTINGSVALUE(5, "Disabled");
  } else {
    DRAW_SETTINGSVALUE(5, "Enabled");
  }

  // Gyro Mode
  if (gyromode.value == 0) {
    DRAW_SETTINGSVALUE(6, "Always On");
  } else {
    DRAW_SETTINGSVALUE(6, "ADS Only");
  }

  // Gyro Sensitivity X
  r = (gyrosensx.value - 0.5) * (1.0 / 0.5);
  DRAW_SLIDER(7, r);

  // Gyro Sensitivity Y
  r = (gyrosensy.value - 0.5) * (1.0 / 0.5);
  DRAW_SLIDER(8, r);

#ifdef VITA

  // Rear TouchPad
  if (cl_enablereartouchpad.value == 0) {
    DRAW_SETTINGSVALUE(9, "Disabled");
  } else {
    DRAW_SETTINGSVALUE(9, "Enabled");
  }

#endif // VITA

  // Descriptions
  switch (csettings_cursor) {
  case 0:
    DRAW_DESCRIPTION("Toggle Crosshair in-game.");
    break;
  case 1:
    DRAW_DESCRIPTION("Toggle Assisted Aim to improve Targeting.");
    break;
  case 2:
    DRAW_DESCRIPTION("Adjust Look Sensitivity.");
    break;
  case 3:
    DRAW_DESCRIPTION("Adjust Look Acceleration.");
    break;
  case 4:
    DRAW_DESCRIPTION("Toggle inverted Camera control.");
    break;
  case 5:
    DRAW_DESCRIPTION("Toggle Gyroscopic Aiming.");
    break;
  case 6:
    DRAW_DESCRIPTION("Set to use Gyro Always or only when ADS.");
    break;
  case 7:
    DRAW_DESCRIPTION("Adjust Gyro Sensitivty on the X Axis.");
    break;
  case 8:
    DRAW_DESCRIPTION("Adjust Gyro Sensitivty on the Y Axis.");
    break;

#ifdef VITA

  case 9:
    DRAW_DESCRIPTION("Toggle support for the PSVita Rear TouchPad.");
    break;

#else

  case 9:
    DRAW_DESCRIPTION("Toggle controller rumble when firing.");
    break;
  case 10:
    DRAW_DESCRIPTION("Adjust rumble strength (safely capped).");
    break;

#endif // VITA
  }
}

void M_Control_Settings_Key(int key) {
  switch (key) {
  case K_ABUTTON:
    S_LocalSound("sounds/menu/navigate.wav");
    switch (csettings_cursor) {
    case 0:
      Cvar_SetValue("crosshair", crosshair.value ? 0 : 1);
      break;
    case 1:
      Cvar_SetValue("in_aimassist", in_aimassist.value ? 0 : 1);
      break;
    // case 2: break;
    // case 3: break;
    case 4:
      Cvar_SetValue("joy_invert", joy_invert.value ? 0 : 1);
      break;
    case 5:
      Cvar_SetValue("motioncam", motioncam.value ? 0 : 1);
      break;
    case 6:
      Cvar_SetValue("gyromode", gyromode.value ? 0 : 1);
      break;
#ifdef VITA
    case 9:
      Cvar_SetValue("cl_enablereartouchpad",
                    cl_enablereartouchpad.value ? 0 : 1);
      Vita_ToggleRearTouchPad();
      break;
    case 10:
      M_Menu_Options_f();
      break;
#else
    case 9:
      Cvar_SetValue("in_rumble", in_rumble.value ? 0 : 1);
      break;
    case 11:
      M_Menu_Options_f();
      break;
#endif
    default:
      break;
    }
    break;
  case K_BBUTTON:
    M_Menu_Options_f();
    break;
  case K_LEFTARROW:
    switch (csettings_cursor) {
    case 2:
      M_AdjustSlidersAdvanced(-1, OPT_CSETTING_LSENS);
      break;
    case 3:
      M_AdjustSlidersAdvanced(-1, OPT_CSETTING_LACC);
      break;
    case 7:
      M_AdjustSlidersAdvanced(-1, OPT_CSETTING_GSEX);
      break;
    case 8:
      M_AdjustSlidersAdvanced(-1, OPT_CSETTING_GSEY);
      break;
    case 10:
      M_AdjustSlidersAdvanced(-1, OPT_CSETTING_RUMBLE);
      break;
    default:
      break;
    }
    break;
  case K_RIGHTARROW:
    switch (csettings_cursor) {
    case 2:
      M_AdjustSlidersAdvanced(1, OPT_CSETTING_LSENS);
      break;
    case 3:
      M_AdjustSlidersAdvanced(1, OPT_CSETTING_LACC);
      break;
    case 7:
      M_AdjustSlidersAdvanced(1, OPT_CSETTING_GSEX);
      break;
    case 8:
      M_AdjustSlidersAdvanced(1, OPT_CSETTING_GSEY);
      break;
    case 10:
      M_AdjustSlidersAdvanced(1, OPT_CSETTING_RUMBLE);
      break;
    default:
      break;
    }
    break;
  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    csettings_cursor--;

    if (csettings_cursor == 3)
      csettings_cursor = 2;

    if (csettings_cursor < 0)
      csettings_cursor = CSETTINGS_ITEMS;
    break;
  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    csettings_cursor++;

    if (csettings_cursor == 3)
      csettings_cursor = 4;

    if (csettings_cursor > CSETTINGS_ITEMS)
      csettings_cursor = 0;
    break;
  default:
    break;
  }
}

//=============================================================================
/* KEYS MENU */

char *bindnames[][2] = {
#if 0 // Current consoles using this engine do have those buttons mapped to
      // analogs
	{"+forward", 		"Walk Forward"},
	{"+back", 			"Walk Backward"},
	{"+moveleft", 		"Move Left"},
	{"+moveright", 		"Move Right"},
	{"+lookup", 		"Look Up"},
	{"+lookdown", 		"Look Down"},
	{"+left", 			"Look Left"},
	{"+right", 			"Look Right"},
#endif
    {"+jump", "Jump"},          {"+attack", "Fire"},
    {"+aim", "Aim Down Sight"}, {"+switch", "Switch Weapon"},
    {"+use", "Interact"},       {"+reload", "Reload"},
    {"+knife", "Melee"},        {"+grenade", "Grenade"},
    {"impulse 23", "Sprint"},   {"impulse 30", "Crouch"}};

#define NUMCOMMANDS (sizeof(bindnames) / sizeof(bindnames[0]))

static int keys_cursor;
static qboolean bind_grab;

void M_Menu_Keys_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_keys;
  m_entersound = true;
}

void M_FindKeysForCommand(const char *command, int *threekeys) {
  int count;
  int j;
  int l;
  char *b;

  threekeys[0] = threekeys[1] = threekeys[2] = -1;
  l = strlen(command);
  count = 0;

  for (j = 0; j < MAX_KEYS; j++) {
    b = keybindings[j];
    if (!b)
      continue;
    if (!strncmp(b, command, l)) {
      threekeys[count] = j;
      count++;
      if (count == 3)
        break;
    }
  }
}

void M_UnbindCommand(const char *command) {
  int j;
  int l;
  char *b;

  l = strlen(command);

  for (j = 0; j < MAX_KEYS; j++) {
    b = keybindings[j];
    if (!b)
      continue;
    if (!strncmp(b, command, l))
      Key_SetBinding(j, NULL);
  }
}

extern qpic_t *pic_up, *pic_down;

extern qpic_t *b_up;
extern qpic_t *b_down;
extern qpic_t *b_left;
extern qpic_t *b_right;
extern qpic_t *b_lthumb;
extern qpic_t *b_rthumb;
extern qpic_t *b_lshoulder;
extern qpic_t *b_rshoulder;
extern qpic_t *b_abutton;
extern qpic_t *b_bbutton;
extern qpic_t *b_ybutton;
extern qpic_t *b_xbutton;
extern qpic_t *b_lt;
extern qpic_t *b_rt;

void M_Keys_Draw(void) {
#ifdef VITA
  int y = 0;
#else
  int y = vid.height * 0.5;
#endif
  char *b;

  // Menu Background
  if (paused_hack == false)
    Draw_BgMenu();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Header
  DRAW_HEADER("CONTROLS");

  if (bind_grab) {
#ifndef VITA
    Draw_ColoredStringScale(
        86, y + 305, "Press a key or button for this action", 1, 1, 1, 1, 1.5f);
#else
    Draw_ColoredStringScale(
        (vid.width / 2 - strlen("Press a key or button for this action") * 8),
        y + 475, "Press a key or button for this action", 1, 1, 1, 1, 2.0f);
#endif // VITA
  } else {
#ifndef VITA
    {
      float sc = 1.5f;
      int gx = 150, gy = y + 305, gs = 18;
      Draw_ColoredStringScale(gx, gy, "Press ", 1, 1, 1, 1, sc);
      gx += getTextWidth("Press ", sc);
      Draw_StretchPic(gx, gy - 1, b_abutton, gs, gs);
      gx += gs + 2;
      Draw_ColoredStringScale(gx, gy, " to change, ", 1, 1, 1, 1, sc);
      gx += getTextWidth(" to change, ", sc);
      Draw_StretchPic(gx, gy - 1, b_ybutton, gs, gs);
      gx += gs + 2;
      Draw_ColoredStringScale(gx, gy, " to clear", 1, 1, 1, 1, sc);
    }
#else
    Draw_ColoredStringScale(
        (vid.width / 2 - strlen("Press   to change,   to clear") * 8), y + 475,
        "Press   to change,   to clear", 1, 1, 1, 1, 2.0f);
    Draw_StretchPic(340, y + 468, b_abutton, 28, 28);
    Draw_StretchPic(545, y + 468, b_ybutton, 28, 28);
#endif // VITA
  }

  for (int i = 0; i < (int)NUMCOMMANDS; i++) {
#ifndef VITA
    int y_offset = y + (55 + 15 * (i + 1));
    if (i == keys_cursor) {
      Draw_ColoredStringScale(10, y_offset, bindnames[i][1], 1, 0, 0, 1, 1.5f);
    } else {
      Draw_ColoredStringScale(10, y_offset, bindnames[i][1], 1, 1, 1, 1, 1.5f);
    }
#else
    int y_offset = y + (70 + 30 * (i + 1));
    if (i == keys_cursor) {
      Draw_ColoredStringScale(10, y_offset, bindnames[i][1], 1, 0, 0, 1, 2.0f);
    } else {
      Draw_ColoredStringScale(10, y_offset, bindnames[i][1], 1, 1, 1, 1, 2.0f);
    }
#endif // VITA

    for (int j = 0; j < 256; j++) {
      b = keybindings[j];

      if (!b) {
        continue;
      }

      if (!strcmp(b, bindnames[i][0])) {
        qpic_t *btn_to_draw = GetButtonIcon(bindnames[i][0]);
#ifndef VITA
        if (!strcmp("LSHOULDER", bindnames[i][0]) ||
            !strcmp("RSHOULDER", bindnames[i][0]))
          Draw_StretchPic(300, y_offset + 4, btn_to_draw, 16, 8);
        else
          Draw_StretchPic(300, y_offset, btn_to_draw, 16, 16);
#else
        if (!strcmp("LSHOULDER", bindnames[i][0]) ||
            !strcmp("RSHOULDER", bindnames[i][0]))
          Draw_StretchPic(400, y_offset + 4, btn_to_draw, 28, 14);
        else
          Draw_StretchPic(400, y_offset, btn_to_draw, 28, 28);
#endif // VITA
        break;
      }
    }
  }

  DRAW_BACKBUTTON(NUMCOMMANDS, keys_cursor);

  // Cursor Flashing
  if (keys_cursor != NUMCOMMANDS) {
#ifndef VITA
    M_DrawCharacter(282, y + 58 + (keys_cursor + 1) * 15,
                    12 + ((int)(realtime * 4) & 1));
#else
    Draw_CharacterScale(375, y + 74 + (keys_cursor + 1) * 30,
                        12 + ((int)(realtime * 4) & 1), 2.0f);
#endif // VITA
  }
}

void M_Keys_Key(int k) {
  char cmd[80];
  int keys[3];

  if (bind_grab) { // defining a key
    S_LocalSound("sounds/menu/navigate.wav");
    if ((k != K_ESCAPE) && (k != '`')) {
      sprintf(cmd, "bind \"%s\" \"%s\"\n", Key_KeynumToString(k),
              bindnames[keys_cursor][0]);
      Cbuf_InsertText(cmd);
    }

    bind_grab = false;
    IN_Deactivate(
        modestate ==
        MS_WINDOWED); // deactivate because we're returning to the menu
    return;
  }

  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Options_f();
    break;

  case K_LEFTARROW:
  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    keys_cursor--;
    if (keys_cursor < 0)
      keys_cursor = NUMCOMMANDS;
    break;

  case K_DOWNARROW:
  case K_RIGHTARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    keys_cursor++;
    if (keys_cursor >= (int)NUMCOMMANDS + 1)
      keys_cursor = 0;
    break;

  case K_ENTER: // go into bind mode
  case K_KP_ENTER:
  case K_ABUTTON:
    if (keys_cursor == NUMCOMMANDS)
      M_Menu_Options_f();
    else {
      M_FindKeysForCommand(bindnames[keys_cursor][0], keys);
      S_LocalSound("sounds/menu/navigate.wav");
      if (keys[2] != -1)
        M_UnbindCommand(bindnames[keys_cursor][0]);
      bind_grab = true;
      IN_Activate(); // activate to allow mouse key binding
    }
    break;

  case K_BACKSPACE: // delete bindings
  case K_DEL:
  case K_YBUTTON:
    S_LocalSound("sounds/menu/navigate.wav");
    M_UnbindCommand(bindnames[keys_cursor][0]);
    break;
  }
}

//=============================================================================
/* CONSOLE OSK */

#define CHAR_SIZE 8
#define MAX_Y 8
#define MAX_X 12

#define MAX_CHAR_LINE 36
#define MAX_CHAR 72

int osk_pos_x = 0;
int osk_pos_y = 0;
int max_len = 0;
int m_old_state = 0;

char *osk_out_buff = NULL;
char osk_buffer[128];

char *osk_help[] = {"CONFIRM: ", "    Y    ", "CANCEL:  ",
                    "    B    ", "DELETE:  ", "    X    ",
                    "ADD CHAR:", "    A    ", ""};

char *osk_text[] = {
    " 1 2 3 4 5 6 7 8 9 0 - = ` ",  " q w e r t y u i o p [ ]   ",
    "   a s d f g h j k l ; ' \\ ", "     z x c v b n m   , . / ",
    "                           ",  " ! @ # $ % ^ & * ( ) _ + ~ ",
    " Q W E R T Y U I O P { }   ",  "   A S D F G H J K L : \" | ",
    "     Z X C V B N M   < > ? "};

void M_OSK_Draw(void) {

  int x, y;
  int i;

  char *selected_line = osk_text[osk_pos_y];
  char selected_char[2];

  GL_SetCanvas(CANVAS_MENU);

  selected_char[0] = selected_line[1 + (2 * osk_pos_x)];
  selected_char[1] = '\0';
  if (selected_char[0] == ' ' || selected_char[0] == '\t')
    selected_char[0] = 'X';

  y = 20;
  x = 16;

  M_DrawTextBox(10, 10, 26, 10);
  M_DrawTextBox(10 + (26 * CHAR_SIZE), 10, 10, 10);
  M_DrawTextBox(10, 10 + (10 * CHAR_SIZE), 36, 3);

  for (i = 0; i <= MAX_Y; i++) {
    M_PrintWhite(x, y + (CHAR_SIZE * i), osk_text[i]);
    if (i % 2 == 0)
      M_Print(x + (27 * CHAR_SIZE), y + (CHAR_SIZE * i), osk_help[i]);
    else
      M_PrintWhite(x + (27 * CHAR_SIZE), y + (CHAR_SIZE * i), osk_help[i]);
  }

  int text_len = strlen(osk_buffer);
  if (text_len > MAX_CHAR_LINE) {

    char oneline[MAX_CHAR_LINE + 1];
    strncpy(oneline, osk_buffer, MAX_CHAR_LINE);
    oneline[MAX_CHAR_LINE] = '\0';

    M_Print(x + 4, y + 4 + (CHAR_SIZE * (MAX_Y + 2)), oneline);

    strncpy(oneline, osk_buffer + MAX_CHAR_LINE, text_len - MAX_CHAR_LINE);
    oneline[text_len - MAX_CHAR_LINE] = '\0';

    M_Print(x + 4, y + 4 + (CHAR_SIZE * (MAX_Y + 3)), oneline);
    M_PrintWhite(x + 4 + (CHAR_SIZE * (text_len - MAX_CHAR_LINE)),
                 y + 4 + (CHAR_SIZE * (MAX_Y + 3)), "_");
  } else {
    M_Print(x + 4, y + 4 + (CHAR_SIZE * (MAX_Y + 2)), osk_buffer);
    M_PrintWhite(x + 4 + (CHAR_SIZE * (text_len)),
                 y + 4 + (CHAR_SIZE * (MAX_Y + 2)), "_");
  }
  M_Print(x + ((((osk_pos_x) * 2) + 1) * CHAR_SIZE),
          y + (osk_pos_y * CHAR_SIZE), selected_char);
}

// reusable on-screen keyboard for menu text fields (name/IP/port), so they're
// typable on a controller. Open with Y; A adds char, X backspaces, Y confirms,
// B cancels, dpad moves.
qboolean menu_osk_active = false;
static char *menu_osk_target = NULL;
static int menu_osk_maxlen = 0;
static qboolean menu_osk_numeric = false;

void M_OSK_Open(char *target, int maxlen, qboolean numeric) {
  menu_osk_target = target;
  menu_osk_maxlen = (maxlen < 126) ? maxlen : 126;
  menu_osk_numeric = numeric;
  menu_osk_active = true;
  osk_pos_x = 0;
  osk_pos_y = 0;
  strncpy(osk_buffer, target, menu_osk_maxlen);
  osk_buffer[menu_osk_maxlen] = '\0';
  S_LocalSound("sounds/menu/navigate.wav");
}

void M_OSK_Keydown(int key) {
  char *selected_line;
  char c;
  int l;

  switch (key) {
  case K_ABUTTON: // add the highlighted character
    selected_line = osk_text[osk_pos_y];
    c = selected_line[1 + (2 * osk_pos_x)];
    if (c == '\t')
      c = ' ';
    if (menu_osk_numeric && (c < '0' || c > '9'))
      break;
    l = strlen(osk_buffer);
    if (l < menu_osk_maxlen) {
      osk_buffer[l] = c;
      osk_buffer[l + 1] = '\0';
    }
    break;

  case K_XBUTTON: // backspace
  case K_BACKSPACE:
    l = strlen(osk_buffer);
    if (l > 0)
      osk_buffer[l - 1] = '\0';
    break;

  case K_YBUTTON: // confirm -> write back to the field
  case K_ENTER:
  case K_KP_ENTER:
    if (menu_osk_target) {
      strncpy(menu_osk_target, osk_buffer, menu_osk_maxlen);
      menu_osk_target[menu_osk_maxlen] = '\0';
    }
    menu_osk_active = false;
    menu_osk_target = NULL;
    S_LocalSound("sounds/menu/navigate.wav");
    break;

  case K_BBUTTON: // cancel -> discard
  case K_ESCAPE:
    menu_osk_active = false;
    menu_osk_target = NULL;
    break;

  case K_RIGHTARROW:
    if (++osk_pos_x > MAX_X)
      osk_pos_x = 0;
    break;
  case K_LEFTARROW:
    if (--osk_pos_x < 0)
      osk_pos_x = MAX_X;
    break;
  case K_DOWNARROW:
    if (++osk_pos_y > MAX_Y)
      osk_pos_y = 0;
    break;
  case K_UPARROW:
    if (--osk_pos_y < 0)
      osk_pos_y = MAX_Y;
    break;
  }
}

//=============================================================================
/* VIDEO MENU */

void M_Menu_Video_f(void) {
  (*vid_menucmdfn)(); // johnfitz
}

void M_Video_Draw(void) { (*vid_menudrawfn)(); }

void M_Video_Key(int key) { (*vid_menukeyfn)(key); }

//=============================================================================
/* HELP MENU */

int help_page;
#define NUM_CREDITS_PAGES 1

void M_Menu_Credits_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  old_m_state = m_state;
  m_state = m_credits;
  m_entersound = true;
  help_page = 0;
}

void M_Credits_Draw(void) {
  MENU_INITVARS();

  // Menu Background
  Draw_BgMenu();

  // Fill black to make everything easier to see
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Version String
  DRAW_VERSIONSTRING();

  // Header
  DRAW_HEADER("CREDITS");

  DRAW_CREDITLINE(0, "Programming:");
  DRAW_CREDITLINE(1, "Blubswillrule, Jukki, DR_Mabuse1981, Naievil, Cypress");
  DRAW_CREDITLINE(2, "ScatterBox");
  DRAW_CREDITLINE(3, "");
  DRAW_CREDITLINE(4, "Models:");
  DRAW_CREDITLINE(5, "Blubswillrule, Ju[s]tice, Derped_Crusader");
  DRAW_CREDITLINE(6, "");
  DRAW_CREDITLINE(7, "GFX:");
  DRAW_CREDITLINE(8, "Blubswillrule, Ju[s]tice, Cypress, Derped_Crusader");
  DRAW_CREDITLINE(9, "");
  DRAW_CREDITLINE(10, "Sounds/Music:");
  DRAW_CREDITLINE(11, "Blubswillrule, Biodude, Cypress, Marty P.");
  DRAW_CREDITLINE(12, "");
  DRAW_CREDITLINE(13, "Special Thanks:");
  DRAW_CREDITLINE(14, "- Spike, Eukara: [FTEQW]");
  DRAW_CREDITLINE(15, "- Shpuld: [CleanQC4FTE]");
  DRAW_CREDITLINE(16, "- Crow_Bar, st1x51: [dQuake(plus)]");
  DRAW_CREDITLINE(17, "- fgsfdsfgs: [Quakespasm-NX]");
  DRAW_CREDITLINE(18,
                  "- Rinnegatamante: [Initial VITA Port, VITA Auto-Updater]");
  DRAW_CREDITLINE(19, "- Azenn: [GFX Help]");
  DRAW_CREDITLINE(20, "- BCDeshiG: [Extensive Testing]");

  DRAW_BACKBUTTON(0, 0);
}

void M_Credits_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_ENTER:
  case K_BBUTTON:
  case K_KP_ENTER:
  case K_ABUTTON:
    M_Menu_Main_f();
    break;

  /*case K_UPARROW:
  case K_RIGHTARROW:
          m_entersound = true;
          if (++help_page >= NUM_CREDITS_PAGES)
                  help_page = 0;
          break;

  case K_DOWNARROW:
  case K_LEFTARROW:
          m_entersound = true;
          if (--help_page < 0)
                  help_page = NUM_CREDITS_PAGES-1;
          break;*/
  default:
    break;
  }
}

//=============================================================================
/* GAMEMODES MENU */

int gamemodes_cursor;
extern cvar_t sv_gamemode;
extern cvar_t sv_difficulty;
extern cvar_t sv_random_mix;
extern cvar_t sv_gamemode_mix;

// MIX sub-screen: combine compatible twists.
static int mix_screen = 0;
static int mix_cursor = 0;
#define MIX_ITEMS 6 // Cranked, Hardpoint, Random, Festive, Apply, Back
#define MIX_CRANKED 1
#define MIX_HARDPOINT 2
#define MIX_RANDOM 4
#define MIX_FESTIVE 8

const char *M_MixComboName(int mask) {
  // hand-picked names for the fun combos
  switch (mask) {
  case MIX_CRANKED:
    return "Cranked";
  case MIX_HARDPOINT:
    return "Hardpoint";
  case MIX_RANDOM:
    return "Random";
  case MIX_FESTIVE:
    return "Festive";
  case MIX_CRANKED | MIX_HARDPOINT:
    return "Crankedpoint";
  case MIX_HARDPOINT | MIX_RANDOM:
    return "Randompoint";
  case MIX_CRANKED | MIX_RANDOM:
    return "Crandom";
  case MIX_CRANKED | MIX_HARDPOINT | MIX_RANDOM:
    return "Crandompoint";
  case MIX_FESTIVE | MIX_HARDPOINT:
    return "Festipoint";
  case MIX_FESTIVE | MIX_CRANKED:
    return "Festicranked";
  case MIX_FESTIVE | MIX_RANDOM:
    return "Festidom";
  }
  // fallback: join the active mode names (scales as more modes are added)
  static char buf[128];
  buf[0] = 0;
  if (mask & MIX_CRANKED) {
    if (buf[0])
      strcat(buf, " + ");
    strcat(buf, "Cranked");
  }
  if (mask & MIX_HARDPOINT) {
    if (buf[0])
      strcat(buf, " + ");
    strcat(buf, "Hardpoint");
  }
  if (mask & MIX_RANDOM) {
    if (buf[0])
      strcat(buf, " + ");
    strcat(buf, "Random");
  }
  if (mask & MIX_FESTIVE) {
    if (buf[0])
      strcat(buf, " + ");
    strcat(buf, "Festive");
  }
  return buf[0] ? buf : "Nothing";
}

#define GAMEMODES_ITEMS 12

// Which base modes the "Random" RNG twist can layer on (mirror of QC's
// Gamemode_AllowsRandom): Classic(0), Hardcore(3), Festive(6), Hardpoint(8).
static qboolean M_Gamemode_AllowsRandom(int mode) {
  return (mode == 0 || mode == 3 || mode == 6 || mode == 8);
}

void M_Menu_Gamemodes_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  old_m_state = m_state;
  m_state = m_gamemodes;
  m_entersound = true;
}

const char *M_Gamemodes_GetModeName(int mode) {
  switch (mode) {
  case 0:
    return "Classic";
  case 2:
    return "Gun Game";
  case 3:
    return "Hardcore";
  case 4:
    return "Wild West";
  case 5:
    return "Sticks and Stones";
  case 6:
    return "Festive";
  case 7:
    return "Random";
  case 8:
    return "Hardpoint";
  case 9:
    return "Cranked";
  case 10:
    return "Skull Ball";
  default:
    return "Classic";
  }
}

void M_Gamemodes_Draw(void) {
  MENU_INITVARS();

  // Menu Background
  Draw_BgMenu();

  // Fill black for visibility
  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  // Version String
  DRAW_VERSIONSTRING();

  if (mix_screen) {
    int mask = (int)sv_gamemode_mix.value;
    DRAW_HEADER("MIX");
    DRAW_MENUOPTION(0, "Cranked", mix_cursor, false);
    DRAW_MENUOPTION(1, "Hardpoint", mix_cursor, false);
    DRAW_MENUOPTION(2, "Random", mix_cursor, false);
    DRAW_MENUOPTION(3, "Festive", mix_cursor, false);
    DRAW_MENUOPTION(4, "Apply", mix_cursor, false);
    DRAW_MENUOPTION(5, "Back", mix_cursor, false);
    DRAW_SETTINGSVALUE(0, (mask & MIX_CRANKED) ? "On" : "Off");
    DRAW_SETTINGSVALUE(1, (mask & MIX_HARDPOINT) ? "On" : "Off");
    DRAW_SETTINGSVALUE(2, (mask & MIX_RANDOM) ? "On" : "Off");
    DRAW_SETTINGSVALUE(3, (mask & MIX_FESTIVE) ? "On" : "Off");
    // result line below the list (clear of the options)
    Draw_ColoredStringScale(10, y + 190, va("Result: %s", M_MixComboName(mask)),
                            1, 1, 0, 1, 2.0f);
    return;
  }

  // Header
  DRAW_HEADER("GAMEMODES");

  // Show current mode (append "+ Random" when the mix is active on a compatible
  // base)
  char current_mode[96];
  int basemode = (int)sv_gamemode.value;
  if (sv_random_mix.value != 0 && M_Gamemode_AllowsRandom(basemode) &&
      basemode != 7)
    sprintf(current_mode, "Current: %s + Random",
            M_Gamemodes_GetModeName(basemode));
  else
    sprintf(current_mode, "Current: %s", M_Gamemodes_GetModeName(basemode));
  Draw_ColoredStringScale(100, y + 75, current_mode, 1, 1, 0, 1, 1.5f);

  DRAW_MENUOPTION(0, "Classic", gamemodes_cursor, false);
  DRAW_MENUOPTION(1, "Gun Game", gamemodes_cursor, false);
  DRAW_MENUOPTION(2, "Hardcore", gamemodes_cursor, false);
  DRAW_MENUOPTION(3, "Wild West", gamemodes_cursor, false);
  DRAW_MENUOPTION(4, "Sticks and Stones", gamemodes_cursor, false);
  DRAW_MENUOPTION(5, "Festive", gamemodes_cursor, false);
  DRAW_MENUOPTION(6, "Random", gamemodes_cursor, false);
  DRAW_MENUOPTION(7, "Hardpoint", gamemodes_cursor, false);
  DRAW_MENUOPTION(8, "Cranked", gamemodes_cursor, false);
  DRAW_MENUOPTION(9, "Skull Ball", gamemodes_cursor, false);
  DRAW_MENUOPTION(10, "Mix...", gamemodes_cursor, false);
  DRAW_MENUOPTION(11, "Back", gamemodes_cursor, false);

  // show the active mix combo (if any) next to Mix...
  if ((int)sv_gamemode_mix.value != 0)
    DRAW_SETTINGSVALUE(10, M_MixComboName((int)sv_gamemode_mix.value));

  switch (gamemodes_cursor) {
  case 0:
    DRAW_DESCRIPTION("Standard Zombies survival mode.");
    break;
  case 1:
    DRAW_DESCRIPTION("Progress through weapons by getting kills.");
    break;
  case 2:
    DRAW_DESCRIPTION("Increased difficulty with tougher zombies.");
    break;
  case 3:
    DRAW_DESCRIPTION("Western-themed weapon loadout.");
    break;
  case 4:
    DRAW_DESCRIPTION("Ballistic Knife and Crossbow only.");
    break;
  case 5:
    DRAW_DESCRIPTION("Holiday-themed mode.");
    break;
  case 6:
    DRAW_DESCRIPTION("RNG: random start gun, wall-buys and perks.");
    break;
  case 7:
    DRAW_DESCRIPTION("Earn points only inside the moving hardpoint zone.");
    break;
  case 8:
    DRAW_DESCRIPTION(
        "Kill fast to stay Cranked. Run out of time and you bleed.");
    break;
  case 9:
    DRAW_DESCRIPTION("Carry a random skull to the pedestal to score points.");
    break;
  case 10:
    DRAW_DESCRIPTION("Open the Mix screen to combine gamemodes.");
    break;
  case 11:
    DRAW_DESCRIPTION("Return to Main Menu.");
    break;
  default:
    break;
  }
}

void M_Gamemodes_Key(int key) {
  if (mix_screen) {
    switch (key) {
    case K_ESCAPE:
    case K_BBUTTON:
      mix_screen = 0;
      break;
    case K_DOWNARROW:
      S_LocalSound("sounds/menu/navigate.wav");
      if (++mix_cursor >= MIX_ITEMS)
        mix_cursor = 0;
      break;
    case K_UPARROW:
      S_LocalSound("sounds/menu/navigate.wav");
      if (--mix_cursor < 0)
        mix_cursor = MIX_ITEMS - 1;
      break;
    case K_ENTER:
    case K_KP_ENTER:
    case K_ABUTTON:
      m_entersound = true;
      {
        int mask = (int)sv_gamemode_mix.value;
        if (mix_cursor == 0)
          mask ^= MIX_CRANKED;
        else if (mix_cursor == 1)
          mask ^= MIX_HARDPOINT;
        else if (mix_cursor == 2)
          mask ^= MIX_RANDOM;
        else if (mix_cursor == 3)
          mask ^= MIX_FESTIVE;
        else if (mix_cursor == 4) {        // Apply
          Cvar_SetValue("sv_gamemode", 0); // Classic base
          Cvar_SetValue("sv_gamemode_mix", mask);
          mix_screen = 0;
          break;
        } else {
          mix_screen = 0;
          break;
        } // Back
        Cvar_SetValue("sv_gamemode_mix", mask);
        if (mask)
          Cvar_SetValue("sv_gamemode", 0);
      }
      break;
    }
    return;
  }

  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (++gamemodes_cursor >= GAMEMODES_ITEMS)
      gamemodes_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    if (--gamemodes_cursor < 0)
      gamemodes_cursor = GAMEMODES_ITEMS - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;
    // picking any single mode clears an active mix
    if (gamemodes_cursor <= 9)
      Cvar_SetValue("sv_gamemode_mix", 0);
    switch (gamemodes_cursor) {
    case 0:
      Cvar_SetValue("sv_gamemode", 0);
      break; // Classic
    case 1:
      Cvar_SetValue("sv_gamemode", 2);
      break; // Gun Game
    case 2:
      Cvar_SetValue("sv_gamemode", 3);
      break; // Hardcore
    case 3:
      Cvar_SetValue("sv_gamemode", 4);
      break; // Wild West
    case 4:
      Cvar_SetValue("sv_gamemode", 5);
      break; // Sticks and Stones
    case 5:
      Cvar_SetValue("sv_gamemode", 6);
      break; // Festive
    case 6:
      Cvar_SetValue("sv_gamemode", 7);
      break; // Random
    case 7:
      Cvar_SetValue("sv_gamemode", 8);
      break; // Hardpoint
    case 8:
      Cvar_SetValue("sv_gamemode", 9);
      break; // Cranked
    case 9:
      Cvar_SetValue("sv_gamemode", 10);
      break; // Skull Ball
    case 10:
      mix_screen = 1;
      mix_cursor = 0;
      break; // Mix... (sub-screen)
    case 11:
      M_Menu_Main_f();
      break; // Back
    }
    break;
  }
}

//=============================================================================
/* QUIT MENU */

int msgNumber;
enum m_state_e m_quit_prevstate;

void M_Menu_Quit_f(void) {
  if (m_state == m_quit)
    return;
  wasInMenus = (key_dest == key_menu);
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_quit_prevstate = m_state;
  old_m_state = m_state;
  m_state = m_quit;
  m_entersound = true;
  msgNumber = rand() & 7;
}

void M_Quit_Key(int key) {
  if (key == K_ESCAPE) {
    if (wasInMenus) {
      m_state = m_quit_prevstate;
      m_entersound = true;
    } else {
      IN_Activate();
      key_dest = key_game;
      m_state = m_none;
    }
  }
}

void M_Quit_Char(int key) {
  switch (key) {
  case 'n':
  case 'N':
    if (wasInMenus) {
      m_state = m_quit_prevstate;
      m_entersound = true;
    } else {
      IN_Activate();
      key_dest = key_game;
      m_state = m_none;
    }
    break;

  case 'y':
  case 'Y':
    IN_Deactivate(modestate == MS_WINDOWED);
    key_dest = key_console;
    Host_Quit_f();
    break;

  default:
    break;
  }
}

qboolean M_Quit_TextEntry(void) { return true; }

void M_Quit_Draw(void) // johnfitz -- modified for new quit message
{
  char msg1[] = "Nazi Zombies Portable";
  char msg2[] = "by NZP Dev Team"; /* msg2/msg3 are mostly [40] */
  char msg3[] = "Press a to quit";
  int boxlen;

  if (wasInMenus) {
    m_state = m_quit_prevstate;
    m_recursiveDraw = true;
    M_Draw();
    m_state = m_quit;
  }

  // okay, this is kind of fucked up.  M_DrawTextBox will always act as if
  // width is even. Also, the width and lines values are for the interior of the
  // box, but the x and y values include the border.
  boxlen =
      q_max(strlen(msg1), q_max((sizeof(msg2) - 1), (sizeof(msg3) - 1))) + 1;
  if (boxlen & 1)
    boxlen++;
  M_DrawTextBox(160 - 4 * (boxlen + 2), 76, boxlen, 4);

  // now do the text
  M_Print(160 - 4 * strlen(msg1), 88, msg1);
  M_Print(160 - 4 * (sizeof(msg2) - 1), 96, msg2);
  M_PrintWhite(160 - 4 * (sizeof(msg3) - 1), 104, msg3);
}

//=============================================================================
/* LAN CONFIG MENU */

int lanConfig_cursor = -1;
int lanConfig_cursor_table[] = {72, 92, 124};
#define NUM_LANCONFIG_CMDS 3

int lanConfig_port;
char lanConfig_portname[6];
char lanConfig_joinname[22];

void M_Menu_LanConfig_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_lanconfig;
  m_entersound = true;
  if (lanConfig_cursor == -1) {
    if (JoiningGame && TCPIPConfig)
      lanConfig_cursor = 2;
    else
      lanConfig_cursor = 1;
  }
  if (StartingGame && lanConfig_cursor == 2)
    lanConfig_cursor = 1;
  lanConfig_port = DEFAULTnet_hostport;
  sprintf(lanConfig_portname, "%u", lanConfig_port);

  m_return_onerror = false;
  m_return_reason[0] = 0;
}

void M_LanConfig_Draw(void) {
  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  if (StartingGame) {
    DRAW_HEADER("HOST GAME");
  } else {
    DRAW_HEADER("JOIN GAME");
  }

  const char *addr = TCPIPConfig ? my_tcpip_address : my_ipx_address;
  DRAW_MENUOPTION(99, va("Your Address: %s", addr), -1, true);

  DRAW_MENUOPTION(
      0,
      va("Port:         %s%s", lanConfig_portname,
         (lanConfig_cursor == 0 && ((int)(realtime * 2) & 1)) ? "_" : ""),
      lanConfig_cursor, false);

  if (StartingGame) {
    DRAW_MENUOPTION(1, "Start Host Lobby", lanConfig_cursor, false);
  } else {
    DRAW_MENUOPTION(1, "Search Local Network", lanConfig_cursor, false);
    DRAW_MENUOPTION(
        2,
        va("Join Address: %s%s", lanConfig_joinname,
           (lanConfig_cursor == 2 && ((int)(realtime * 2) & 1)) ? "_" : ""),
        lanConfig_cursor, false);
  }

  if (*m_return_reason) {
    Draw_ColoredStringScale(10, menu_offset_y + 40, m_return_reason, 1, 0, 0, 1,
                            1.5f);
  }

  if (lanConfig_cursor == 0) {
    DRAW_DESCRIPTION("Press Y or A to type the port number.");
  } else if (lanConfig_cursor == 1) {
    if (StartingGame) {
      DRAW_DESCRIPTION("Start server and wait for players.");
    } else {
      DRAW_DESCRIPTION("Search for local games on the network.");
    }
  } else if (lanConfig_cursor == 2) {
    DRAW_DESCRIPTION("Press Y to type host IP, A to connect.");
  }
}

void M_LanConfig_Key(int key) {
  int l;

  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_MultiPlayer_f();
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    lanConfig_cursor--;
    if (lanConfig_cursor < 0)
      lanConfig_cursor = NUM_LANCONFIG_CMDS - 1;
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    lanConfig_cursor++;
    if (lanConfig_cursor >= NUM_LANCONFIG_CMDS)
      lanConfig_cursor = 0;
    break;

  case K_YBUTTON:
    // open the on-screen keyboard for the text fields
    if (lanConfig_cursor == 0)
      M_OSK_Open(lanConfig_portname, 5, true);
    else if (lanConfig_cursor == 2)
      M_OSK_Open(lanConfig_joinname, 21, false);
    return;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    if (lanConfig_cursor == 0) {
      // Port field: A opens the on-screen keyboard to edit it.
      M_OSK_Open(lanConfig_portname, 5, true);
      return;
    }

    m_entersound = true;

    M_ConfigureNetSubsystem();

    if (lanConfig_cursor == 1) {
      if (StartingGame) {
        M_Menu_GameOptions_f();
        break;
      }
      M_Menu_Search_f();
      break;
    }

    if (lanConfig_cursor == 2) {
      m_return_state = m_state;
      m_return_onerror = true;
      IN_Activate();
      key_dest = key_game;
      m_state = m_none;
      Cbuf_AddText(va("connect \"%s\"\n", lanConfig_joinname));
      break;
    }

    break;

  case K_BACKSPACE:
    if (lanConfig_cursor == 0) {
      if (strlen(lanConfig_portname))
        lanConfig_portname[strlen(lanConfig_portname) - 1] = 0;
    }

    if (lanConfig_cursor == 2) {
      if (strlen(lanConfig_joinname))
        lanConfig_joinname[strlen(lanConfig_joinname) - 1] = 0;
    }
    break;
  }

  if (StartingGame && lanConfig_cursor == 2) {
    if (key == K_UPARROW)
      lanConfig_cursor = 1;
    else
      lanConfig_cursor = 0;
  }

  l = Q_atoi(lanConfig_portname);
  if (l > 65535)
    l = lanConfig_port;
  else
    lanConfig_port = l;
  sprintf(lanConfig_portname, "%u", lanConfig_port);
}

void M_LanConfig_Char(int key) {
  int l;

  switch (lanConfig_cursor) {
  case 0:
    if (key < '0' || key > '9')
      return;
    l = strlen(lanConfig_portname);
    if (l < 5) {
      lanConfig_portname[l + 1] = 0;
      lanConfig_portname[l] = key;
    }
    break;
  case 2:
    l = strlen(lanConfig_joinname);
    if (l < 21) {
      lanConfig_joinname[l + 1] = 0;
      lanConfig_joinname[l] = key;
    }
    break;
  }
}

qboolean M_LanConfig_TextEntry(void) {
  return (lanConfig_cursor == 0 || lanConfig_cursor == 2);
}

//=============================================================================
/* GAME OPTIONS MENU */

typedef struct {
  const char *name;
  const char *description;
} level_t;

level_t levels[] = {{"start", "Entrance"}, // 0

                    {"e1m1", "Slipgate Complex"}, // 1
                    {"e1m2", "Castle of the Damned"},
                    {"e1m3", "The Necropolis"},
                    {"e1m4", "The Grisly Grotto"},
                    {"e1m5", "Gloom Keep"},
                    {"e1m6", "The Door To Chthon"},
                    {"e1m7", "The House of Chthon"},
                    {"e1m8", "Ziggurat Vertigo"},

                    {"e2m1", "The Installation"}, // 9
                    {"e2m2", "Ogre Citadel"},
                    {"e2m3", "Crypt of Decay"},
                    {"e2m4", "The Ebon Fortress"},
                    {"e2m5", "The Wizard's Manse"},
                    {"e2m6", "The Dismal Oubliette"},
                    {"e2m7", "Underearth"},

                    {"e3m1", "Termination Central"}, // 16
                    {"e3m2", "The Vaults of Zin"},
                    {"e3m3", "The Tomb of Terror"},
                    {"e3m4", "Satan's Dark Delight"},
                    {"e3m5", "Wind Tunnels"},
                    {"e3m6", "Chambers of Torment"},
                    {"e3m7", "The Haunted Halls"},

                    {"e4m1", "The Sewage System"}, // 23
                    {"e4m2", "The Tower of Despair"},
                    {"e4m3", "The Elder God Shrine"},
                    {"e4m4", "The Palace of Hate"},
                    {"e4m5", "Hell's Atrium"},
                    {"e4m6", "The Pain Maze"},
                    {"e4m7", "Azure Agony"},
                    {"e4m8", "The Nameless City"},

                    {"end", "Shub-Niggurath's Pit"}, // 31

                    {"dm1", "Place of Two Deaths"}, // 32
                    {"dm2", "Claustrophobopolis"},
                    {"dm3", "The Abandoned Base"},
                    {"dm4", "The Bad Place"},
                    {"dm5", "The Cistern"},
                    {"dm6", "The Dark Zone"}};

// MED 01/06/97 added hipnotic levels
level_t hipnoticlevels[] = {
    {"start", "Command HQ"}, // 0

    {"hip1m1", "The Pumping Station"}, // 1
    {"hip1m2", "Storage Facility"},
    {"hip1m3", "The Lost Mine"},
    {"hip1m4", "Research Facility"},
    {"hip1m5", "Military Complex"},

    {"hip2m1", "Ancient Realms"}, // 6
    {"hip2m2", "The Black Cathedral"},
    {"hip2m3", "The Catacombs"},
    {"hip2m4", "The Crypt"},
    {"hip2m5", "Mortum's Keep"},
    {"hip2m6", "The Gremlin's Domain"},

    {"hip3m1", "Tur Torment"}, // 12
    {"hip3m2", "Pandemonium"},
    {"hip3m3", "Limbo"},
    {"hip3m4", "The Gauntlet"},

    {"hipend", "Armagon's Lair"}, // 16

    {"hipdm1", "The Edge of Oblivion"} // 17
};

// PGM 01/07/97 added rogue levels
// PGM 03/02/97 added dmatch level
level_t roguelevels[] = {
    {"start", "Split Decision"},   {"r1m1", "Deviant's Domain"},
    {"r1m2", "Dread Portal"},      {"r1m3", "Judgement Call"},
    {"r1m4", "Cave of Death"},     {"r1m5", "Towers of Wrath"},
    {"r1m6", "Temple of Pain"},    {"r1m7", "Tomb of the Overlord"},
    {"r2m1", "Tempus Fugit"},      {"r2m2", "Elemental Fury I"},
    {"r2m3", "Elemental Fury II"}, {"r2m4", "Curse of Osiris"},
    {"r2m5", "Wizard's Keep"},     {"r2m6", "Blood Sacrifice"},
    {"r2m7", "Last Bastion"},      {"r2m8", "Source of Evil"},
    {"ctf1", "Division of Change"}};

typedef struct {
  const char *description;
  int firstLevel;
  int levels;
} episode_t;

episode_t episodes[] = {
    {"Welcome to Quake", 0, 1},     {"Doomed Dimension", 1, 8},
    {"Realm of Black Magic", 9, 7}, {"Netherworld", 16, 7},
    {"The Elder World", 23, 8},     {"Final Level", 31, 1},
    {"Deathmatch Arena", 32, 6}};

// MED 01/06/97  added hipnotic episodes
episode_t hipnoticepisodes[] = {
    {"Scourge of Armagon", 0, 1},   {"Fortress of the Dead", 1, 5},
    {"Dominion of Darkness", 6, 6}, {"The Rift", 12, 4},
    {"Final Level", 16, 1},         {"Deathmatch Arena", 17, 1}};

// PGM 01/07/97 added rogue episodes
// PGM 03/02/97 added dmatch episode
episode_t rogueepisodes[] = {{"Introduction", 0, 1},
                             {"Hell's Fortress", 1, 7},
                             {"Corridors of Time", 8, 8},
                             {"Deathmatch Arena", 16, 1}};

int startepisode;
int startlevel;
int maxplayers;
int start_map_index = 0; // Selected map index for multiplayer
qboolean m_serverInfoMessage = false;
double m_serverInfoMessageTime;

// Get total number of available maps (base + custom)
int M_GetTotalMapCount(void) {
  int count = BASE_MAP_COUNT;
  for (int i = 0; i < 50; i++) {
    if (custom_maps[i].occupied)
      count++;
  }
  return count;
}

// Get map name at index (base maps first, then custom)
const char *M_GetMapName(int index) {
  if (index < BASE_MAP_COUNT)
    return base_maps[index];
  int custom_index = index - BASE_MAP_COUNT;
  int found = 0;
  for (int i = 0; i < 50; i++) {
    if (custom_maps[i].occupied) {
      if (found == custom_index)
        return custom_maps[i].map_name;
      found++;
    }
  }
  return base_maps[0]; // Fallback
}

// Get map display name at index
const char *M_GetMapDisplayName(int index) {
  if (index < BASE_MAP_COUNT) {
    // Return prettier names for base maps
    if (strcmp(base_maps[index], "ndu") == 0)
      return "Nacht der Untoten";
    if (strcmp(base_maps[index], "nzp_warehouse") == 0)
      return "Warehouse";
    if (strcmp(base_maps[index], "nzp_warehouse2") == 0)
      return "Warehouse 2";
    if (strcmp(base_maps[index], "christmas_special") == 0)
      return "Christmas Special";
    return base_maps[index];
  }
  int custom_index = index - BASE_MAP_COUNT;
  int found = 0;
  for (int i = 0; i < 50; i++) {
    if (custom_maps[i].occupied) {
      if (found == custom_index) {
        if (custom_maps[i].map_name_pretty)
          return custom_maps[i].map_name_pretty;
        return custom_maps[i].map_name;
      }
      found++;
    }
  }
  return "Unknown";
}

void M_Menu_GameOptions_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_gameoptions;
  m_entersound = true;
  if (maxplayers == 0)
    maxplayers = svs.maxclients;
  if (maxplayers < 2)
    maxplayers = svs.maxclientslimit;
}

int gameoptions_cursor_table[] = {40, 56, 64, 72, 80};
#define NUM_GAMEOPTIONS 5
int gameoptions_cursor;

void M_GameOptions_Draw(void) {
  const char *msg = "";

  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  DRAW_HEADER("HOST GAME");

  // Simplified menu - removed Deathmatch/Coop, Teamplay, Frag/Time limits
  // Nazi Zombies is always cooperative mode
  DRAW_MENUOPTION(0, "Begin Game", gameoptions_cursor, false);
  DRAW_MENUOPTION(1, va("Max Players:  %i", maxplayers), gameoptions_cursor,
                  false);

  // difficulty drives sv_difficulty (the cvar QC reads); it used to set "skill"
  // which nothing uses. Mapping: 0=Normal,1=Easy,2=Hard,3=Nightmare.
  if (sv_difficulty.value == 1)
    msg = "Easy";
  else if (sv_difficulty.value == 2)
    msg = "Hard";
  else if (sv_difficulty.value == 3)
    msg = "Nightmare";
  else
    msg = "Normal";
  DRAW_MENUOPTION(2, va("Difficulty:   %s", msg), gameoptions_cursor, false);

  // Map selection
  int total_maps = M_GetTotalMapCount();
  if (start_map_index >= total_maps)
    start_map_index = 0;
  DRAW_MENUOPTION(3,
                  va("Map:          %s", M_GetMapDisplayName(start_map_index)),
                  gameoptions_cursor, true);

  DRAW_MENUOPTION(4, "Back", gameoptions_cursor, false);

  if (m_serverInfoMessage) {
    if ((realtime - m_serverInfoMessageTime) < 5.0) {
      Draw_ColoredStringScale(10, menu_offset_y + 40,
                              "More than 4 players requires", 1, 1, 0, 1, 1.2f);
      Draw_ColoredStringScale(10, menu_offset_y + 55,
                              "using command line parameters.", 1, 1, 0, 1,
                              1.2f);
    } else {
      m_serverInfoMessage = false;
    }
  }
}

void M_NetStart_Change(int dir) {
  int count;
  float f;

  // Simplified menu indices:
  // 0 = Begin Game, 1 = Max Players, 2 = Difficulty, 3 = Map, 4 = Back
  switch (gameoptions_cursor) {
  case 1: // Max Players
    maxplayers += dir;
    if (maxplayers > svs.maxclientslimit) {
      maxplayers = svs.maxclientslimit;
      m_serverInfoMessage = true;
      m_serverInfoMessageTime = realtime;
    }
    if (maxplayers < 2)
      maxplayers = 2;
    break;

  case 2: // Difficulty -- drive sv_difficulty (what the game reads), not skill
    f = sv_difficulty.value + dir;
    if (f > 3)
      f = 0;
    else if (f < 0)
      f = 3;
    Cvar_SetValue("sv_difficulty", f);
    break;

  case 3: // Map selection
    count = M_GetTotalMapCount();
    start_map_index += dir;
    if (start_map_index < 0)
      start_map_index = count - 1;
    if (start_map_index >= count)
      start_map_index = 0;
    break;

    // case 4 = Back button, no left/right action needed
  }
}

void M_GameOptions_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_MultiPlayer_f(); // Go back to Multiplayer menu
    break;

  case K_UPARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    gameoptions_cursor--;
    if (gameoptions_cursor < 0)
      gameoptions_cursor = NUM_GAMEOPTIONS - 1;
    break;

  case K_DOWNARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    gameoptions_cursor++;
    if (gameoptions_cursor >= NUM_GAMEOPTIONS)
      gameoptions_cursor = 0;
    break;

  case K_LEFTARROW:
    // No left/right for Begin Game (0) or Back (4)
    if (gameoptions_cursor == 0 || gameoptions_cursor == 4)
      break;
    S_LocalSound("sounds/menu/navigate.wav");
    M_NetStart_Change(-1);
    break;

  case K_RIGHTARROW:
    // No left/right for Begin Game (0) or Back (4)
    if (gameoptions_cursor == 0 || gameoptions_cursor == 4)
      break;
    S_LocalSound("sounds/menu/navigate.wav");
    M_NetStart_Change(1);
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    S_LocalSound("sounds/menu/navigate.wav");

    // Back button
    if (gameoptions_cursor == 4) {
      M_Menu_MultiPlayer_f();
      return;
    }

    // Begin Game - force cooperative mode for Nazi Zombies
    if (gameoptions_cursor == 0) {
      if (sv.active)
        Cbuf_AddText("disconnect\n");

      // Configure network subsystem (sets port, stops demos)
      M_ConfigureNetSubsystem();

      Cbuf_AddText("listen 1\n");     // so host_netport will be re-examined
      Cbuf_AddText("coop 1\n");       // Always cooperative mode
      Cbuf_AddText("deathmatch 0\n"); // Never deathmatch
      Cbuf_AddText(va("maxplayers %u\n", maxplayers));
      SCR_BeginLoadingPlaque();

      // Use selected map from our map list
      Cbuf_AddText(va("map %s\n", M_GetMapName(start_map_index)));

      return;
    }

    M_NetStart_Change(1);
    break;
  }
}

//=============================================================================
/* SEARCH MENU */

qboolean searchComplete = false;
double searchCompleteTime;

void M_Menu_Search_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_search;
  m_entersound = false;
  slistSilent = true;
  slistLocal = false;
  searchComplete = false;
  NET_Slist_f();
}

void M_Search_Draw(void) {
  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  DRAW_HEADER("SEARCH LOBBIES");

  Draw_ColoredStringScale(10, menu_offset_y + 40, "Searching local network...",
                          1, 1, 1, 1, 1.5f);

  if (slistInProgress) {
    NET_Poll();
    return;
  }

  if (!searchComplete) {
    searchComplete = true;
    searchCompleteTime = realtime;
  }

  if (hostCacheCount) {
    M_Menu_ServerList_f();
    return;
  }

  Draw_ColoredStringScale(10, menu_offset_y + 70, "No lobby found", 1, 0, 0, 1,
                          1.5f);
  if ((realtime - searchCompleteTime) < 3.0)
    return;

  M_Menu_LanConfig_f();
}

void M_Search_Key(int key) {}

//=============================================================================
/* SLIST MENU */

int slist_cursor;
qboolean slist_sorted;

void M_Menu_ServerList_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_slist;
  m_entersound = true;
  slist_cursor = 0;
  m_return_onerror = false;
  m_return_reason[0] = 0;
  slist_sorted = false;
}

void M_ServerList_Draw(void) {
  int n;

  if (!slist_sorted) {
    slist_sorted = true;
    NET_SlistSort();
  }

  MENU_INITVARS();

  if (paused_hack == false)
    Draw_BgMenu();

  Draw_FillByColor(0, 0, 1280, 720, 0, 0, 0, 0.4);

  DRAW_HEADER("LOBBIES FOUND");

  for (n = 0; n < hostCacheCount; n++) {
    DRAW_MENUOPTION(n, NET_SlistPrintServer(n), slist_cursor, false);
  }

  if (*m_return_reason) {
    Draw_ColoredStringScale(10, menu_offset_y + 40, m_return_reason, 1, 0, 0, 1,
                            1.5f);
  }
}

void M_ServerList_Key(int k) {
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_LanConfig_f();
    break;

  case K_SPACE:
    M_Menu_Search_f();
    break;

  case K_UPARROW:
  case K_LEFTARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    slist_cursor--;
    if (slist_cursor < 0)
      slist_cursor = hostCacheCount - 1;
    break;

  case K_DOWNARROW:
  case K_RIGHTARROW:
    S_LocalSound("sounds/menu/navigate.wav");
    slist_cursor++;
    if (slist_cursor >= hostCacheCount)
      slist_cursor = 0;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    S_LocalSound("sounds/menu/navigate.wav");
    m_return_state = m_state;
    m_return_onerror = true;
    slist_sorted = false;
    IN_Activate();
    key_dest = key_game;
    m_state = m_none;
    Cbuf_AddText(
        va("connect \"%s\"\n", NET_SlistPrintServerName(slist_cursor)));
    break;

  default:
    break;
  }
}

//=============================================================================
/* Menu Subsystem */

void M_Init(void) {
  Cmd_AddCommand("togglemenu", M_ToggleMenu_f);
  Cmd_AddCommand("menu_main", M_Menu_Main_f);
  Cmd_AddCommand("menu_singleplayer", M_Menu_SinglePlayer_f);
  Cmd_AddCommand("menu_multiplayer", M_Menu_MultiPlayer_f);
  Cmd_AddCommand("menu_setup", M_Menu_Setup_f);
  Cmd_AddCommand("menu_options", M_Menu_Options_f);
  Cmd_AddCommand("menu_keys", M_Menu_Keys_f);
  Cmd_AddCommand("menu_video", M_Menu_Video_f);
  Cmd_AddCommand("help", M_Menu_Credits_f);
  Cmd_AddCommand("menu_quit", M_Menu_Quit_f);
#ifdef VITA
  Cmd_AddCommand("savea", Save_Achivements);
  Cmd_AddCommand("loada", Load_Achivements);
#endif
  Cvar_RegisterVariable(&cl_enablereartouchpad);
  PSel_RegisterCvars(); // per-map Perk Selection slots

  // Sys_FileOpenRead (va("%s/maps/%s.way",com_gamedir, sv.name), &h);
  // Sys_FileOpenRead(va("%s/version.txt", com_gamedir));

  // Snag the game version
  long length;
  FILE *f = fopen(va("%s/version.txt", com_gamedir), "rb");

  if (f) {
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    game_build_date = malloc(length);

    if (game_build_date)
      fread(game_build_date, 1, length, f);

    fclose(f);
  } else {
    game_build_date = "version.txt not found.";
  }

  Map_Finder();
}

qboolean music_init = false;

void M_Draw(void) {
  if (m_state == m_none || key_dest != key_menu)
    return;

  if (!music_init) {
    Cbuf_AddText("music tensioned_by_the_damned\n");
    Cbuf_AddText("music_loop 1\n");
    music_init = true;
  }

  if (!m_recursiveDraw) {
    // scr_copyeverything = 1;

    if (scr_con_current) {
      Draw_ConsoleBackground();
      S_ExtraUpdate();
    }

    Draw_FadeScreen(); // johnfitz -- fade even if console fills screen
  } else {
    m_recursiveDraw = false;
  }

  GL_SetCanvas(CANVAS_USEPRINT);

  switch (m_state) {
  case m_none:
    break;

  case m_start:
    // Menu_Background_Draw (MENU_START);
    M_Start_Menu_Draw();
    break;
  case m_main:
    // Menu_Background_Draw (MENU_MAIN);
    M_Main_Draw();
    break;
  case m_paused_menu:
    // Menu_Background_Draw (MENU_PAUSE);
    M_Paused_Menu_Draw();
    break;
  case m_restart:
    M_Restart_Draw();
    break;
  case m_exit:
    M_Exit_Draw();
    break;
  case m_singleplayer:
    // Menu_Background_Draw (MENU_DEFAULT);
    M_SinglePlayer_Draw();
    break;
  case m_maps:
    // Menu_Background_Draw (MENU_DEFAULT);
    M_Menu_Maps_Draw();
    break;
  case m_multiplayer:
    M_MultiPlayer_Draw();
    break;
  case m_achievement: // un-gated from VITA so the Switch can view achievements
    M_Achievement_Draw();
    break;
  case m_loadout: // equip unlocked loadout perks
    M_Loadout_Draw();
    break;
  case m_perksel: // per-map perk machine selection
    M_PerkSelection_Draw();
    break;
  case m_setup:
    M_Setup_Draw();
    break;
  case m_net:
    M_Graphics_Settings_Draw();
    break;
  case m_options:
    M_Options_Draw();
    break;
  case m_keys:
    M_Keys_Draw();
    break;
  case m_video:
    M_Control_Settings_Draw();
    break;
  case m_credits:
    M_Credits_Draw();
    break;
  case m_gamemodes:
    M_Gamemodes_Draw();
    break;
  case m_quit:
    if (!fitzmode) { /* QuakeSpasm customization: */
      /* Quit now! S.A. */
      key_dest = key_console;
      Host_Quit_f();
    }
    M_Quit_Draw();
    break;
  case m_lanconfig:
    M_LanConfig_Draw();
    break;
  case m_gameoptions:
    // Menu_Background_Draw (MENU_DEFAULT);
    M_GameOptions_Draw();
    break;
  case m_search:
    M_Search_Draw();
    break;
  case m_slist:
    M_ServerList_Draw();
    break;
  default:
    break;
  }

  // draw the on-screen keyboard on top of whatever menu opened it.
  if (menu_osk_active)
    M_OSK_Draw();

  if (m_entersound) {
    S_LocalSound("sounds/menu/navigate.wav");
    m_entersound = false;
  }

  S_ExtraUpdate();
}

void M_Keydown(int key) {
  // when the on-screen keyboard is up it owns all input until the player
  // confirms (Y) or cancels (B), so menu navigation can't eat the keystrokes.
  if (menu_osk_active) {
    M_OSK_Keydown(key);
    return;
  }

  switch (m_state) {
  case m_none:
    return;

  case m_start:
    M_Start_Key(key);
    return;

  case m_main:
    M_Main_Key(key);
    return;

  case m_paused_menu:
    M_Paused_Menu_Key(key);
    return;

  case m_restart:
    M_Restart_Key(key);
    return;

  case m_exit:
    M_Exit_Key(key);
    return;

  case m_singleplayer:
    M_SinglePlayer_Key(key);
    return;

  case m_maps:
    M_Menu_Maps_Key(key);
    return;

  case m_multiplayer:
    M_MultiPlayer_Key(key);
    return;
  case m_achievement: // un-gated from VITA
    M_Achievement_Key(key);
    return;
  case m_loadout:
    M_Loadout_Key(key);
    return;
  case m_perksel:
    M_PerkSelection_Key(key);
    return;

  case m_setup:
    M_Setup_Key(key);
    return;

  case m_net:
    M_Graphics_Settings_Key(key);
    return;

  case m_options:
    M_Options_Key(key);
    return;

  case m_keys:
    M_Keys_Key(key);
    return;

  case m_video:
    M_Control_Settings_Key(key);
    return;

  case m_credits:
    M_Credits_Key(key);
    return;

  case m_gamemodes:
    M_Gamemodes_Key(key);
    return;

  case m_quit:
    M_Quit_Key(key);
    return;

  case m_lanconfig:
    M_LanConfig_Key(key);
    return;

  case m_gameoptions:
    M_GameOptions_Key(key);
    return;

  case m_search:
    M_Search_Key(key);
    break;

  case m_slist:
    M_ServerList_Key(key);
    return;

  default:
    return;
  }
}

void M_Charinput(int key) {
  // the OSK handles its own character input; ignore physical-keyboard
  // chars while it's up so the field and the OSK buffer can't desync.
  if (menu_osk_active)
    return;

  switch (m_state) {
  case m_setup:
    M_Setup_Char(key);
    return;
  case m_quit:
    M_Quit_Char(key);
    return;
  case m_lanconfig:
    M_LanConfig_Char(key);
    return;
  default:
    return;
  }
}

qboolean M_TextEntry(void) {
  switch (m_state) {
  case m_setup:
    return M_Setup_TextEntry();
  case m_quit:
    return M_Quit_TextEntry();
  case m_lanconfig:
    return M_LanConfig_TextEntry();
  default:
    return false;
  }
}

void M_ConfigureNetSubsystem(void) {
  // enable/disable net systems to match desired config
  Cbuf_AddText("stopdemo\n");

  // the Host path never seeds lanConfig_port, so guard against clobbering
  // net_hostport with 0 (it would bind a random port instead of 26000, so
  // clients find nothing).
  if (lanConfig_port == 0)
    lanConfig_port = DEFAULTnet_hostport;

  if (IPXConfig || TCPIPConfig)
    net_hostport = lanConfig_port;
}
