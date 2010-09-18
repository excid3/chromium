/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include "cr_packfunctions.h"
#include "cr_mem.h"
#include "replicatespu.h"
#include "replicatespu_proto.h"

void REPLICATESPU_APIENTRY
replicatespu_ChromiumParametervCR(GLenum target, GLenum type, GLsizei count,
																	const GLvoid *values)
{
	CRMessage msg;
	int len;
	
	GET_THREAD(thread);

	switch(target)
	{
		case GL_GATHER_PACK_CR:
			/* flush the current pack buffer */
			replicatespuFlushAll( (void *) thread );

			/* the connection is thread->server.conn */
			msg.header.type = CR_MESSAGE_GATHER;
			msg.gather.offset = 69;
			len = sizeof(CRMessageGather);
			crNetSend(thread->server.conn, NULL, &msg, len);
			break;
			
		default:
			if (replicate_spu.swap)
				crPackChromiumParametervCRSWAP(target, type, count, values);
			else
				crPackChromiumParametervCR(target, type, count, values);
	}
}


GLint REPLICATESPU_APIENTRY replicatespu_RenderMode( GLenum mode )
{
	/* ignore this, use the feedbackSPU if needed */
	return 0;
}


void REPLICATESPU_APIENTRY replicatespu_Finish( void )
{
	unsigned int i;
	GET_THREAD(thread);

	replicatespuFlushAll( (void *) thread );

	for (i = 0; i < CR_MAX_REPLICANTS; i++) {
		int writeback = 1;

		if (!IS_CONNECTED(replicate_spu.rserver[i].conn))
			continue;

		if (replicate_spu.swap) {
			crPackFinishSWAP();
			crPackWritebackSWAP( &writeback );
		}
		else {
			crPackFinish();
			crPackWriteback( &writeback );
		}

		replicatespuFlushOne(thread, i);

		while (writeback)
			crNetRecv();
	}
}


void REPLICATESPU_APIENTRY replicatespu_Flush( void )
{
	GET_THREAD(thread);
	if (replicate_spu.swap)
	{
		crPackFlushSWAP();
	}
	else
	{
		crPackFlush();
	}
	replicatespuFlushAll( (void *) thread );
}


GLint REPLICATESPU_APIENTRY
replicatespu_WindowCreate( const char *dpyName, GLint visBits )
{
	unsigned int i;
	static GLint freeWinID = 400;
	WindowInfo *winInfo = (WindowInfo *) crCalloc(sizeof(WindowInfo));
	GET_THREAD(thread);

	if (!winInfo)
		return -1;

	if (thread)
		replicatespuFlushAll( (void *) thread );
	else
		thread = replicatespuNewThread( crThreadID() );

	replicatespuFlushAll( (void *) thread );
	crPackSetContext( thread->packer );

#ifdef CHROMIUM_THREADSAFE_notyet
	crLockMutex(&_ReplicateMutex);
#endif

	for (i = 1; i < CR_MAX_REPLICANTS; i++) {
		if (IS_CONNECTED(replicate_spu.rserver[i].conn)) {
			int writeback = 1;
			int return_val = -1;

			if (replicate_spu.swap)
				crPackWindowCreateSWAP( dpyName, visBits, &return_val, &writeback );
			else
				crPackWindowCreate( dpyName, visBits, &return_val, &writeback );

			replicatespuFlushOne(thread, i);

			while (writeback)
				crNetRecv();

			if (replicate_spu.swap)
				return_val = (GLint) SWAP32(return_val);

			if (return_val < 0)
				crWarning("Replicate SPU: server %d returned window id -1", i);

			/* XXX temp */
			crDebug("Replicate SPU: Window create, server %d returned %d", i, return_val);
			winInfo->id[i] = return_val;
		}
		else {
			winInfo->id[i] = -1;
		}
	}

	winInfo->visBits = visBits;
	winInfo->width = 0;
	winInfo->height = 0;
	winInfo->nativeWindow = 0;
	winInfo->viewable = GL_FALSE; 

	crHashtableAdd(replicate_spu.windowTable, freeWinID, winInfo);

#ifdef CHROMIUM_THREADSAFE_notyet
	crUnlockMutex(&_ReplicateMutex);
#endif

	return freeWinID++;
}


void REPLICATESPU_APIENTRY
replicatespu_WindowDestroy(GLint win)
{
	WindowInfo *winInfo = (WindowInfo *) crHashtableSearch( replicate_spu.windowTable, win );
	GET_THREAD(thread);
	int i;

	replicatespuFlushAll( (void *) thread );

	if (winInfo) {
		for (i = 0; i < CR_MAX_REPLICANTS; i++) {
			if (!IS_CONNECTED(replicate_spu.rserver[i].conn))
				continue;

			if (replicate_spu.swap)
				crPackWindowDestroySWAP( winInfo->id[i] );
			else
				crPackWindowDestroy( winInfo->id[i] );

			winInfo->id[i] = -1; /* just to be safe */

			replicatespuFlushOne(thread, i);
		}
	}

	crHashtableDelete(replicate_spu.windowTable, win, crFree);
}



void REPLICATESPU_APIENTRY
replicatespu_WindowSize( GLint win, GLint w, GLint h )
{
	WindowInfo *winInfo = (WindowInfo *) crHashtableSearch( replicate_spu.windowTable, win );
	GET_THREAD(thread);
	int i;

	winInfo->width = w;
	winInfo->height = h;

	replicatespuFlushAll( (void *) thread );

	for (i = 0; i < CR_MAX_REPLICANTS; i++) {
		if (!IS_CONNECTED(replicate_spu.rserver[i].conn))
			continue;

		if (replicate_spu.swap)
			crPackWindowSizeSWAP( winInfo->id[i], w, h );
		else
			crPackWindowSize( winInfo->id[i], w, h );

		replicatespuFlushOne(thread, i);
	}
}


void REPLICATESPU_APIENTRY
replicatespu_GenTextures( GLsizei n, GLuint * textures )
{
	/* Don't get IDs from the crservers since different servers will
	 * return different IDs.
	 */
	crStateGenTextures(n, textures);
}

/**
 * Special case just to call Flush at end.
 * Since glClear is typically called at the start of a frame, it's a good
 * time to do a flush to detect any broken connections.  This helps to prevent
 * us from getting into the middle of a Begin/End primitive before detecting
 * a broken connection.
 */
void REPLICATESPU_APIENTRY
replicatespu_Clear( GLbitfield mask )
{
	GET_THREAD(thread);

	/* good time to check for vnc events */
	if (replicate_spu.vncAvailable)
		replicatespuCheckVncEvents();

	if (thread->currentContext->displayListMode != GL_FALSE) {
		crDLMCompileClear(mask);
	}
	if (replicate_spu.swap)
	{
		crPackClearSWAP(mask);
	}
	else
	{
		crPackClear(mask);
	}
	replicatespuFlushAll(thread);
}

