#/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# *   Mupen64plus - Makefile                                                *
# *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
# *   Copyright (C) 2007-2008 DarkJeztr Tillin9 Richard42                   *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU General Public License as published by  *
# *   the Free Software Foundation; either version 2 of the License, or     *
# *   (at your option) any later version.                                   *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU General Public License for more details.                          *
# *                                                                         *
# *   You should have received a copy of the GNU General Public License     *
# *   along with this program; if not, write to the                         *
# *   Free Software Foundation, Inc.,                                       *
# *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
# * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
# Makefile for glN64 in Mupen64plus.

ARCH = ARM
OS = LINUX
#ARCH = X86
#OS = WIN32

CFLAGS  = -Wall -pipe -O3
LDFLAGS =

ifeq ($(ARCH), X86)

SO_EXTENSION = dll
CXX = C:/MinGW/bin/g++
LD = C:/MinGW/bin/g++
CFLAGS += -IC:/MinGW/include
CFLAGS += -IC:/MinGW/include/SDL
CFLAGS += -IC:/MinGW/include/PVR
CFLAGS += -IC:/MinGW/include/libpng12
CFLAGS += -DINLINE_OPT -m32

else

COMPILER_DIR = C:/CS2010q1
SO_EXTENSION = so
CXX = 	$(COMPILER_DIR)/bin/arm-none-linux-gnueabi-g++
LD = 	$(COMPILER_DIR)/bin/arm-none-linux-gnueabi-g++
INCLUDE	= C:/Users/jim/Desktop/Lachlan/Pandora/include
CFLAGS 	+= -I$(INCLUDE)/libpng12
CFLAGS 	+= -I$(INCLUDE)/SDL
CFLAGS	+= -I$(INCLUDE)
CFLAGS 	+= -I$(COMPILER_DIR)/arm-none-linux-gnueabi/libc/lib
CFLAGS  += -march=armv7-a -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -ffast-math \
		  -fsingle-precision-constant  -ftree-vectorize -fexpensive-optimizations -fomit-frame-pointer

CFLAGS  += -findirect-inlining
CFLAGS  += -ftree-switch-conversion
CFLAGS  += -floop-interchange -floop-strip-mine -floop-block
CFLAGS  += -DX11_WINDOW

CFLAGS  +=-DARM_ASM -D__NEON_OPT -D__VEC4_OPT -D__HASHMAP_OPT -D__CRC_OPT
#CFLAGS += -D__PACKVERTEX_OPT
CFLAGS += -D__TRIBUFFER_OPT
endif

ifeq ($(OS), LINUX)
CFLAGS += -Wall -D__LINUX__ -fPIC
LDFLAGS += -LC:/Users/jim/Desktop/Lachlan/Pandora/OS/20101302-pandora-xfce/usr/lib
LDFLAGS += -lEGL -lGLESv2 -lsrv_um -lSDL-1.2 -lpng12 -lz -lIMGegl
LDFLAGS += -lX11 -lts-1.0 -lXau -lXdmcp -shared
else
CFLAGS += -Wall
LDFLAGS +=  -LC:/MinGW/lib/PVR -lSDLmain -lSDL -lpng -lGLESv2
endif

#CFLAGS += -DPROFILE_GBI
#CFLAGS += -DSHADER_TEST

OBJECTS =

# list of object files to generate
OBJECTS = Config.o \
    gles2n64.o \
	OpenGL.o \
	N64.o \
	RSP.o \
	VI.o \
	Textures.o \
	ShaderCombiner.o \
	gDP.o \
	gSP.o \
	gSPNeon.o \
	GBI.o \
	DepthBuffer.o \
	CRC.o \
	2xSAI.o \
	RDP.o \
	F3D.o \
	F3DEX.o \
	F3DEX2.o \
	L3D.o \
	L3DEX.o \
	L3DEX2.o \
	S2DEX.o \
	S2DEX2.o \
	F3DPD.o \
	F3DDKR.o \
	F3DWRUS.o \
	F3DCBFD.o

# build targets
all: gles2n64.$(SO_EXTENSION)

clean:
	del -f *.o *.s *.ii *.$(SO_EXTENSION) ui_gln64config.*

# build rules
.cpp.o:
	$(CXX) -o $@ $(CFLAGS) -c $<

.c.o:
	$(CXX) -o $@ $(CFLAGS) -c $<

ui_gln64config.h: gln64config.ui
	$(UIC) $< -o $@

gles2n64.$(SO_EXTENSION): $(OBJECTS)
	$(CXX) $^ $(LDFLAGS) $(SDL_LIBS) $(LIBGL_LIBS) -o $@

gles2n64.o: gles2N64.cpp
	$(CXX) $(CFLAGS) $(SDL_FLAGS) -DMAINDEF -c -o $@ $<

Config_qt4.o: Config_qt4.cpp ui_gln64config.h
	$(CXX) $(CFLAGS) $(SDL_FLAGS) -c -o $@ $<

Config_gtk2.o: Config_gtk2.cpp
	$(CXX) $(CFLAGS) $(SDL_FLAGS) -c -o $@ $<

GBI.o: GBI.cpp
	$(CXX) $(CFLAGS) $(SDL_FLAGS) -c -o $@ $<

