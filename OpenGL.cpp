
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <png.h>
#ifndef EMSCRIPTEN
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#else
#include <emscripten.h>
#endif
#include <unistd.h>

#include <SDL.h>
#include <SDL/SDL_syswm.h>


#define timeGetTime() time(NULL)

#include "Common.h"
#include "winlnxdefs.h"
#include "gles2N64.h"
#include "OpenGL.h"
#include "Types.h"
#include "N64.h"
#include "gSP.h"
#include "gDP.h"
#include "Textures.h"
#include "ShaderCombiner.h"
#include "VI.h"
#include "RSP.h"
#include "Config.h"

//#define BATCH_TEST
//#define TEXTURECACHE_TEST
//#define RENDERSTATE_TEST
//#define SHADER_TEST


#ifdef TEXTURECACHE_TEST
int     TextureCacheTime = 0;
#endif


#ifdef RENDERSTATE_TEST
int     StateChanges = 0;
#endif

#ifdef SHADER_TEST
int     ProgramSwaps = 0;
#endif

#ifdef BATCH_TEST
int     TotalDrawTime = 0;
int     TotalTriangles = 0;
int     TotalDrawCalls = 0;
#define glDrawElements(A,B,C,D) \
    TotalTriangles += B; TotalDrawCalls++; int t = SDL_GetTicks(); glDrawElements(A,B,C,D); TotalDrawTime += (SDL_GetTicks() - t);
#define glDrawArrays(A,B,C) \
    TotalTriangles += C; TotalDrawCalls++; int t = SDL_GetTicks(); glDrawArrays(A,B,C); TotalDrawTime += (SDL_GetTicks() - t);

#endif

GLInfo OGL;

const char _default_vsh[] = "                           \n\t" \
"attribute highp vec2 aPosition;                        \n\t" \
"attribute highp vec2 aTexCoord;                        \n\t" \
"varying mediump vec2 vTexCoord;                        \n\t" \
"void main(){                                           \n\t" \
"gl_Position = vec4(aPosition.x, aPosition.y, 0.0, 1.0);\n\t" \
"vTexCoord = aTexCoord;                                 \n\t" \
"}                                                      \n\t";

const char _default_fsh[] = "                           \n\t" \
"uniform sampler2D uTex;                                \n\t" \
"varying mediump vec2 vTexCoord;                        \n\t" \
"void main(){                                           \n\t" \
"gl_FragColor = texture2D(uTex, vTexCoord);             \n\t" \
"}                                                      \n\t";

#define BPP_WANTED 24

const EGLint ConfigAttribs[] =
{
	EGL_LEVEL,				0,
	EGL_DEPTH_SIZE,         BPP_WANTED,
	EGL_STENCIL_SIZE,       0,
	EGL_SURFACE_TYPE,		EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES2_BIT,
	EGL_NATIVE_RENDERABLE,	EGL_FALSE,
	EGL_NONE
};

const EGLint ContextAttribs[] =
{
	EGL_CONTEXT_CLIENT_VERSION, 	2,
	EGL_NONE
};

void OGL_EnableRunfast()
{
#ifdef __arm__
	static const unsigned int x = 0x04086060;
	static const unsigned int y = 0x03000000;
	int r;
	asm volatile (
		"fmrx	%0, fpscr			\n\t"	//r0 = FPSCR
		"and	%0, %0, %1			\n\t"	//r0 = r0 & 0x04086060
		"orr	%0, %0, %2			\n\t"	//r0 = r0 | 0x03000000
		"fmxr	fpscr, %0			\n\t"	//FPSCR = r0
		: "=r"(r)
		: "r"(x), "r"(y)
	);
#endif
}

const char* EGLErrorString()
{
	EGLint nErr = eglGetError();
	switch(nErr){
		case EGL_SUCCESS: 				return "EGL_SUCCESS";
		case EGL_BAD_DISPLAY:			return "EGL_BAD_DISPLAY";
		case EGL_NOT_INITIALIZED:		return "EGL_NOT_INITIALIZED";
		case EGL_BAD_ACCESS:			return "EGL_BAD_ACCESS";
		case EGL_BAD_ALLOC:				return "EGL_BAD_ALLOC";
		case EGL_BAD_ATTRIBUTE:			return "EGL_BAD_ATTRIBUTE";
		case EGL_BAD_CONFIG:			return "EGL_BAD_CONFIG";
		case EGL_BAD_CONTEXT:			return "EGL_BAD_CONTEXT";
		case EGL_BAD_CURRENT_SURFACE:	return "EGL_BAD_CURRENT_SURFACE";
		case EGL_BAD_MATCH:				return "EGL_BAD_MATCH";
		case EGL_BAD_NATIVE_PIXMAP:		return "EGL_BAD_NATIVE_PIXMAP";
		case EGL_BAD_NATIVE_WINDOW:		return "EGL_BAD_NATIVE_WINDOW";
		case EGL_BAD_PARAMETER:			return "EGL_BAD_PARAMETER";
		case EGL_BAD_SURFACE:			return "EGL_BAD_SURFACE";
		default:						return "unknown";
	}
};

int OGL_IsExtSupported( const char *extension )
{
	const GLubyte *extensions = NULL;
	const GLubyte *start;
	GLubyte *where, *terminator;

	where = (GLubyte *) strchr(extension, ' ');
	if (where || *extension == '\0')
		return 0;

	extensions = glGetString(GL_EXTENSIONS);

    if (!extensions) return 0;

	start = extensions;
	for (;;)
	{
		where = (GLubyte *) strstr((const char *) start, extension);
		if (!where)
			break;

		terminator = where + strlen(extension);
		if (where == start || *(where - 1) == ' ')
			if (*terminator == ' ' || *terminator == '\0')
				return 1;

		start = terminator;
	}

	return 0;
}

void OGL_UpdateDepthUpdate()
{
    if (gDP.otherMode.depthUpdate)
        glDepthMask(GL_TRUE);
    else
        glDepthMask(GL_FALSE);
}

extern void _glcompiler_error(GLint shader);

