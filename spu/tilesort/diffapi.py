# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

import sys

sys.path.append( "../../glapi_parser" )
import apiutil

apiutil.CopyrightC()

print """
/* DO NOT EDIT - THIS FILE GENERATED BY THE diffapi.py SCRIPT */
#include "cr_spu.h"
#include "cr_packfunctions.h"
#include "tilesortspu.h"

#include <stdio.h>

static const CRPixelPackState defaultPacking = {
	0, 		/* rowLength */
	0, 		/* skipRows */
	0, 		/* skipPixels */
	1, 		/* alignment */
	0, 		/* imageHeight */
	0, 		/* skipImages */
	GL_FALSE, 	/* swapBytes */
	GL_FALSE  	/* psLSBFirst */
};

"""

keys = apiutil.GetDispatchedFunctions("../../glapi_parser/APIspec.txt")

for func_name in keys:
	props = apiutil.Properties(func_name)
	if "pixelstore" in props and "get" not in props:
	    return_type = apiutil.ReturnType(func_name)
	    params = apiutil.Parameters(func_name)
	    print 'static %s TILESORTSPU_APIENTRY tilesortspuDiff%s( %s )' % (return_type, func_name, apiutil.MakeDeclarationString( params ) )
	    print '{'
	    params.append( ('&defaultPacking', 'foo', 0) )
	    print '\tif (tilesort_spu.swap)'
	    print '\t{'
	    print '\t\tcrPack%sSWAP( %s );' % (func_name, apiutil.MakeCallString( params ) )
	    print '\t}'
	    print '\telse'
	    print '\t{'
	    print '\t\tcrPack%s( %s );' % (func_name, apiutil.MakeCallString( params ) )
	    print '\t}'
	    print '}'


print """
static const GLubyte * TILESORTSPU_APIENTRY
diffGetString(GLenum cap)
{
  GET_CONTEXT(ctx);
  return ctx->limits.extensions;
}


void tilesortspuCreateDiffAPI( void )
{
	SPUDispatchTable diff;

	crSPUInitDispatchTable(&diff);

	/* Note: state differencing should never involve calling a "glGet"
	 * function.  So we set those pointers to NULL.
	 */
"""

for func_name in keys:
	props = apiutil.Properties(func_name)

	if not apiutil.CanPack(func_name):
		continue

	if "get" in props:
		if func_name == "GetString":
			# special case for state differencer
			print '\tdiff.GetString = diffGetString;'
			pass
		else:
			print '\tdiff.%s = NULL;' % func_name
	elif "pixelstore" in props:
		print '\tdiff.%s = tilesortspuDiff%s;' % (func_name, func_name)
	else:
		print '\tdiff.%s = tilesort_spu.swap ? crPack%sSWAP : crPack%s;' % (func_name, func_name, func_name)
print '\tcrStateDiffAPI( &diff );'
print '}'