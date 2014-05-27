
#include <string.h>
#include "winlnxdefs.h"

#include "gles2N64.h"
#include "Debug.h"
#include "Zilmar GFX 1.3.h"
#include "OpenGL.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"
#include "Config.h"
#include "Textures.h"
#include "ShaderCombiner.h"

HWND        hWnd;
#ifndef __LINUX__
HWND        hStatusBar;
//HWND      hFullscreen;
HWND        hToolBar;
HINSTANCE   hInstance;
#endif // !__LINUX__

char        pluginName[] = "gles2n64 v0.0.5";
char        *screenDirectory;
u32         last_good_ucode = (u32) -1;
void        (*CheckInterrupts)( void );
char        configdir[PATH_MAX] = {0};
#ifdef M64P_STATIC_PLUGINS
void        (*renderCallback)(int) = NULL;
#else
void        (*renderCallback)() = NULL;
#endif

#ifdef EMSCRIPTEN
#define BOOL int
#endif

void _init( void )
{
    Config_LoadConfig();
}

EXPORT void CALL CaptureScreen ( char * Directory )
{
    screenDirectory = Directory;
    OGL_SaveScreenshot();
}

EXPORT void CALL ChangeWindow (void)
{
}

EXPORT void CALL CloseDLL (void)
{
}

EXPORT void CALL DllAbout ( HWND hParent )
{
    puts( "gles2N64 by Adventus / Orkin\nThanks to Clements, Rice, Gonetz, Malcolm, Dave2001, cryhlove, icepir8, zilmar, Azimer, and StrmnNrmn\nported by blight" );
}

EXPORT void CALL DllConfig ( HWND hParent )
{
    Config_DoConfig(hParent);
}

EXPORT void CALL DllTest ( HWND hParent )
{
}

EXPORT void CALL DrawScreen (void)
{
}

#ifdef M64P_STATIC_PLUGINS
EXPORT m64p_error CALL PluginGetVersionVideo(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
	if(PluginType)
		*PluginType = M64PLUGIN_GFX;
	if(PluginVersion)
		*PluginVersion = 0x101;
	if(PluginNamePtr)
		*PluginNamePtr = pluginName;
	if(APIVersion)
		*APIVersion = 0x20200;
	return M64ERR_SUCCESS;
}
#else
EXPORT void CALL GetDllInfo ( PLUGIN_INFO * PluginInfo )
{

    PluginInfo->Version = 0x101;
    PluginInfo->Type = PLUGIN_TYPE_GFX;
    strcpy( PluginInfo->Name, pluginName);
    PluginInfo->NormalMemory = FALSE;
    PluginInfo->MemoryBswaped = TRUE;
}
#endif

EXPORT BOOL CALL InitiateGFX (GFX_INFO Gfx_Info)
{

#ifndef EMSCRIPTEN
    hWnd = Gfx_Info.hWnd;
#endif

    DMEM = Gfx_Info.DMEM;
    IMEM = Gfx_Info.IMEM;
    RDRAM = Gfx_Info.RDRAM;

    REG.MI_INTR = (u32*) Gfx_Info.MI_INTR_REG;
    REG.DPC_START = (u32*) Gfx_Info.DPC_START_REG;
    REG.DPC_END = (u32*) Gfx_Info.DPC_END_REG;
    REG.DPC_CURRENT = (u32*) Gfx_Info.DPC_CURRENT_REG;
    REG.DPC_STATUS = (u32*) Gfx_Info.DPC_STATUS_REG;
    REG.DPC_CLOCK = (u32*) Gfx_Info.DPC_CLOCK_REG;
    REG.DPC_BUFBUSY = (u32*) Gfx_Info.DPC_BUFBUSY_REG;
    REG.DPC_PIPEBUSY = (u32*) Gfx_Info.DPC_PIPEBUSY_REG;
    REG.DPC_TMEM = (u32*) Gfx_Info.DPC_TMEM_REG;

    REG.VI_STATUS = (u32*) Gfx_Info.VI_STATUS_REG;
    REG.VI_ORIGIN = (u32*) Gfx_Info.VI_ORIGIN_REG;
    REG.VI_WIDTH = (u32*) Gfx_Info.VI_WIDTH_REG;
    REG.VI_INTR = (u32*) Gfx_Info.VI_INTR_REG;
    REG.VI_V_CURRENT_LINE = (u32*) Gfx_Info.VI_V_CURRENT_LINE_REG;
    REG.VI_TIMING = (u32*) Gfx_Info.VI_TIMING_REG;
    REG.VI_V_SYNC = (u32*) Gfx_Info.VI_V_SYNC_REG;
    REG.VI_H_SYNC = (u32*) Gfx_Info.VI_H_SYNC_REG;
    REG.VI_LEAP = (u32*) Gfx_Info.VI_LEAP_REG;
    REG.VI_H_START = (u32*) Gfx_Info.VI_H_START_REG;
    REG.VI_V_START = (u32*) Gfx_Info.VI_V_START_REG;
    REG.VI_V_BURST = (u32*) Gfx_Info.VI_V_BURST_REG;
    REG.VI_X_SCALE = (u32*) Gfx_Info.VI_X_SCALE_REG;
    REG.VI_Y_SCALE = (u32*) Gfx_Info.VI_Y_SCALE_REG;

    CheckInterrupts = Gfx_Info.CheckInterrupts;

    Config_LoadConfig();
    Config_LoadRomConfig(Gfx_Info.HEADER);

    return TRUE;
}