void OGL_InitStates()
{
    GLint   success;

    glEnable(GL_CULL_FACE);
    glEnableVertexAttribArray(SC_POSITION);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
    glEnable(GL_SCISSOR_TEST);
    //glDepthRangef(1.0f, 0.0f);
    //glPolygonOffset(-0.2f, -0.2f);
    glDepthRangef(0.0f, (float)0x7FFF);
    glPolygonOffset(0.2f, 0.2f);
    glViewport(config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);

    //create default shader program
    LOG(LOG_VERBOSE, "Generate Default Shader Program.\n");

    const char *src[1];
    src[0] = _default_fsh;
    OGL.defaultFragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(OGL.defaultFragShader, 1, (const char**) src, NULL);
    glCompileShader(OGL.defaultFragShader);
    glGetShaderiv(OGL.defaultFragShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        LOG(LOG_ERROR, "Failed to produce default fragment shader.\n");
    }

    src[0] = _default_vsh;
    OGL.defaultVertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(OGL.defaultVertShader, 1, (const char**) src, NULL);
    glCompileShader(OGL.defaultVertShader);
    glGetShaderiv(OGL.defaultVertShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        LOG(LOG_ERROR, "Failed to produce default vertex shader.\n");
        _glcompiler_error(OGL.defaultVertShader);
    }

    OGL.defaultProgram = glCreateProgram();
    glBindAttribLocation(OGL.defaultProgram, 0, "aPosition");
    glBindAttribLocation(OGL.defaultProgram, 1, "aTexCoord");
    glAttachShader(OGL.defaultProgram, OGL.defaultFragShader);
    glAttachShader(OGL.defaultProgram, OGL.defaultVertShader);
    glLinkProgram(OGL.defaultProgram);
    glGetProgramiv(OGL.defaultProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        LOG(LOG_ERROR, "Failed to link default program.\n");
        _glcompiler_error(OGL.defaultFragShader);
    }
    glUniform1i(glGetUniformLocation(OGL.defaultProgram, "uTex"), 0);
    glUseProgram(OGL.defaultProgram);

}

void OGL_UpdateScale()
{
    OGL.scaleX = (float)config.framebuffer.width / (float)VI.width;
    OGL.scaleY = (float)config.framebuffer.height / (float)VI.height;
}

void OGL_ResizeWindow(int x, int y, int width, int height)
{
    config.window.xpos = x;
    config.window.ypos = y;
    config.window.width = width;
    config.window.height = height;

    config.framebuffer.xpos = x;
    config.framebuffer.ypos = y;
    config.framebuffer.width = width;
    config.framebuffer.height = height;
    OGL_UpdateScale();

    glViewport(config.framebuffer.xpos, config.framebuffer.ypos,
            config.framebuffer.width, config.framebuffer.height);
}

#if defined(SDL_WINDOW)
bool OGL_SDL_Start()
{
	/* Initialize SDL */
    LOG(LOG_MINIMAL, "Initializing SDL video subsystem...\n" );
    if (SDL_InitSubSystem( SDL_INIT_VIDEO ) == -1)
    {
         LOG(LOG_ERROR, "Error initializing SDL video subsystem: %s\n", SDL_GetError() );
        return FALSE;
    }

    int current_w = config.window.width;
    int current_h = config.window.height;

    /* Set the video mode */
    LOG(LOG_MINIMAL, "Setting video mode %dx%d...\n", current_w, current_h );

    // TODO: I should actually check what the pixelformat is, rather than assuming 16 bpp (RGB_565) or 32 bpp (RGBA_8888):
    int bitsPP = BPP_WANTED;

    // TODO: Replace SDL_SetVideoMode with something that is SDL 2.0 compatible
    //       Better yet, eliminate all SDL calls by using the Mupen64Plus core api
    if (!(OGL.hScreen = SDL_SetVideoMode( current_w, current_h, bitsPP, SDL_HWSURFACE )))
    {
        LOG(LOG_ERROR, "Problem setting videomode %dx%d: %s\n", current_w, current_h, SDL_GetError() );
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
        return FALSE;
    }

	return true;
}
#endif

#ifndef EMSCRIPTEN
bool OGL_X11_Start()
{
	LOG(LOG_MINIMAL, "Starting X11 Window\n");

	Window window;
	Display *display;
	XEvent event;
    Atom atomdelete, atomhint;
	XWindowAttributes attributes;

	display = XOpenDisplay(NULL);
    if (!display) {
        LOG(LOG_ERROR, "Cannot connect to X server\n");
		return false;
    }

    //get dimensions of framebuffer / window
	int x, y, w, h;
    if (config.window.centre){
        config.window.xpos = (config.screen.width - config.window.width) / 2;
        config.window.ypos = (config.screen.height - config.window.height) / 2;
    }

    if (config.window.fullscreen){
        x = 0;
        y = 0;
        w = config.screen.width;
        h = config.screen.height;
    } else {
        x = 0;
        y = 0;
        w = config.window.width;
        h = config.window.height;
        config.window.xpos = 0;
        config.window.ypos = 0;
    }

    if (config.framebuffer.enable){
        config.framebuffer.xpos = 0;


        config.framebuffer.ypos = 0;
    } else {
        if (config.window.fullscreen) {
            config.framebuffer.xpos = (config.screen.width - config.window.width) / 2;
            config.framebuffer.ypos = (config.screen.height - config.window.height) / 2;
        } else {
            config.framebuffer.xpos = 0;
            config.framebuffer.ypos = 0;
        }

        config.framebuffer.width = config.window.width;
        config.framebuffer.height= config.window.height;
    }

    //create window
    int black = BlackPixel(display, DefaultScreen(display));
	window = XCreateSimpleWindow(display, DefaultRootWindow(display), x, y, w, h, 0, black, black);
	if (!window){
        LOG(LOG_ERROR, "Failed to create X window\n");
		return false;
    }

	//make fullscreen window
	if (config.window.fullscreen){
		struct {
			unsigned long   flags;
			unsigned long   functions;
			unsigned long   decorations;
			long            inputMode;
			unsigned long   status;
		} hints;

		hints.flags = 2;
		hints.decorations = 0;
		atomhint = XInternAtom(display, "_MOTIF_WM_HINTS", False);
		XChangeProperty(display, window, atomhint, atomhint, 32, PropModeReplace, (unsigned char *) &hints,5);
        XDefineCursor(display, window, None);
	}


	//set name and show window
    XStoreName (display, window, "gles2n64" );
	XMapWindow(display, window);
	XFlush(display);

    OGL.EGL.device = (EGLNativeDisplayType) display;
	OGL.EGL.handle = (void*) window;

	return true;
}
#endif

