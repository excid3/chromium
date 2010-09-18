/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include <stdio.h>
#include <assert.h>

#include "cr_spu.h"
#include "saveframespu.h"

extern SPUNamedFunctionTable _cr_saveframe_table[];

static SPUFunctions saveframe_functions = {
	NULL, /* CHILD COPY */
	NULL, /* DATA */
	_cr_saveframe_table /* THE ACTUAL FUNCTIONS */
};

static SPUFunctions *SPUInit( int id, SPU *child, SPU *self,
		unsigned int context_id,
		unsigned int num_contexts )
{

	(void) context_id;
	(void) num_contexts;

	saveframe_spu.id = id;
	saveframe_spu.has_child = 0;
	if (child)
	{
		crSPUInitDispatchTable( &(saveframe_spu.child) );
		crSPUCopyDispatchTable( &(saveframe_spu.child), &(child->dispatch_table) );
		saveframe_spu.has_child = 1;
	}
	crSPUInitDispatchTable( &(saveframe_spu.super) );
	crSPUCopyDispatchTable( &(saveframe_spu.super), &(self->superSPU->dispatch_table) );
	saveframespuGatherConfiguration( child );

	return &saveframe_functions;
}

static void SPUSelfDispatch(SPUDispatchTable *self)
{
	crSPUInitDispatchTable( &(saveframe_spu.self) );
	crSPUCopyDispatchTable( &(saveframe_spu.self), self );
}

static int SPUCleanup(void)
{
	return 1;
}

extern SPUOptions saveframeSPUOptions[];

int SPULoad( char **name, char **super, SPUInitFuncPtr *init,
	     SPUSelfDispatchFuncPtr *self, SPUCleanupFuncPtr *cleanup,
	     SPUOptionsPtr *options, int *flags )
{
	*name = "saveframe";
	*super = "passthrough";
	*init = SPUInit;
	*self = SPUSelfDispatch;
	*cleanup = SPUCleanup;
	*options = saveframeSPUOptions;
	*flags = (SPU_NO_PACKER|SPU_NOT_TERMINAL|SPU_MAX_SERVERS_ZERO);
	
	return 1;
}
