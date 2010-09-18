# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

import sys

sys.path.append( "../glapi_parser" )
import apiutil


apiutil.CopyrightC()

print """
/* DO NOT EDIT - THIS FILE AUTOMATICALLY GENERATED BY server_retval.py SCRIPT */
#include "chromium.h"
#include "cr_mem.h"
#include "cr_net.h"
#include "server_dispatch.h"
#include "server.h"

void crServerReturnValue( const void *payload, unsigned int payload_len )
{
	CRMessageReadback *rb;
	int msg_len = sizeof( *rb ) + payload_len;
	if (cr_server.curClient->conn->type == CR_FILE)
	{
		return;
	}
	rb = (CRMessageReadback *) crAlloc( msg_len );

	rb->header.type = CR_MESSAGE_READBACK;
	crMemcpy( &(rb->writeback_ptr), &(cr_server.writeback_ptr), sizeof( rb->writeback_ptr ) );
	crMemcpy( &(rb->readback_ptr), &(cr_server.return_ptr), sizeof( rb->readback_ptr ) );
	crMemcpy( rb+1, payload, payload_len );
	crNetSend( cr_server.curClient->conn, NULL, rb, msg_len );
	crFree( rb );
}
"""

keys = apiutil.GetDispatchedFunctions("../glapi_parser/APIspec.txt")

for func_name in keys:
	params = apiutil.Parameters(func_name)
	return_type = apiutil.ReturnType(func_name)
	if apiutil.FindSpecial( "server", func_name ):
		continue
	if return_type != 'void':
		print '%s SERVER_DISPATCH_APIENTRY crServerDispatch%s( %s )' % ( return_type, func_name, apiutil.MakeDeclarationString(params))
		print '{'
		print '\t%s retval;' % return_type
		print '\tretval = cr_server.head_spu->dispatch_table.%s( %s );' % (func_name, apiutil.MakeCallString(params) );
		print '\tcrServerReturnValue( &retval, sizeof(retval) );'
		print '\treturn retval; /* WILL PROBABLY BE IGNORED */'
		print '}'
