# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

TOP = ../
SHARED = 1
LIBRARY = spuload
LIBRARIES = crutil crmothership

FILES = spuchange spucopy spuinit spuload dispatch glloader choosevisual

PRECOMP = $(TOP)/include/spu_dispatch_table.h dispatch.c spucopy.c spuchange.c glloader.c
SLOP = $(PRECOMP)

LIB_DEFS += loader.def
include ${TOP}/cr.mk


# Need this for crChooseVisual()
ifdef WINDOWS
#LDFLAGS += user32.lib gdi32.lib
else
ifdef DARWIN
#LDFLAGS += -framework Carbon -framework ApplicationServices
else
LDFLAGS += -lX11
endif
endif


$(TOP)/include/spu_dispatch_table.h: dispatchheader.py ../glapi_parser/apiutil.py ../glapi_parser/APIspec.txt
	@$(ECHO) Making the SPU dispatch header
	@$(PYTHON) dispatchheader.py > $(TOP)/include/spu_dispatch_table.h

dispatch.c: dispatch.py ../glapi_parser/apiutil.py ../glapi_parser/APIspec.txt
	@$(ECHO) Making the SPU dispatch builder
	@$(PYTHON) dispatch.py > dispatch.c

spucopy.c: spucopy.py ../glapi_parser/apiutil.py ../glapi_parser/APIspec.txt
	@$(ECHO) Making the SPU copier
	@$(PYTHON) spucopy.py > spucopy.c

spuchange.c: spuchange.py ../glapi_parser/apiutil.py ../glapi_parser/APIspec.txt
	@$(ECHO) Making the SPU interface changer
	@$(PYTHON) spuchange.py > spuchange.c

glloader.c: glloader.py ../glapi_parser/apiutil.py ../glapi_parser/APIspec.txt
	@$(ECHO) Building the OpenGL API loader
	@$(PYTHON) glloader.py > glloader.c
