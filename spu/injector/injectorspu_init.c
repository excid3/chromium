/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include "cr_mem.h"
#include "cr_spu.h"
#include "cr_net.h"
#include "cr_url.h"
#include "injectorspu.h"
#include <stdio.h>

InjectorSPU injector_spu;

static SPUFunctions injector_functions = {
	NULL, /* CHILD COPY */
	NULL, /* DATA */
	_cr_injector_table /* THE ACTUAL FUNCTIONS */
};

static int
injectorspuReceiveData( CRConnection* conn, CRMessage *msg, unsigned int len )
{
	switch ( msg->header.type ) {
		case CR_MESSAGE_OOB:
			/*injector_spu.oob_count++ ;*/
			crDebug( "injectorspuReceiveData: OOB from conn %p ?", (void *) conn ) ;
			break ;
		default:
			crWarning( "injectorspuReceiveData: Why did I receive message of type 0x%x ?", msg->header.type ) ;
			return 0 ; /* not handled, pass to default handler */
	}

	(void) conn ;
	(void) len ;

	return 1 ; /* handled */
}

static void injectorspuConnect( void )
{
	char hostname[4096], protocol[4096] ;
	unsigned short port ;

	crNetInit( injectorspuReceiveData, NULL ) ;

	injector_spu.oob_conn = crAlloc( sizeof(CRConnection*) ) ;
	if ( ! crParseURL( injector_spu.oob_url, protocol, hostname, &port, (unsigned short) INJECTORSPU_OOB_PORT ) )
			crError( "Malformed URL: \"%s\"", injector_spu.oob_url ) ;
	injector_spu.oob_conn = crNetAcceptClient( protocol, NULL, (short) port, 32768 /*mtu*/, 0 /*broker*/ ) ;

	/* Initialize the message we send back after every frame */
	injector_spu.info_msg.header.type = CR_MESSAGE_OOB ;
	injector_spu.info_msg.header.conn_id = injector_spu.oob_conn->id ;
	injector_spu.info_msg.frameNum = 0 ;
}

static SPUFunctions *injectorSPUInit( int id, SPU *child, SPU *self,
		unsigned int context_id,
		unsigned int num_contexts )
{

	(void) self;
	(void) context_id;
	(void) num_contexts;

	injector_spu.id = id;
	injector_spu.has_child = 0;
	injector_spu.server = NULL;
	if (child)
	{
		crSPUInitDispatchTable( &(injector_spu.child) );
		crSPUCopyDispatchTable( &(injector_spu.child), &(child->dispatch_table) );
		injector_spu.has_child = 1;
	}
	crSPUInitDispatchTable( &(injector_spu.super) );
	crSPUCopyDispatchTable( &(injector_spu.super), &(self->superSPU->dispatch_table) );
	injectorspuGatherConfiguration( &injector_spu );

	/* Don't want oob stream to pass swapbuffers */
	crSPUCopyDispatchTable( &(injector_spu.oob_dispatch), &(injector_spu.child) );
	injector_spu.oob_dispatch.SwapBuffers = (SwapBuffersFunc_t) injectorspuOOBSwapBuffers;
	injector_spu.oob_dispatch.ChromiumParameteriCR = (ChromiumParameteriCRFunc_t) injectorspuChromiumParameteri;

	injectorspuConnect() ;
	return &injector_functions;
}

static void injectorSPUSelfDispatch(SPUDispatchTable *self)
{
	crSPUInitDispatchTable( &(injector_spu.self) );
	crSPUCopyDispatchTable( &(injector_spu.self), self );

	injector_spu.server = (CRServer *)(self->server);
}

static int injectorSPUCleanup(void)
{
	return 1;
}

int SPULoad( char **name, char **super, SPUInitFuncPtr *init,
	     SPUSelfDispatchFuncPtr *self, SPUCleanupFuncPtr *cleanup,
	     SPUOptionsPtr *options, int *flags )
{
	*name = "injector";
	*super = "passthrough";
	*init = injectorSPUInit;
	*self = injectorSPUSelfDispatch;
	*cleanup = injectorSPUCleanup;
	*options = injectorSPUOptions;
	*flags = (SPU_NO_PACKER|SPU_NOT_TERMINAL|SPU_MAX_SERVERS_ZERO);
	
	return 1;
}
