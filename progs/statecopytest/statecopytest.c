/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

/*
 * Test that copies of the state tracker library (such as
 * libreadbackspu_crstate_copy.so) really act as copies.
 *
 * We discovered the following on Linux:
 * 1. The crserver links with libcrserverlib_crstate_copy.so
 * 2. The readback SPU links with libreadbackspu_crstate_copy.so
 * 3. If the crserver loads the readback SPU, the server and readback SPU
 *    both seem to use the same instance of the state tracker (we'd expect
 *    each to have its own copy).
 *
 * This program tests that scenario.
 *
 * Brian Paul  10 July 2002
 */


#include <stdio.h>
#include "cr_dll.h"
#include "cr_error.h"


/* This __currentContext should be the one linked into this program
 * via the TRACKS_STATE makefile option.
 */
extern void *__currentBits;


typedef void *(*FuncPtr_t)(void);


int main(int argc, char *argv[])
{
	CRDLL *lib;
	FuncPtr_t fptr;
	void *p;

	/* Load the readback SPU */
#ifdef WINDOWS
	lib = crDLLOpen("readbackspu.dll", 0);
#elif defined(DARWIN)
	lib = crDLLOpen("readbackspu.bundle", 0);
#else
	lib = crDLLOpen("libreadbackspu.so", 0);
#endif
	CRASSERT(lib);
	/* get address of the readbackspu_state_test() function */
	fptr = (FuncPtr_t) crDLLGet(lib, "_readbackspu_state_test");
	/* call it */
	p = (*fptr)();

	printf("The statecopytest program finds &__currentBits = %p\n",
		   (void *)&__currentBits);

	printf("The readback SPU finds   ...    &__currentBits = %p\n", p);

	printf("If copying of the state tracker works, these addresses should be different!\n");

	crDLLClose(lib);

	(void) argc;
	(void) argv;

	return 0;
}