bool OGL_Start()
{
    EGLint nConfigs;
    GLint   success;

    // Already done in mupen:
    //SDL_Init(SDL_INIT_TIMER);

#ifdef SDL_WINDOW
	if (!OGL_SDL_Start())
        return false;
#endif

#ifdef X11_WINDOW
    if (config.window.enableX11)
    {
        if (!OGL_X11_Start())
            return false;
	}
	else
	{
        OGL.EGL.handle = NULL;
        OGL.EGL.device = 0;

        //shut down X11.
        system("sudo /etc/init.d/slim-init stop");
	}
#endif

#ifdef EMSCRIPTEN
	emscripten_set_canvas_size(config.window.width,config.window.height);
#endif

    LOG(LOG_VERBOSE, "EGL Context Creation\n");
    OGL.EGL.display = eglGetDisplay((EGLNativeDisplayType) OGL.EGL.device);
    if (OGL.EGL.display == EGL_NO_DISPLAY){
        LOG(LOG_ERROR, "EGL Display Get failed: %s \n", EGLErrorString());
        return FALSE;
    }

    if (!eglInitialize(OGL.EGL.display, &OGL.EGL.version_major, &OGL.EGL.version_minor)){
        LOG(LOG_ERROR, "EGL Display Initialize failed: %s \n", EGLErrorString());
        return FALSE;
    }


	if (!eglChooseConfig(OGL.EGL.display, ConfigAttribs, &OGL.EGL.config, 1, &nConfigs)){
        LOG(LOG_ERROR, "EGL Configuration failed: %s \n", EGLErrorString());
        return FALSE;
    } else if (nConfigs != 1){
        LOG(LOG_ERROR, "EGL Configuration failed: nconfig %i, %s \n", nConfigs, EGLErrorString());
        return FALSE;
    }

    OGL.EGL.surface = eglCreateWindowSurface(OGL.EGL.display, OGL.EGL.config, OGL.EGL.handle, NULL);
    if (OGL.EGL.surface == EGL_NO_SURFACE){
		LOG(LOG_WARNING, "EGL Surface Creation failed: %s will attempt without window... \n", EGLErrorString());
        OGL.EGL.surface = eglCreateWindowSurface(OGL.EGL.display, OGL.EGL.config, NULL, NULL);
        if (OGL.EGL.surface == EGL_NO_SURFACE){
            LOG(LOG_ERROR, "EGL Surface Creation failed: %s \n", EGLErrorString());
            return FALSE;
        }
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    OGL.EGL.context = eglCreateContext(OGL.EGL.display, OGL.EGL.config, EGL_NO_CONTEXT, ContextAttribs);
    if (OGL.EGL.context == EGL_NO_CONTEXT){
        LOG(LOG_ERROR, "EGL Context Creation failed: %s \n", EGLErrorString());
        return FALSE;
    }

    if (!eglMakeCurrent(OGL.EGL.display, OGL.EGL.surface, OGL.EGL.surface, OGL.EGL.context)){
        LOG(LOG_ERROR, "EGL Make Current failed: %s \n", EGLErrorString());
        return FALSE;
    };
    eglSwapInterval(OGL.EGL.display, config.verticalSync);

    OGL_InitStates();

    float depth = gDP.fillColor.z ;
    glDisable( GL_SCISSOR_TEST );
    glDepthMask( GL_TRUE );  
    glClearDepthf( 1.0f );  
    glClearColor( 0, 0, 0, 1 );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glFinish();
    eglSwapBuffers(OGL.EGL.display, OGL.EGL.surface);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glFinish();
    OGL_UpdateDepthUpdate();
    glEnable( GL_SCISSOR_TEST );
    glFinish();

    //create framebuffer
    if (config.framebuffer.enable)
    {
        LOG(LOG_VERBOSE, "Create offscreen framebuffer. \n");
        if (config.framebuffer.width == config.screen.width && config.framebuffer.height == config.screen.height)
        {
            LOG(LOG_WARNING, "There's no point in using a offscreen framebuffer when the window and screen dimensions are the same\n");
        }

        glGenFramebuffers(1, &OGL.framebuffer.fb);
        glGenRenderbuffers(1, &OGL.framebuffer.depth_buffer);
        glGenTextures(1, &OGL.framebuffer.color_buffer);
        glBindRenderbuffer(GL_RENDERBUFFER, OGL.framebuffer.depth_buffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24_OES, config.framebuffer.width, config.framebuffer.height);
        glBindTexture(GL_TEXTURE_2D, OGL.framebuffer.color_buffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, config.framebuffer.width, config.framebuffer.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, OGL.framebuffer.fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, OGL.framebuffer.color_buffer, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, OGL.framebuffer.depth_buffer);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            LOG(LOG_ERROR, "Incomplete Framebuffer Object: ");
            switch(glCheckFramebufferStatus(GL_FRAMEBUFFER))
            {
                case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                    printf("Incomplete Attachment. \n"); break;
                case GL_FRAMEBUFFER_UNSUPPORTED:
                    printf("Framebuffer Unsupported. \n"); break;
                case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
                    printf("Incomplete Dimensions. \n"); break;
#ifndef EMSCRIPTEN
                case GL_FRAMEBUFFER_INCOMPLETE_FORMATS:
                    printf("Incomplete Formats. \n"); break;
#endif
            }
            config.framebuffer.enable = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    //check extensions
    if ((config.texture.maxAnisotropy>0) && !OGL_IsExtSupported("GL_EXT_texture_filter_anistropic"))
    {
        LOG(LOG_WARNING, "Anistropic Filtering is not supported.\n");
        config.texture.maxAnisotropy = 0;
    }

    float f = 0;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &f);
    if (config.texture.maxAnisotropy > ((int)f))
    {
        LOG(LOG_WARNING, "Clamping max anistropy to %ix.\n", (int)f);
        config.texture.maxAnisotropy = (int)f;
    }

    //Print some info
    EGLint attrib;
    LOG(LOG_VERBOSE, "Width: %i Height:%i \n", config.framebuffer.width, config.framebuffer.height);
#ifndef EMSCRIPTEN
    eglGetConfigAttrib(OGL.EGL.display, OGL.EGL.config, EGL_DEPTH_SIZE, &attrib);
    LOG(LOG_VERBOSE, "[gles2n64]: Depth Size: %i \n", attrib);
    eglGetConfigAttrib(OGL.EGL.display, OGL.EGL.config, EGL_BUFFER_SIZE, &attrib);
    LOG(LOG_VERBOSE, "[gles2n64]: Color Buffer Size: %i \n", attrib);
#endif
    LOG(LOG_VERBOSE, "[gles2n64]: Enable Runfast... \n");

    OGL_EnableRunfast();
    OGL_UpdateScale();

    //We must have a shader bound before binding any textures:
    ShaderCombiner_Init();
    ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0));
    ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1));

    TextureCache_Init();

    memset(OGL.triangles.vertices, 0, VERTBUFF_SIZE * sizeof(SPVertex));
    memset(OGL.triangles.elements, 0, ELEMBUFF_SIZE * sizeof(GLubyte));
    OGL.triangles.num = 0;

#ifdef __TRIBUFFER_OPT
    __indexmap_init();
#endif

    OGL.frameSkipped = 0;
    for(int i = 0; i < OGL_FRAMETIME_NUM; i++) OGL.frameTime[i] = 0;

    OGL.renderingToTexture = false;
    OGL.renderState = RS_NONE;
    gSP.changed = gDP.changed = 0xFFFFFFFF;
    VI.displayNum = 0;
    glGetError();

    return TRUE;
}

