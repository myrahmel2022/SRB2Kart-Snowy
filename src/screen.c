// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  screen.c
/// \brief Handles multiple resolutions, 8bpp/16bpp(highcolor) modes

#include "doomdef.h"
#include "doomstat.h"
#include "screen.h"
#include "console.h"
#include "am_map.h"
#include "i_system.h"
#include "i_video.h"
#include "r_local.h"
#include "r_sky.h"
#include "m_argv.h"
#include "m_misc.h"
#include "v_video.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "z_zone.h"
#include "d_main.h"
#include "d_clisrv.h"
#include "f_finale.h"


#if defined (USEASM) && !defined (NORUSEASM)//&& (!defined (_MSC_VER) || (_MSC_VER <= 1200))
#define RUSEASM //MSC.NET can't patch itself
#endif

// --------------------------------------------
// assembly or c drawer routines for 8bpp/16bpp
// --------------------------------------------
void (*wallcolfunc)(void); // new wall column drawer to draw posts >128 high
void (*colfunc)(void); // standard column, up to 128 high posts

void (*basecolfunc)(void);
void (*fuzzcolfunc)(void); // standard fuzzy effect column drawer
void (*transcolfunc)(void); // translation column drawer
void (*shadecolfunc)(void); // smokie test..
void (*spanfunc)(void); // span drawer, use a 64x64 tile
void (*splatfunc)(void); // span drawer w/ transparency
void (*basespanfunc)(void); // default span func for color mode
void (*transtransfunc)(void); // translucent translated column drawer
void (*twosmultipatchfunc)(void); // for cols with transparent pixels
void (*twosmultipatchtransfunc)(void); // for cols with transparent pixels AND translucency

// ------------------
// global video state
// ------------------
viddef_t vid;
INT32 setmodeneeded; //video mode change needed if > 0 (the mode number to set + 1)

static CV_PossibleValue_t scr_depth_cons_t[] = {{8, "8 bits"}, {16, "16 bits"}, {24, "24 bits"}, {32, "32 bits"}, {0, NULL}};

static CV_PossibleValue_t shittyscreen_cons_t[] = {{0, "Okay"}, {1, "Shitty"}, {2, "Extra Shitty"}, {0, NULL}};

