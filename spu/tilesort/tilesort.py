# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

import sys

sys.path.append( "../../glapi_parser")
import apiutil


keys = apiutil.GetDispatchedFunctions("../../glapi_parser/APIspec.txt")

apiutil.CopyrightC()

print """
/* DO NOT EDIT - THIS FILE GENERATED BY THE tilesort.py SCRIPT */
#include <stdio.h>
#include "cr_string.h"
#include "cr_spu.h"
#include "cr_packfunctions.h"
#include "cr_glstate.h"
#include "tilesortspu.h"
#include "tilesortspu_gen.h"
"""

num_funcs = len(keys)

print 'SPUNamedFunctionTable _cr_tilesort_table[%d];' % (num_funcs+1)

print """
static void __fillin( int offset, char *name, SPUGenericFunction func )
{
	_cr_tilesort_table[offset].name = crStrdup( name );
	_cr_tilesort_table[offset].fn = func;
}
"""



print 'void tilesortspuCreateFunctions( void )'
print '{'
table_index = 0
# XXX NOTE: this should basically be identical to code in tilesort_lists.py
for func_name in keys:
	if apiutil.FindSpecial( "tilesort", func_name ):
		print '\t__fillin( %3d, "%s", (SPUGenericFunction) tilesortspu_%s );' % (table_index, func_name, func_name )
	elif apiutil.FindSpecial( "tilesort_unimplemented", func_name ):
		print '\t__fillin( %3d, "%s", (SPUGenericFunction) tilesortspu_%s );' % (table_index, func_name, func_name )
	elif apiutil.FindSpecial( "tilesort_state", func_name ):
		print '\t__fillin( %3d, "%s", (SPUGenericFunction) crState%s );' % (table_index, func_name, func_name )
	elif apiutil.FindSpecial( "tilesort_bbox", func_name ):
		print '\t__fillin( %3d, "%s", (SPUGenericFunction) (tilesort_spu.swap ? crPack%sBBOX_COUNTSWAP : crPack%sBBOX_COUNT) );' % (table_index, func_name, func_name, func_name )
	else:
		print '\t__fillin( %3d, "%s", (SPUGenericFunction) (tilesort_spu.swap ? crPack%sSWAP : crPack%s) );' % (table_index, func_name, func_name, func_name )
	table_index += 1
print '\t__fillin( %3d, NULL, NULL );' % num_funcs
print '}'