void OGL_Stop()
{
    LOG(LOG_MINIMAL, "Stopping OpenGL\n");

#ifdef WIN32
    SDL_QuitSubSystem( SDL_INIT_VIDEO );
#endif

    if (config.framebuffer.enable)
    {
        glDeleteFramebuffers(1, &OGL.framebuffer.fb);
        glDeleteTextures(1, &OGL.framebuffer.color_buffer);
        glDeleteRenderbuffers(1, &OGL.framebuffer.depth_buffer);
    }

    glDeleteShader(OGL.defaultFragShader);
    glDeleteShader(OGL.defaultVertShader);
    glDeleteProgram(OGL.defaultProgram);

    ShaderCombiner_Destroy();
    TextureCache_Destroy();

#ifdef X11_WINDOW
    if (config.window.enableX11)
        XDestroyWindow((Display*) OGL.EGL.display, (Window) OGL.EGL.handle);
    else
        system("sudo /etc/init.d/slim-init start");
#endif

	eglMakeCurrent(OGL.EGL.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(OGL.EGL.display, OGL.EGL.surface);
 	eglDestroyContext(OGL.EGL.display, OGL.EGL.context);
   	eglTerminate(OGL.EGL.display);
}

void OGL_UpdateCullFace()
{
    if (config.enableFaceCulling && (gSP.geometryMode & G_CULL_BOTH))
    {
        glEnable( GL_CULL_FACE );
        if ((gSP.geometryMode & G_CULL_BACK) && (gSP.geometryMode & G_CULL_FRONT))
            glCullFace(GL_FRONT_AND_BACK);
        else if (gSP.geometryMode & G_CULL_BACK)
            glCullFace(GL_BACK);
        else
            glCullFace(GL_FRONT);
    }
    else
        glDisable(GL_CULL_FACE);
}

void OGL_UpdateViewport()
{
    int x, y, w, h;
    x = config.framebuffer.xpos + (int)(gSP.viewport.x * OGL.scaleX);
    y = config.framebuffer.ypos + (int)((VI.height - (gSP.viewport.y + gSP.viewport.height)) * OGL.scaleY);
    w = (int)(gSP.viewport.width * OGL.scaleX);
    h = (int)(gSP.viewport.height * OGL.scaleY);

    glViewport(x, y, w, h);
}

void OGL_UpdateScissor()
{
    int x, y, w, h;
    x = config.framebuffer.xpos + (int)(gDP.scissor.ulx * OGL.scaleX);
    y = config.framebuffer.ypos + (int)((VI.height - gDP.scissor.lry) * OGL.scaleY);
    w = (int)((gDP.scissor.lrx - gDP.scissor.ulx) * OGL.scaleX);
    h = (int)((gDP.scissor.lry - gDP.scissor.uly) * OGL.scaleY);
    glScissor(x, y, w, h);
}

//copied from RICE VIDEO
void OGL_SetBlendMode()
{

    u32 blender = gDP.otherMode.l >> 16;
    u32 blendmode_1 = blender&0xcccc;
    u32 blendmode_2 = blender&0x3333;

    glEnable(GL_BLEND);
    switch(gDP.otherMode.cycleType)
    {
        case G_CYC_FILL:
            glDisable(GL_BLEND);
            break;

        case G_CYC_COPY:
            glBlendFunc(GL_ONE, GL_ZERO);
            break;

        case G_CYC_2CYCLE:
            if (gDP.otherMode.forceBlender && gDP.otherMode.depthCompare)
            {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                break;
            }

            switch(blendmode_1+blendmode_2)
            {
                case BLEND_PASS+(BLEND_PASS>>2):    // In * 0 + In * 1
                case BLEND_FOG_APRIM+(BLEND_PASS>>2):
                case BLEND_FOG_MEM_FOG_MEM + (BLEND_OPA>>2):
                case BLEND_FOG_APRIM + (BLEND_OPA>>2):
                case BLEND_FOG_ASHADE + (BLEND_OPA>>2):
                case BLEND_BI_AFOG + (BLEND_OPA>>2):
                case BLEND_FOG_ASHADE + (BLEND_NOOP>>2):
                case BLEND_NOOP + (BLEND_OPA>>2):
                case BLEND_NOOP4 + (BLEND_NOOP>>2):
                case BLEND_FOG_ASHADE+(BLEND_PASS>>2):
                case BLEND_FOG_3+(BLEND_PASS>>2):
                    glDisable(GL_BLEND);
                    break;

                case BLEND_PASS+(BLEND_OPA>>2):
                    if (gDP.otherMode.cvgXAlpha && gDP.otherMode.alphaCvgSel)
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    else
                        glDisable(GL_BLEND);
                    break;

                case BLEND_PASS + (BLEND_XLU>>2):
                case BLEND_FOG_ASHADE + (BLEND_XLU>>2):
                case BLEND_FOG_APRIM + (BLEND_XLU>>2):
                case BLEND_FOG_MEM_FOG_MEM + (BLEND_PASS>>2):
                case BLEND_XLU + (BLEND_XLU>>2):
                case BLEND_BI_AFOG + (BLEND_XLU>>2):
                case BLEND_XLU + (BLEND_FOG_MEM_IN_MEM>>2):
                case BLEND_PASS + (BLEND_FOG_MEM_IN_MEM>>2):
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    break;

                case BLEND_FOG_ASHADE+0x0301:
                    glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
                    break;

                case 0x0c08+0x1111:
                    glBlendFunc(GL_ZERO, GL_DST_ALPHA);
                    break;

                default:
                    if (blendmode_2 == (BLEND_PASS>>2))
                        glDisable(GL_BLEND);
                    else
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    break;
                }
                break;

    default:

        if (gDP.otherMode.forceBlender && gDP.otherMode.depthCompare && blendmode_1 != BLEND_FOG_ASHADE )
        {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }

        switch (blendmode_1)
        {
            case BLEND_XLU:
            case BLEND_BI_AIN:
            case BLEND_FOG_MEM:
            case BLEND_FOG_MEM_IN_MEM:
            case BLEND_BLENDCOLOR:
            case 0x00c0:
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                break;

            case BLEND_MEM_ALPHA_IN:
                glBlendFunc(GL_ZERO, GL_DST_ALPHA);
                break;

            case BLEND_OPA:
                //if( options.enableHackForGames == HACK_FOR_MARIO_TENNIS )
                //{
                //   glBlendFunc(BLEND_SRCALPHA, BLEND_INVSRCALPHA);
                //}

                glDisable(GL_BLEND);
                break;

            case BLEND_PASS:
            case BLEND_NOOP:
            case BLEND_FOG_ASHADE:
            case BLEND_FOG_MEM_3:
            case BLEND_BI_AFOG:
                glDisable(GL_BLEND);
                break;

            case BLEND_FOG_APRIM:
                glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_ZERO);
                break;

            case BLEND_NOOP3:
            case BLEND_NOOP5:
            case BLEND_MEM:
                glBlendFunc(GL_ZERO, GL_ONE);
                break;

            default:
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

}

void OGL_UpdateStates()
{
    if (gDP.otherMode.cycleType == G_CYC_COPY)
        ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0));
    else if (gDP.otherMode.cycleType == G_CYC_FILL)
        ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1));
    else
        ShaderCombiner_Set(gDP.combine.mux);

#ifdef SHADER_TEST
    ProgramSwaps += scProgramChanged;
