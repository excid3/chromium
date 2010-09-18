# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

import sys

sys.path.append( "../../glapi_parser" )
import apiutil

apiutil.CopyrightC()

print """
/* DO NOT EDIT - THIS FILE AUTOMATICALLY GENERATED BY packspu_proto.py SCRIPT */

#ifndef PACKSPU_FUNCTIONS_H
#define PACKSPU_FUNCTIONS_H 1

#include <stdio.h>
#include "cr_string.h"
#include "cr_spu.h"
#include "packspu.h"
#include "cr_packfunctions.h"
"""


pack_specials = []

keys = apiutil.GetDispatchedFunctions("../../glapi_parser/APIspec.txt")

# make list of special functions
for func_name in keys:
	if ("get" in apiutil.Properties(func_name) or
	    apiutil.FindSpecial( "packspu", func_name ) or 
	    apiutil.FindSpecial( "packspu_flush", func_name ) or 
		apiutil.FindSpecial( "packspu_vertex", func_name )):
	  pack_specials.append( func_name )

for func_name in keys:
	if apiutil.FindSpecial( "packspu_unimplemented", func_name ):
		continue
	if func_name in pack_specials:
		return_type = apiutil.ReturnType(func_name)
		params = apiutil.Parameters(func_name)
		print 'extern %s PACKSPU_APIENTRY packspu_%s( %s );' % ( return_type, func_name, apiutil.MakeDeclarationString(params) )


print """
#endif
"""