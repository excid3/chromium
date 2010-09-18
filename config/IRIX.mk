# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

# Disabled warnings:
# 1174: The function "f" was declared but never referenced.
# 3201: The parameter "i" was never referenced.
# 1209: The controlling expression is constant.
# 1552: The variable "i" is set but never used.

G++-INCLUDE-DIR = /usr/include/g++
CXX = CC
CC = cc

CXXFLAGS          += -n32 -DIRIX -woff 1174,3201,1209,1552
CXX_RELEASE_FLAGS += -O2 -DNDEBUG
CXX_DEBUG_FLAGS   += -g

CFLAGS            += -n32 -DIRIX -woff 1174,3201,1209,1552
C_RELEASE_FLAGS   += -O2 -DNDEBUG
C_DEBUG_FLAGS     += -g

LDFLAGS           += -n32 -ignore_unresolved
LD_RELEASE_FLAGS  +=
LD_DEBUG_FLAGS    +=

FULLWARN = -fullwarn
PROFILEFLAGS = -pg -a

CAT = cat
LEX = flex -t
LEXLIB = -ll
YACC = bison -y -d
LD = $(CXX)
AR = ar
ARCREATEFLAGS = cr
RANLIB = true
LN = ln -s
MKDIR = mkdir -p
RM = rm -f
CP = cp
MAKE = gmake -s
NOWEB = noweb
LATEX = latex
BIBTEX = bibtex
DVIPS = dvips -t letter
GHOSTSCRIPT = gs
LIBPREFIX = lib
DLLSUFFIX = .so
LIBSUFFIX = .a
OBJSUFFIX = .o
MV = mv
SHARED_LDFLAGS = -shared -n32
PERL = perl
PYTHON = python
JGRAPH = jgraph
PS2PDF = ps2pdf

MPI_CC = cc
MPI_CXX = CC
MPI_LDFLAGS = -lmpi
SLOP += so_locations

QT=0
ifeq ($(QT),1)
    QTDIR=/insert/path/to/qt/here/qt-2.3.1
    MOC=$(QTDIR)/bin/moc
    UIC=$(QTDIR)/bin/uic
endif

ifeq ($(USE_DMX), 1)
CXXFLAGS += -DUSE_DMX
CFLAGS += -DUSE_DMX
endif