#endif

    if (gSP.changed & CHANGED_GEOMETRYMODE)
    {
        OGL_UpdateCullFace();

        if (gSP.geometryMode & G_ZBUFFER)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);

    }

    if (gDP.changed & CHANGED_CONVERT)
    {
        SC_SetUniform1f(uK4, gDP.convert.k4);
        SC_SetUniform1f(uK5, gDP.convert.k5);
    }

    if (gDP.changed & CHANGED_RENDERMODE || gDP.changed & CHANGED_CYCLETYPE)
    {
        if (gDP.otherMode.cycleType == G_CYC_1CYCLE || gDP.otherMode.cycleType == G_CYC_2CYCLE)
        {
            //glDepthFunc((gDP.otherMode.depthCompare) ? GL_GEQUAL : GL_ALWAYS);
            glDepthFunc((gDP.otherMode.depthCompare) ? GL_LESS : GL_ALWAYS);
            glDepthMask((gDP.otherMode.depthUpdate) ? GL_TRUE : GL_FALSE);

            if (gDP.otherMode.depthMode == ZMODE_DEC)
                glEnable(GL_POLYGON_OFFSET_FILL);
           else
                glDisable(GL_POLYGON_OFFSET_FILL);
        }
        else
        {
            glDepthFunc(GL_ALWAYS);
            glDepthMask(GL_FALSE);
        }
    }

    if ((gDP.changed & CHANGED_BLENDCOLOR) || (gDP.changed & CHANGED_RENDERMODE))
        SC_SetUniform1f(uAlphaRef, (gDP.otherMode.cvgXAlpha) ? 0.5f : gDP.blendColor.a);

    if (gDP.changed & CHANGED_SCISSOR)
        OGL_UpdateScissor();

    if (gSP.changed & CHANGED_VIEWPORT)
        OGL_UpdateViewport();

    if (gSP.changed & CHANGED_FOGPOSITION)
    {
        SC_SetUniform1f(uFogMultiplier, (float) gSP.fog.multiplier / 255.0f);
        SC_SetUniform1f(uFogOffset, (float) gSP.fog.offset / 255.0f);
    }

    if (gSP.changed & CHANGED_TEXTURESCALE)
    {
        if (scProgramCurrent->usesT0 || scProgramCurrent->usesT1)
            SC_SetUniform2f(uTexScale, gSP.texture.scales, gSP.texture.scalet);
    }

    if ((gSP.changed & CHANGED_TEXTURE) || (gDP.changed & CHANGED_TILE) || (gDP.changed & CHANGED_TMEM))
    {
        //For some reason updating the texture cache on the first frame of LOZ:OOT causes a NULL Pointer exception...
        if (scProgramCurrent)
        {
            if (scProgramCurrent->usesT0)
            {
#ifdef TEXTURECACHE_TEST
                unsigned t = SDL_GetTicks();
                TextureCache_Update(0);
                TextureCacheTime += (SDL_GetTicks() - t);
#else
                TextureCache_Update(0);
#endif
                SC_ForceUniform2f(uTexOffset[0], gSP.textureTile[0]->fuls, gSP.textureTile[0]->fult);
                SC_ForceUniform2f(uCacheShiftScale[0], cache.current[0]->shiftScaleS, cache.current[0]->shiftScaleT);
                SC_ForceUniform2f(uCacheScale[0], cache.current[0]->scaleS, cache.current[0]->scaleT);
                SC_ForceUniform2f(uCacheOffset[0], cache.current[0]->offsetS, cache.current[0]->offsetT);
            }
            //else TextureCache_ActivateDummy(0);

            //Note: enabling dummies makes some F-zero X textures flicker.... strange.

            if (scProgramCurrent->usesT1)
            {
#ifdef TEXTURECACHE_TEST
                unsigned t = SDL_GetTicks();
                TextureCache_Update(1);
                TextureCacheTime += (SDL_GetTicks() - t);
#else
                TextureCache_Update(1);
#endif
                SC_ForceUniform2f(uTexOffset[1], gSP.textureTile[1]->fuls, gSP.textureTile[1]->fult);
                SC_ForceUniform2f(uCacheShiftScale[1], cache.current[1]->shiftScaleS, cache.current[1]->shiftScaleT);
                SC_ForceUniform2f(uCacheScale[1], cache.current[1]->scaleS, cache.current[1]->scaleT);
                SC_ForceUniform2f(uCacheOffset[1], cache.current[1]->offsetS, cache.current[1]->offsetT);
            }
            //else TextureCache_ActivateDummy(1);
        }
    }

    if ((gDP.changed & CHANGED_FOGCOLOR) && config.enableFog)
        SC_SetUniform4fv(uFogColor, &gDP.fogColor.r );

    if (gDP.changed & CHANGED_ENV_COLOR)
        SC_SetUniform4fv(uEnvColor, &gDP.envColor.r);

    if (gDP.changed & CHANGED_PRIM_COLOR)
    {
        SC_SetUniform4fv(uPrimColor, &gDP.primColor.r);
        SC_SetUniform1f(uPrimLODFrac, gDP.primColor.l);
    }

    if ((gDP.changed & CHANGED_RENDERMODE) || (gDP.changed & CHANGED_CYCLETYPE))
    {
#ifndef OLD_BLENDMODE
        OGL_SetBlendMode();
#else
        if ((gDP.otherMode.forceBlender) &&
            (gDP.otherMode.cycleType != G_CYC_COPY) &&
            (gDP.otherMode.cycleType != G_CYC_FILL) &&
            !(gDP.otherMode.alphaCvgSel))
        {
            glEnable( GL_BLEND );

            switch (gDP.otherMode.l >> 16)
            {
                case 0x0448: // Add
                case 0x055A:
                    glBlendFunc( GL_ONE, GL_ONE );
                    break;
                case 0x0C08: // 1080 Sky
                case 0x0F0A: // Used LOTS of places
                    glBlendFunc( GL_ONE, GL_ZERO );
                    break;

                case 0x0040: // Fzero
                case 0xC810: // Blends fog
                case 0xC811: // Blends fog
                case 0x0C18: // Standard interpolated blend
                case 0x0C19: // Used for antialiasing
                case 0x0050: // Standard interpolated blend
                case 0x0055: // Used for antialiasing
                    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                    break;

                case 0x0FA5: // Seems to be doing just blend color - maybe combiner can be used for this?
                case 0x5055: // Used in Paper Mario intro, I'm not sure if this is right...
                    glBlendFunc( GL_ZERO, GL_ONE );
                    break;

                default:
                    LOG(LOG_VERBOSE, "Unhandled blend mode=%x", gDP.otherMode.l >> 16);
                    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                    break;
            }
        }
        else
        {
            glDisable( GL_BLEND );
        }

        if (gDP.otherMode.cycleType == G_CYC_FILL)
        {
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            glEnable( GL_BLEND );
        }
#endif
    }

    gDP.changed &= CHANGED_TILE | CHANGED_TMEM;
    gSP.changed &= CHANGED_TEXTURE | CHANGED_MATRIX;
}

void OGL_DrawTriangle(SPVertex *vertices, int v0, int v1, int v2)
{

}

