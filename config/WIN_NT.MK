# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

WINDOWS=1
G++-INCLUDE-DIR = /usr/include/g++
CXX = cl
CC = cl

# NOTE:  If you're compiling on Windows and find that some code warnings
# stop compilation, remove the /WX flags in the next few lines.

CXXFLAGS          += /nologo /DWIN32 /DWINDOWS /TP /W3 /WX /GX /QIfist
CXX_RELEASE_FLAGS += /MD /Ox /DNDEBUG
# This is just not working for me on VC++ 7 --> CXX_DEBUG_FLAGS   += /GZ
CXX_DEBUG_FLAGS   += /MDd /Yd /Z7

CFLAGS          += /nologo /DWIN32 /DWINDOWS /W3 /WX /TC /GX /QIfist /Zm1024
C_RELEASE_FLAGS += /MD /Ox /DNDEBUG
# This is just not working for me on VC++ 7 --> C_DEBUG_FLAGS   += /GZ
C_DEBUG_FLAGS   += /MDd /Yd /Z7

LDFLAGS          += /nologo
LD_RELEASE_FLAGS += 
LD_DEBUG_FLAGS   += 

CAT = cat
LEX = flex -t
LEXLIB = -ll
YACC = bison -y -d
LD = cl
AR = ar
ARCREATEFLAGS = cr
RANLIB = true
LN = ln -s
MKDIR = mkdir -p
ECHO := echo
CLEAR := cls
RM = rm -f
CP = cp
MV = mv
MAKE = make --no-print-directory
NOWEB = noweb
LATEX = latex
BIBTEX = bibtex
DVIPS = dvips -t letter
GHOSTSCRIPT = gs
LIBPREFIX = 
DLLSUFFIX = .dll
LIBSUFFIX = .lib
OBJSUFFIX = .obj
EXESUFFIX = .exe
SHARED_LDFLAGS = /LDd
PERL = perl
PYTHON = python.exe
JGRAPH = jgraph
MPI_CC = cl
MPI_CXX = cl
MPI_LDFLAGS += mpichd.lib