EXPORT void CALL MoveScreen (int xpos, int ypos)
{
}


EXPORT void CALL ProcessDList(void)
{
    OGL.frame_dl++;

    if (config.autoFrameSkip)
    {
        OGL_UpdateFrameTime();

        if (OGL.consecutiveSkips < 1)
        {
            unsigned t = 0;
            for(int i = 0; i < OGL_FRAMETIME_NUM; i++) t += OGL.frameTime[i];
            t *= config.targetFPS;
            if (config.romPAL) t = (t * 5) / 6;
            if (t > (OGL_FRAMETIME_NUM * 1000))
            {
                OGL.consecutiveSkips++;
                OGL.frameSkipped++;
                RSP.busy = FALSE;
                RSP.DList++;
                return;
            }
        }
    }
    else if ((OGL.frame_vsync % config.frameRenderRate) != 0)
    {
        OGL.frameSkipped++;
        RSP.busy = FALSE;
        RSP.DList++;
        return;
    }

    OGL.consecutiveSkips = 0;
    RSP_ProcessDList();
    OGL.mustRenderDlist = true;
}

EXPORT void CALL ProcessRDPList(void)
{

}

EXPORT void CALL RomClosed (void)
{
    OGL_Stop();
}

EXPORT 
#ifdef EMSCRIPTEN
int
#else
void
#endif
CALL RomOpen (void)
{
    RSP_Init();
    OGL.frame_vsync = 0;
    OGL.frame_dl = 0;
    OGL.frame_prevdl = -1;
    OGL.mustRenderDlist = false;
#ifdef EMSCRIPTEN
	return 1;
#endif
}

EXPORT void CALL ShowCFB (void)
{
}

EXPORT void CALL UpdateScreen (void)
{
    //has there been any display lists since last update
    if (OGL.frame_prevdl == OGL.frame_dl) return;

    OGL.frame_prevdl = OGL.frame_dl;

    if (OGL.frame_dl > 0) OGL.frame_vsync++;

    if (OGL.mustRenderDlist)
    {
        OGL.screenUpdate=true;
        VI_UpdateScreen();
        OGL.mustRenderDlist = false;
    }
}

EXPORT void CALL ViStatusChanged (void)
{
}

EXPORT void CALL ViWidthChanged (void)
{
}

EXPORT void CALL 
#ifdef M64P_STATIC_PLUGINS
ReadScreen2(void *dest, int *width, int *height, int front)
{
    OGL_ReadScreen( dest, width, height );
}
#else
ReadScreen (void **dest, int *width, int *height)
{
    OGL_ReadScreen( dest[0], width, height );
}
#endif

EXPORT void CALL SetConfigDir (char *configDir)
{
    strncpy(configdir, configDir, PATH_MAX);
}

EXPORT void CALL SetRenderingCallback
#ifdef M64P_STATIC_PLUGINS
(void (*callback)(int))
#else
(void (*callback)())
#endif
{
    renderCallback = callback;
}

EXPORT void CALL ResizeVideoOutput(int width, int height)
{
	OGL_ResizeWindow(0, 0, width, height);
}