void OGL_AddTriangle(int v0, int v1, int v2)
{
    OGL.triangles.elements[OGL.triangles.num++] = v0;
    OGL.triangles.elements[OGL.triangles.num++] = v1;
    OGL.triangles.elements[OGL.triangles.num++] = v2;
}

void OGL_SetColorArray()
{
    if (scProgramCurrent->usesCol)
        glEnableVertexAttribArray(SC_COLOR);
    else
        glDisableVertexAttribArray(SC_COLOR);
}

void OGL_SetTexCoordArrays()
{
    if (scProgramCurrent->usesT0)
        glEnableVertexAttribArray(SC_TEXCOORD0);
    else
        glDisableVertexAttribArray(SC_TEXCOORD0);

    if (scProgramCurrent->usesT1)
        glEnableVertexAttribArray(SC_TEXCOORD1);
    else
        glDisableVertexAttribArray(SC_TEXCOORD1);
}

void OGL_DrawTriangles()
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering)
    {
        OGL.triangles.num = 0;
        return;
    }

    if (OGL.triangles.num == 0) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    if (gSP.changed || gDP.changed)
        OGL_UpdateStates();

    if (OGL.renderState != RS_TRIANGLE || scProgramChanged)
    {
        OGL_SetColorArray();
        OGL_SetTexCoordArrays();
        glDisableVertexAttribArray(SC_TEXCOORD1);
        SC_ForceUniform1f(uRenderState, RS_TRIANGLE);
    }

    if (OGL.renderState != RS_TRIANGLE)
    {
#ifdef RENDERSTATE_TEST
        StateChanges++;
#endif
        glVertexAttribPointer(SC_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].x);
        glVertexAttribPointer(SC_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].r);
        glVertexAttribPointer(SC_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].s);

        OGL_UpdateCullFace();
        OGL_UpdateViewport();
        glEnable(GL_SCISSOR_TEST);
        OGL.renderState = RS_TRIANGLE;
    }

    glDrawElements(GL_TRIANGLES, OGL.triangles.num, GL_UNSIGNED_BYTE, OGL.triangles.elements);
    OGL.triangles.num = 0;

#ifdef __TRIBUFFER_OPT
    __indexmap_clear();
#endif
}

void OGL_DrawLine(int v0, int v1, float width )
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    if (gSP.changed || gDP.changed)
        OGL_UpdateStates();

    if (OGL.renderState != RS_LINE || scProgramChanged)
    {
#ifdef RENDERSTATE_TEST
        StateChanges++;
#endif
        OGL_SetColorArray();
        glDisableVertexAttribArray(SC_TEXCOORD0);
        glDisableVertexAttribArray(SC_TEXCOORD1);
        glVertexAttribPointer(SC_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].x);
        glVertexAttribPointer(SC_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].r);

        SC_ForceUniform1f(uRenderState, RS_LINE);
        OGL_UpdateCullFace();
        OGL_UpdateViewport();
        OGL.renderState = RS_LINE;
    }

    unsigned short elem[2];
    elem[0] = v0;
    elem[1] = v1;
    glLineWidth( width * OGL.scaleX );
    glDrawElements(GL_LINES, 2, GL_UNSIGNED_SHORT, elem);
}

void OGL_DrawRect( int ulx, int uly, int lrx, int lry, float *color)
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    if (gSP.changed || gDP.changed)
        OGL_UpdateStates();

    if (OGL.renderState != RS_RECT || scProgramChanged)
    {
        glDisableVertexAttribArray(SC_COLOR);
        glDisableVertexAttribArray(SC_TEXCOORD0);
        glDisableVertexAttribArray(SC_TEXCOORD1);
        SC_ForceUniform1f(uRenderState, RS_RECT);
    }

    if (OGL.renderState != RS_RECT)
    {
#ifdef RENDERSTATE_TEST
        StateChanges++;
#endif
        glVertexAttrib4f(SC_POSITION, 0, 0, gSP.viewport.nearz, 1.0);
        glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].x);
        OGL.renderState = RS_RECT;
    }

    glViewport(config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height );
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);

    OGL.rect[0].x = (float) ulx * (2.0f * VI.rwidth) - 1.0;
    OGL.rect[0].y = (float) uly * (-2.0f * VI.rheight) + 1.0;
    OGL.rect[1].x = (float) (lrx+1) * (2.0f * VI.rwidth) - 1.0;
    OGL.rect[1].y = OGL.rect[0].y;
    OGL.rect[2].x = OGL.rect[0].x;
    OGL.rect[2].y = (float) (lry+1) * (-2.0f * VI.rheight) + 1.0;
    OGL.rect[3].x = OGL.rect[1].x;
    OGL.rect[3].y = OGL.rect[2].y;

    glVertexAttrib4fv(SC_COLOR, color);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEnable(GL_SCISSOR_TEST);
    OGL_UpdateViewport();

}