//added : 03-02-98: default screen mode, as loaded/saved in config
#ifdef WII
consvar_t cv_scr_width = {"scr_width", "640", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scr_height = {"scr_height", "480", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scr_depth = {"scr_depth", "16 bits", CV_SAVE, scr_depth_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
#else
consvar_t cv_scr_width = {"scr_width", "1280", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scr_height = {"scr_height", "800", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scr_depth = {"scr_depth", "16 bits", CV_SAVE, scr_depth_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
#endif
consvar_t cv_renderview = {"renderview", "On", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_vhseffect = {"vhspause", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_shittyscreen = {"televisionsignal", "Okay", CV_NOSHOWHELP, shittyscreen_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static void SCR_ChangeFullscreen (void);

consvar_t cv_fullscreen = {"fullscreen", "Yes", CV_SAVE|CV_CALL, CV_YesNo, SCR_ChangeFullscreen, 0, NULL, NULL, 0, 0, NULL};

//static CV_PossibleValue_t framerate_cons_t[] = {{35, "35"}, {50, "50"}, {60, "60"}, {70, "70"}, {90, "90"}, {120, "120"}, {144, "144"}, {240, "240"}, {1000, "Unlimited"}, {24, "Cinematic"}, {0, NULL}};
//consvar_t cv_framerate = {"framerate", "Unlimited", CV_SAVE, framerate_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t interpolationmode_cons_t[] = {{0, "Off"}, {1, "On, capped"}, {2, "Uncapped"}, {0, NULL}};
consvar_t cv_interpolationmode = {"interpolationmode", "1", CV_SAVE, interpolationmode_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t frameratecap_cons_t[] = {{24, "MIN"}, {1000, "MAX"}, {0, NULL}};
consvar_t cv_frameratecap = {"frameratecap", "100", CV_SAVE, frameratecap_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t lerp_cons_t[] = {{FRACUNIT, "None"}, {FRACUNIT*3/4, "Little"}, {FRACUNIT/2, "Some"}, {FRACUNIT/4, "Most"}, {0, "Full"}, {0, NULL}};
consvar_t cv_extrapolation = {"extrapolation", "None", CV_SAVE, lerp_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_gifrecordinterpolatedframes = {"gif_recordinterpolated", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

// =========================================================================
//                           SCREEN VARIABLES
// =========================================================================

INT32 scr_bpp; // current video mode bytes per pixel
UINT8 *scr_borderpatch; // flat used to fill the reduced view borders set at ST_Init()

// =========================================================================

//  Short and Tall sky drawer, for the current color mode
void (*walldrawerfunc)(void);

boolean R_ASM = true;
boolean R_486 = false;
boolean R_586 = false;
boolean R_MMX = false;
boolean R_SSE = false;
boolean R_3DNow = false;
boolean R_MMXExt = false;
boolean R_SSE2 = false;


void SCR_SetMode(void)
{
	if (dedicated)
		return;

	if (!setmodeneeded || WipeInAction)
		return; // should never happen and don't change it during a wipe, BAD!

	VID_SetMode(--setmodeneeded);

	V_SetPalette(0);

	//
	//  setup the right draw routines for either 8bpp or 16bpp
	//
	if (true)//vid.bpp == 1) //Always run in 8bpp. todo: remove all 16bpp code?
	{
		spanfunc = basespanfunc = R_DrawSpan_8;
		splatfunc = R_DrawSplat_8;
		transcolfunc = R_DrawTranslatedColumn_8;
		transtransfunc = R_DrawTranslatedTranslucentColumn_8;

		colfunc = basecolfunc = R_DrawColumn_8;
		shadecolfunc = R_DrawShadeColumn_8;
		fuzzcolfunc = R_DrawTranslucentColumn_8;
		walldrawerfunc = R_DrawWallColumn_8;
		twosmultipatchfunc = R_Draw2sMultiPatchColumn_8;
		twosmultipatchtransfunc = R_Draw2sMultiPatchTranslucentColumn_8;
#ifdef RUSEASM
		if (R_ASM)
		{
			if (R_MMX)
			{
				colfunc = basecolfunc = R_DrawColumn_8_MMX;
				//shadecolfunc = R_DrawShadeColumn_8_ASM;
				//fuzzcolfunc = R_DrawTranslucentColumn_8_ASM;
				walldrawerfunc = R_DrawWallColumn_8_MMX;
				twosmultipatchfunc = R_Draw2sMultiPatchColumn_8_MMX;
				spanfunc = basespanfunc = R_DrawSpan_8_MMX;
			}
			else
			{
				colfunc = basecolfunc = R_DrawColumn_8_ASM;
				//shadecolfunc = R_DrawShadeColumn_8_ASM;
				//fuzzcolfunc = R_DrawTranslucentColumn_8_ASM;
				walldrawerfunc = R_DrawWallColumn_8_ASM;
				twosmultipatchfunc = R_Draw2sMultiPatchColumn_8_ASM;
			}
		}
#endif
	}
/*	else if (vid.bpp > 1)
	{
		I_OutputMsg("using highcolor mode\n");
		spanfunc = basespanfunc = R_DrawSpan_16;
		transcolfunc = R_DrawTranslatedColumn_16;
		transtransfunc = R_DrawTranslucentColumn_16; // No 16bit operation for this function

		colfunc = basecolfunc = R_DrawColumn_16;
		shadecolfunc = NULL; // detect error if used somewhere..
		fuzzcolfunc = R_DrawTranslucentColumn_16;
		walldrawerfunc = R_DrawWallColumn_16;
	}*/
	else
		I_Error("unknown bytes per pixel mode %d\n", vid.bpp);
/*#if !defined (DC) && !defined (WII)
	if (SCR_IsAspectCorrect(vid.width, vid.height))
		CONS_Alert(CONS_WARNING, M_GetText("Resolution is not aspect-correct!\nUse a multiple of %dx%d\n"), BASEVIDWIDTH, BASEVIDHEIGHT);
#endif*/
	// set the apprpriate drawer for the sky (tall or INT16)
	setmodeneeded = 0;
}

// do some initial settings for the game loading screen
//
void SCR_Startup(void)
{
	const CPUInfoFlags *RCpuInfo = I_CPUInfo();
	if (!M_CheckParm("-NOCPUID") && RCpuInfo)
	{
#if defined (__i386__) || defined (_M_IX86) || defined (__WATCOMC__)
		R_486 = true;
#endif
		if (RCpuInfo->RDTSC)
			R_586 = true;
		if (RCpuInfo->MMX)
			R_MMX = true;
		if (RCpuInfo->AMD3DNow)
			R_3DNow = true;
		if (RCpuInfo->MMXExt)
			R_MMXExt = true;
		if (RCpuInfo->SSE)
			R_SSE = true;
		if (RCpuInfo->SSE2)
			R_SSE2 = true;
		CONS_Printf("CPU Info: 486: %i, 586: %i, MMX: %i, 3DNow: %i, MMXExt: %i, SSE2: %i\n", R_486, R_586, R_MMX, R_3DNow, R_MMXExt, R_SSE2);
	}

	if (M_CheckParm("-noASM"))
		R_ASM = false;
	if (M_CheckParm("-486"))
		R_486 = true;
	if (M_CheckParm("-586"))
		R_586 = true;
	if (M_CheckParm("-MMX"))
		R_MMX = true;
	if (M_CheckParm("-3DNow"))
		R_3DNow = true;
	if (M_CheckParm("-MMXExt"))
		R_MMXExt = true;

	if (M_CheckParm("-SSE"))
		R_SSE = true;
	if (M_CheckParm("-noSSE"))
		R_SSE = false;

	if (M_CheckParm("-SSE2"))
		R_SSE2 = true;

	M_SetupMemcpy();

	if (dedicated)
	{
		V_Init();
		V_SetPalette(0);
		return;
	}

	vid.modenum = 0;

	vid.dupx = vid.width / BASEVIDWIDTH;
	vid.dupy = vid.height / BASEVIDHEIGHT;
	vid.dupx = vid.dupy = (vid.dupx < vid.dupy ? vid.dupx : vid.dupy);
	vid.fdupx = FixedDiv(vid.width*FRACUNIT, BASEVIDWIDTH*FRACUNIT);
	vid.fdupy = FixedDiv(vid.height*FRACUNIT, BASEVIDHEIGHT*FRACUNIT);

#ifdef HWRENDER
	if (rendermode != render_opengl && rendermode != render_none) // This was just placing it incorrectly at non aspect correct resolutions in opengl
#endif
		vid.fdupx = vid.fdupy = (vid.fdupx < vid.fdupy ? vid.fdupx : vid.fdupy);

	vid.meddupx = (UINT8)(vid.dupx >> 1) + 1;
	vid.meddupy = (UINT8)(vid.dupy >> 1) + 1;
#ifdef HWRENDER
	vid.fmeddupx = vid.meddupx*FRACUNIT;
	vid.fmeddupy = vid.meddupy*FRACUNIT;
#endif

	vid.smalldupx = (UINT8)(vid.dupx / 3) + 1;
	vid.smalldupy = (UINT8)(vid.dupy / 3) + 1;
#ifdef HWRENDER
	vid.fsmalldupx = vid.smalldupx*FRACUNIT;
	vid.fsmalldupy = vid.smalldupy*FRACUNIT;
#endif

	vid.baseratio = FRACUNIT;

	V_Init();
	CV_RegisterVar(&cv_ticrate);
	CV_RegisterVar(&cv_constextsize);

	V_SetPalette(0);
}

// Called at new frame, if the video mode has changed
//
void SCR_Recalc(void)
{
	if (dedicated)
		return;

	// bytes per pixel quick access
	scr_bpp = vid.bpp;

	// scale 1,2,3 times in x and y the patches for the menus and overlays...
	// calculated once and for all, used by routines in v_video.c
	vid.dupx = vid.width / BASEVIDWIDTH;
	vid.dupy = vid.height / BASEVIDHEIGHT;
	vid.dupx = vid.dupy = (vid.dupx < vid.dupy ? vid.dupx : vid.dupy);
	vid.fdupx = FixedDiv(vid.width*FRACUNIT, BASEVIDWIDTH*FRACUNIT);
	vid.fdupy = FixedDiv(vid.height*FRACUNIT, BASEVIDHEIGHT*FRACUNIT);

#ifdef HWRENDER
	//if (rendermode != render_opengl && rendermode != render_none) // This was just placing it incorrectly at non aspect correct resolutions in opengl
	// 13/11/18:
	// The above is no longer necessary, since we want OpenGL to be just like software now
	// -- Monster Iestyn
#endif
		vid.fdupx = vid.fdupy = (vid.fdupx < vid.fdupy ? vid.fdupx : vid.fdupy);

	//vid.baseratio = FixedDiv(vid.height << FRACBITS, BASEVIDHEIGHT << FRACBITS);
	vid.baseratio = FRACUNIT;

	vid.meddupx = (UINT8)(vid.dupx >> 1) + 1;
	vid.meddupy = (UINT8)(vid.dupy >> 1) + 1;
#ifdef HWRENDER
	vid.fmeddupx = vid.meddupx*FRACUNIT;
	vid.fmeddupy = vid.meddupy*FRACUNIT;
#endif

	vid.smalldupx = (UINT8)(vid.dupx / 3) + 1;
	vid.smalldupy = (UINT8)(vid.dupy / 3) + 1;
#ifdef HWRENDER
	vid.fsmalldupx = vid.smalldupx*FRACUNIT;
	vid.fsmalldupy = vid.smalldupy*FRACUNIT;
#endif

	// toggle off automap because some screensize-dependent values will
	// be calculated next time the automap is activated.
	if (automapactive)
		AM_Stop();

	// set the screen[x] ptrs on the new vidbuffers
	V_Init();

	// scr_viewsize doesn't change, neither detailLevel, but the pixels
	// per screenblock is different now, since we've changed resolution.
	R_SetViewSize(); //just set setsizeneeded true now ..

	// vid.recalc lasts only for the next refresh...
	con_recalc = true;
	am_recalc = true;
}

// Check for screen cmd-line parms: to force a resolution.
//
// Set the video mode to set at the 1st display loop (setmodeneeded)
//

void SCR_CheckDefaultMode(void)
{
	INT32 scr_forcex, scr_forcey; // resolution asked from the cmd-line

	if (dedicated)
		return;

	// 0 means not set at the cmd-line
	scr_forcex = scr_forcey = 0;

	if (M_CheckParm("-width") && M_IsNextParm())
		scr_forcex = atoi(M_GetNextParm());

	if (M_CheckParm("-height") && M_IsNextParm())
		scr_forcey = atoi(M_GetNextParm());

	if (scr_forcex && scr_forcey)
	{
		CONS_Printf(M_GetText("Using resolution: %d x %d\n"), scr_forcex, scr_forcey);
		// returns -1 if not found, thus will be 0 (no mode change) if not found
		setmodeneeded = VID_GetModeForSize(scr_forcex, scr_forcey) + 1;
	}
	else
	{
		CONS_Printf(M_GetText("Default resolution: %d x %d (%d bits)\n"), cv_scr_width.value,
			cv_scr_height.value, cv_scr_depth.value);
		// see note above
		setmodeneeded = VID_GetModeForSize(cv_scr_width.value, cv_scr_height.value) + 1;
	}
}

// sets the modenum as the new default video mode to be saved in the config file
void SCR_SetDefaultMode(void)
{
	// remember the default screen size
	CV_SetValue(&cv_scr_width, vid.width);
	CV_SetValue(&cv_scr_height, vid.height);
	CV_SetValue(&cv_scr_depth, vid.bpp*8);
}

// Change fullscreen on/off according to cv_fullscreen
void SCR_ChangeFullscreen(void)
{
#ifdef DIRECTFULLSCREEN
	// allow_fullscreen is set by VID_PrepareModeList
	// it is used to prevent switching to fullscreen during startup
	if (!allow_fullscreen)
		return;

	if (graphics_started)
	{
		VID_PrepareModeList();
		setmodeneeded = VID_GetModeForSize(vid.width, vid.height) + 1;
	}
	return;
#endif
}

boolean SCR_IsAspectCorrect(INT32 width, INT32 height)
{
	return
	 (  width % BASEVIDWIDTH == 0
	 && height % BASEVIDHEIGHT == 0
	 && width / BASEVIDWIDTH == height / BASEVIDHEIGHT
	 );
}

// XMOD FPS display
// moved out of os-specific code for consistency
#define FPSRATE 20
static uint8_t fpsgraph[FPSRATE];
static tic_t lasttic, totaltics;

void SCR_DisplayTicRate(void)
{
	tic_t i;
	tic_t ontic = I_GetFrameReference(FPSRATE);
	
	UINT32 flags = V_SNAPTORIGHT | V_SNAPTOBOTTOM;

	INT32 fps_color = SKINCOLOR_WHITE;
	const UINT8 *ticcntcolor = NULL;

	if (lasttic != ontic)
	{
		totaltics = 0;
		for (i = 0;i < FPSRATE;++i)
			totaltics += fpsgraph[i];

		for (i = lasttic + 1; i < FPSRATE+lasttic && i < ontic; ++i)
			fpsgraph[i] = 0;

		fpsgraph[ontic] = 1;
	}
	else
		fpsgraph[ontic]++;

	if 		(totaltics <= TICRATE/2) fps_color = SKINCOLOR_CHERRY; // <= 17
	else if (totaltics >= TICRATE*2) fps_color = SKINCOLOR_AQUA;   // >= 70
	else if (totaltics >= TICRATE)	 fps_color = SKINCOLOR_MINT;   // >= 35

	ticcntcolor = R_GetTranslationColormap(TC_RAINBOW, fps_color, GTC_CACHE);

	if (cv_ticrate.value == 2) // Enhanced.
	{
		INT32 frame_offs = (cv_frameratecap.value > 99) ? 301 : 305;
		
		// "FPS"
		V_DrawFixedPatch(305 << FRACBITS, 183 << FRACBITS, FRACUNIT, flags, framecounter, R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_YELLOW, GTC_CACHE));

		// A lot of stuff is changed here, if we count for uncapped values, or capped ones.
		switch (cv_interpolationmode.value)
		{
			case 1: // Capped.
				V_DrawPingNum(317, 190, flags, cv_frameratecap.value, ticcntcolor);
				V_DrawFixedPatch(frame_offs << FRACBITS, 190 << FRACBITS, FRACUNIT, flags, frameslash, ticcntcolor);
				V_DrawPingNum(frame_offs, 190, flags, totaltics, ticcntcolor);
				break;

			case 2: // Uncapped!
				V_DrawPingNum(317, 190, flags, totaltics, ticcntcolor);
				break;

			default: // We turned that off.
				V_DrawPingNum(317, 190, flags, TICRATE, ticcntcolor);
				V_DrawFixedPatch(305 << FRACBITS, 190 << FRACBITS, FRACUNIT, flags, frameslash, ticcntcolor);
				V_DrawPingNum(305, 190, flags, totaltics, ticcntcolor);
				break;
		}
	}
	else if (cv_ticrate.value == 1) // Compact. Just like vanilla.
	{
		switch (cv_interpolationmode.value)
		{
			case 2: // Uncapped.
				V_DrawRightAlignedString(318, 191, flags, va("%s%d\x82 FPS", V_ApproximateSkinColorCode(fps_color), totaltics));
				break;

			case 1: // Capped.
				V_DrawRightAlignedString(318, 191, flags, va("%s%d/%d\x82 FPS", V_ApproximateSkinColorCode(fps_color), totaltics, cv_frameratecap.value));
				break;

			default:
				V_DrawRightAlignedString(318, 191, flags, va("%s%d/%d\x82 FPS", V_ApproximateSkinColorCode(fps_color), totaltics, TICRATE));
				break;
		}
	}

	lasttic = ontic;
}

// SCR_DisplayLocalPing
// Used to draw the user's local ping next to the framerate for a quick check without having to hold TAB for instance. By default, it only shows up if your ping is too high and risks getting you kicked.

void SCR_DisplayLocalPing(void)
{
	UINT32 ping = playerpingtable[consoleplayer];	// consoleplayer's ping is everyone's ping in a splitnetgame :P
	if (cv_showping.value == 1 || (cv_showping.value == 2 && ping > servermaxping))	// only show 2 (warning) if our ping is at a bad level
	{
		INT32 dispy = 0;

		switch (cv_ticrate.value)
		{
			case 2: // Enchanced.
				dispy = 164;
				break;

			case 1: // Compact.
				dispy = 172;
				break;

			default: // Off.
				dispy = 181;
				break;
		}

		HU_drawPing(307, dispy, ping, V_SNAPTORIGHT|V_SNAPTOBOTTOM);
	}
}