void OGL_DrawTexturedRect( float ulx, float uly, float lrx, float lry, float uls, float ult, float lrs, float lrt, bool flip )
{
    if (config.hackBanjoTooie)
    {
        if (gDP.textureImage.width == gDP.colorImage.width &&
            gDP.textureImage.format == G_IM_FMT_CI &&
            gDP.textureImage.size == G_IM_SIZ_8b)
        {
            return;
        }
    }

    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    if (gSP.changed || gDP.changed)
        OGL_UpdateStates();

    if (OGL.renderState != RS_TEXTUREDRECT || scProgramChanged)
    {
        glDisableVertexAttribArray(SC_COLOR);
        OGL_SetTexCoordArrays();
        SC_ForceUniform1f(uRenderState, RS_TEXTUREDRECT);
    }

    if (OGL.renderState != RS_TEXTUREDRECT)
    {
#ifdef RENDERSTATE_TEST
        StateChanges++;
#endif
        glVertexAttrib4f(SC_COLOR, 0, 0, 0, 0);
        glVertexAttrib4f(SC_POSITION, 0, 0, (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz, 1.0);
        glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].x);
        glVertexAttribPointer(SC_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].s0);
        glVertexAttribPointer(SC_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].s1);
        OGL.renderState = RS_TEXTUREDRECT;
    }

    glViewport(config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
    glDisable(GL_CULL_FACE);

    OGL.rect[0].x = (float) ulx * (2.0f * VI.rwidth) - 1.0f;
    OGL.rect[0].y = (float) uly * (-2.0f * VI.rheight) + 1.0f;
    OGL.rect[1].x = (float) (lrx) * (2.0f * VI.rwidth) - 1.0f;
    OGL.rect[1].y = OGL.rect[0].y;
    OGL.rect[2].x = OGL.rect[0].x;
    OGL.rect[2].y = (float) (lry) * (-2.0f * VI.rheight) + 1.0f;
    OGL.rect[3].x = OGL.rect[1].x;
    OGL.rect[3].y = OGL.rect[2].y;

    if (scProgramCurrent->usesT0 && cache.current[0] && gSP.textureTile[0])
    {
        OGL.rect[0].s0 = uls * cache.current[0]->shiftScaleS - gSP.textureTile[0]->fuls;
        OGL.rect[0].t0 = ult * cache.current[0]->shiftScaleT - gSP.textureTile[0]->fult;
        OGL.rect[3].s0 = (lrs + 1.0f) * cache.current[0]->shiftScaleS - gSP.textureTile[0]->fuls;
        OGL.rect[3].t0 = (lrt + 1.0f) * cache.current[0]->shiftScaleT - gSP.textureTile[0]->fult;

        if ((cache.current[0]->maskS) && !(cache.current[0]->mirrorS) && (fmod( OGL.rect[0].s0, cache.current[0]->width ) == 0.0f))
        {
            OGL.rect[3].s0 -= OGL.rect[0].s0;
            OGL.rect[0].s0 = 0.0f;
        }

        if ((cache.current[0]->maskT)  && !(cache.current[0]->mirrorT) && (fmod( OGL.rect[0].t0, cache.current[0]->height ) == 0.0f))
        {
            OGL.rect[3].t0 -= OGL.rect[0].t0;
            OGL.rect[0].t0 = 0.0f;
        }

        glActiveTexture( GL_TEXTURE0);
        if ((OGL.rect[0].s0 >= 0.0f) && (OGL.rect[3].s0 <= cache.current[0]->width))
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );

        if ((OGL.rect[0].t0 >= 0.0f) && (OGL.rect[3].t0 <= cache.current[0]->height))
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

        OGL.rect[0].s0 *= cache.current[0]->scaleS;
        OGL.rect[0].t0 *= cache.current[0]->scaleT;
        OGL.rect[3].s0 *= cache.current[0]->scaleS;
        OGL.rect[3].t0 *= cache.current[0]->scaleT;
    }

    if (scProgramCurrent->usesT1 && cache.current[1] && gSP.textureTile[1])
    {
        OGL.rect[0].s1 = uls * cache.current[1]->shiftScaleS - gSP.textureTile[1]->fuls;
        OGL.rect[0].t1 = ult * cache.current[1]->shiftScaleT - gSP.textureTile[1]->fult;
        OGL.rect[3].s1 = (lrs + 1.0f) * cache.current[1]->shiftScaleS - gSP.textureTile[1]->fuls;
        OGL.rect[3].t1 = (lrt + 1.0f) * cache.current[1]->shiftScaleT - gSP.textureTile[1]->fult;

        if ((cache.current[1]->maskS) && (fmod( OGL.rect[0].s1, cache.current[1]->width ) == 0.0f) && !(cache.current[1]->mirrorS))
        {
            OGL.rect[3].s1 -= OGL.rect[0].s1;
            OGL.rect[0].s1 = 0.0f;
        }

        if ((cache.current[1]->maskT) && (fmod( OGL.rect[0].t1, cache.current[1]->height ) == 0.0f) && !(cache.current[1]->mirrorT))
        {
            OGL.rect[3].t1 -= OGL.rect[0].t1;
            OGL.rect[0].t1 = 0.0f;
        }

        glActiveTexture( GL_TEXTURE1);
        if ((OGL.rect[0].s1 == 0.0f) && (OGL.rect[3].s1 <= cache.current[1]->width))
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );

        if ((OGL.rect[0].t1 == 0.0f) && (OGL.rect[3].t1 <= cache.current[1]->height))
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

        OGL.rect[0].s1 *= cache.current[1]->scaleS;
        OGL.rect[0].t1 *= cache.current[1]->scaleT;
        OGL.rect[3].s1 *= cache.current[1]->scaleS;
        OGL.rect[3].t1 *= cache.current[1]->scaleT;
    }

    if ((gDP.otherMode.cycleType == G_CYC_COPY) && !config.texture.forceBilinear)
    {
        glActiveTexture(GL_TEXTURE0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    }

    if (flip)
    {
        OGL.rect[1].s0 = OGL.rect[0].s0;
        OGL.rect[1].t0 = OGL.rect[3].t0;
        OGL.rect[1].s1 = OGL.rect[0].s1;
        OGL.rect[1].t1 = OGL.rect[3].t1;
        OGL.rect[2].s0 = OGL.rect[3].s0;
        OGL.rect[2].t0 = OGL.rect[0].t0;
        OGL.rect[2].s1 = OGL.rect[3].s1;
        OGL.rect[2].t1 = OGL.rect[0].t1;
    }
    else
    {
        OGL.rect[1].s0 = OGL.rect[3].s0;
        OGL.rect[1].t0 = OGL.rect[0].t0;
        OGL.rect[1].s1 = OGL.rect[3].s1;
        OGL.rect[1].t1 = OGL.rect[0].t1;
        OGL.rect[2].s0 = OGL.rect[0].s0;
        OGL.rect[2].t0 = OGL.rect[3].t0;
        OGL.rect[2].s1 = OGL.rect[0].s1;
        OGL.rect[2].t1 = OGL.rect[3].t1;
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    OGL_UpdateViewport();
}

void OGL_ClearDepthBuffer()
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    //float depth = 1.0 - (gDP.fillColor.z / ((float)0x3FFF));
    float depth = gDP.fillColor.z ;

    glDisable( GL_SCISSOR_TEST );
    glDepthMask( GL_TRUE ); 
    glClearDepthf( 1.0f );  
    glClearColor( 0, 0, 0, 1 );
    glClear( GL_DEPTH_BUFFER_BIT );
    OGL_UpdateDepthUpdate();
    glEnable( GL_SCISSOR_TEST );
}

void OGL_ClearColorBuffer( float *color )
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    glScissor(config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
    glClearColor( color[0], color[1], color[2], color[3] );
    glClear( GL_COLOR_BUFFER_BIT );
    OGL_UpdateScissor();

}

static void OGL_png_error(png_structp png_write, const char *message)
{
    printf("PNG Error: %s\n", message);
}

static void OGL_png_warn(png_structp png_write, const char *message)
{
    printf("PNG Warning: %s\n", message);
}

void OGL_SaveScreenshot()
{
    // start by getting the base file path
    char filepath[2048], filename[2048];
    filepath[0] = 0;
    filename[0] = 0;
    strcpy(filepath, screenDirectory);
    if (strlen(filepath) > 0 && filepath[strlen(filepath)-1] != '/')
        strcat(filepath, "/");
    strcat(filepath, "mupen64");
    // look for a file
    int i;
    for (i = 0; i < 100; i++)
    {
        sprintf(filename, "%s_%03i.png", filepath, i);
        FILE *pFile = fopen(filename, "r");
        if (pFile == NULL)
            break;
        fclose(pFile);
    }
    if (i == 100) return;
    // allocate PNG structures
    png_structp png_write = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, OGL_png_error, OGL_png_warn);
    if (!png_write)
    {
        printf("Error creating PNG write struct.\n");
        return;
    }
    png_infop png_info = png_create_info_struct(png_write);
    if (!png_info)
    {
        png_destroy_write_struct(&png_write, (png_infopp)NULL);
        printf("Error creating PNG info struct.\n");
        return;
    }
    // Set the jumpback
    if (setjmp(png_jmpbuf(png_write)))
    {
        png_destroy_write_struct(&png_write, &png_info);
        printf("Error calling setjmp()\n");
        return;
    }
    // open the file to write
    FILE *savefile = fopen(filename, "wb");
    if (savefile == NULL)
    {
        printf("Error opening '%s' to save screenshot.\n", filename);
        return;
    }
    // give the file handle to the PNG compressor
    png_init_io(png_write, savefile);

    // read pixel data from OpenGL
    char *pixels;
    int width, height;
    OGL_ReadScreen(pixels, &width, &height );

    // set the info
    png_set_IHDR(png_write, png_info, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    // lock the screen, get a pointer and row pitch
    long pitch = width * 3;
    // allocate row pointers and scale each row to 24-bit color
    png_byte **row_pointers;
    row_pointers = (png_byte **) malloc(height * sizeof(png_bytep));
    for (int i = 0; i < height; i++)
    {
        row_pointers[i] = (png_byte *) (pixels + (height - 1 - i) * pitch);
    }
    // set the row pointers
    png_set_rows(png_write, png_info, row_pointers);
    // write the picture to disk
    png_write_png(png_write, png_info, 0, NULL);
    // free memory
    free(row_pointers);
    png_destroy_write_struct(&png_write, &png_info);
    free(pixels);
    // all done
}

int OGL_CheckError()
{
    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
    {
        printf("GL Error: ");
        switch(e)
        {
            case GL_INVALID_ENUM:   printf("INVALID ENUM"); break;
            case GL_INVALID_VALUE:  printf("INVALID VALUE"); break;
            case GL_INVALID_OPERATION:  printf("INVALID OPERATION"); break;
            case GL_OUT_OF_MEMORY:  printf("OUT OF MEMORY"); break;
        }
        printf("\n");
        return 1;
    }
    return 0;
}

void OGL_UpdateFrameTime()
{
    unsigned ticks = SDL_GetTicks();
    static unsigned lastFrameTicks = 0;
    for(int i = OGL_FRAMETIME_NUM-1; i > 0; i--) OGL.frameTime[i] = OGL.frameTime[i-1];
    OGL.frameTime[0] = ticks - lastFrameTicks;
    lastFrameTicks = ticks;
}

void OGL_SwapBuffers()
{
    //OGL_DrawTriangles();
    scProgramChanged = 0;
    static int frames = 0;
    static unsigned lastTicks = 0;
    unsigned ticks = SDL_GetTicks();

    frames++;
    if (ticks >= (lastTicks + 1000))
    {

        float fps = 1000.0f * (float) frames / (ticks - lastTicks);
        LOG(LOG_MINIMAL, "fps = %.2f \n", fps);
        LOG(LOG_MINIMAL, "skipped frame = %i of %i \n", OGL.frameSkipped, frames + OGL.frameSkipped);

        OGL.frameSkipped = 0;

#ifdef BATCH_TEST
        LOG(LOG_MINIMAL, "time spent in draw calls per frame = %.2f ms\n", (float)TotalDrawTime / frames);
        LOG(LOG_MINIMAL, "average draw calls per frame = %.0f\n", (float)TotalDrawCalls / frames);
        LOG(LOG_MINIMAL, "average vertices per draw call = %.2f\n", (float)TotalTriangles / TotalDrawCalls);
        TotalDrawCalls = 0;
        TotalTriangles = 0;
        TotalDrawTime = 0;
#endif

#ifdef SHADER_TEST
        LOG(LOG_MINIMAL, "average shader changes per frame = %f\n", (float)ProgramSwaps / frames);
        ProgramSwaps = 0;
#endif

#ifdef TEXTURECACHE_TEST
        LOG(LOG_MINIMAL, "texture cache time per frame: %.2f ms\n", (float)TextureCacheTime/ frames);
        LOG(LOG_MINIMAL, "texture cache per frame: hits=%.2f misses=%.2f\n", (float)cache.hits / frames,
                (float)cache.misses / frames);
        cache.hits = cache.misses = 0;
        TextureCacheTime = 0;

#endif
        frames = 0;
        lastTicks = ticks;
    }


#ifdef PROFILE_GBI
    u32 profileTicks = SDL_GetTicks();
    static u32 profileLastTicks = 0;
    if (profileTicks >= (profileLastTicks + 5000))
    {
        LOG(LOG_MINIMAL, "GBI PROFILE DATA: %i ms \n", profileTicks - profileLastTicks);
        LOG(LOG_MINIMAL, "=========================================================\n");
        GBI_ProfilePrint(stdout);
        LOG(LOG_MINIMAL, "=========================================================\n");
        GBI_ProfileReset();
        profileLastTicks = profileTicks;
    }
#endif

    // if emulator defined a render callback function, call it before buffer swap
    if (renderCallback) (*renderCallback)(1);

    if (config.framebuffer.enable)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor( 0, 0, 0, 1 );
        glClear( GL_COLOR_BUFFER_BIT );

        glUseProgram(OGL.defaultProgram);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glViewport(config.window.xpos, config.window.ypos, config.window.width, config.window.height);

        static const float vert[] =
        {
            -1.0, -1.0, +0.0, +0.0,
            +1.0, -1.0, +1.0, +0.0,
            -1.0, +1.0, +0.0, +1.0,
            +1.0, +1.0, +1.0, +1.0
        };

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, OGL.framebuffer.color_buffer);
        if (config.framebuffer.bilinear)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (float*)vert);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (float*)vert + 2);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        eglSwapBuffers(OGL.EGL.display, OGL.EGL.surface);

        glBindFramebuffer(GL_FRAMEBUFFER, OGL.framebuffer.fb);
        OGL_UpdateViewport();
        if (scProgramCurrent) glUseProgram(scProgramCurrent->program);
        OGL.renderState = RS_NONE;
    }
    else
    {
        eglSwapBuffers(OGL.EGL.display, OGL.EGL.surface);
    }

    OGL.screenUpdate = false;

    if (config.forceBufferClear)
    {
    	float depth = gDP.fillColor.z ;
    	glDisable( GL_SCISSOR_TEST );
    	glDepthMask( GL_TRUE );  
    	glClearDepthf( 1.0f );  
    	glClearColor( 0, 0, 0, 1 );
    	glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );
    	OGL_UpdateDepthUpdate();
    	glEnable( GL_SCISSOR_TEST );
    }

}

void OGL_ReadScreen( void *dest, int *width, int *height )
{
    if (width)
        *width = config.framebuffer.width;
    if (height)
        *height = config.framebuffer.height;

    if (dest == NULL)
        return;

    glReadPixels( config.framebuffer.xpos, config.framebuffer.ypos,
            config.framebuffer.width, config.framebuffer.height,
            GL_RGBA, GL_UNSIGNED_BYTE, dest );
}

