/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include "replicatespu.h"
#include "cr_mothership.h"
#include "cr_mem.h"
#include "cr_packfunctions.h"
#include "cr_string.h"
#include "cr_dlm.h"
#include "replicatespu_proto.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <X11/Xmd.h>
#include <X11/extensions/vnc.h>

#define MAGIC_OFFSET 3000


/*
 * Allocate a new ThreadInfo structure, setup a connection to the
 * server, allocate/init a packer context, bind this ThreadInfo to
 * the calling thread with crSetTSD().
 * We'll always call this function at least once even if we're not
 * using threads.
 */
ThreadInfo *replicatespuNewThread( unsigned long id )
{
	ThreadInfo *thread;

#ifdef CHROMIUM_THREADSAFE_notyet
	crLockMutex(&_ReplicateMutex);
#else
	CRASSERT(replicate_spu.numThreads == 0);
#endif

	CRASSERT(replicate_spu.numThreads < MAX_THREADS);
	thread = &(replicate_spu.thread[replicate_spu.numThreads]);

	thread->id = id;
	thread->currentContext = NULL;

	/* connect to the server */
	thread->server.name = crStrdup( replicate_spu.name );
	thread->server.buffer_size = replicate_spu.buffer_size;
	if (replicate_spu.numThreads == 0) {
		replicatespuConnectToServer( &(thread->server) );
		CRASSERT(thread->server.conn);
		replicate_spu.swap = thread->server.conn->swap;
	}
	else {
		/* a new pthread */
		replicatespuFlushAll( &(replicate_spu.thread[0]) );
		crNetNewClient( replicate_spu.thread[0].server.conn, &(thread->server));
		CRASSERT(thread->server.conn);
	}

	/* packer setup */
	CRASSERT(thread->packer == NULL);
	thread->packer = crPackNewContext( replicate_spu.swap );
	CRASSERT(thread->packer);
	crPackInitBuffer( &(thread->buffer), crNetAlloc(thread->server.conn),
				thread->server.conn->buffer_size, thread->server.conn->mtu );
	thread->buffer.canBarf = thread->server.conn->Barf ? GL_TRUE : GL_FALSE;
	crPackSetBuffer( thread->packer, &thread->buffer );
	crPackFlushFunc( thread->packer, replicatespuFlush );
	crPackFlushArg( thread->packer, (void *) thread );
	crPackSendHugeFunc( thread->packer, replicatespuHuge );
	crPackSetContext( thread->packer );

#ifdef CHROMIUM_THREADSAFE_notyet
	crSetTSD(&_ReplicateTSD, thread);
#endif

	replicate_spu.numThreads++;

#ifdef CHROMIUM_THREADSAFE_notyet
	crUnlockMutex(&_ReplicateMutex);
#endif
	return thread;
}


/**
 * Determine if the X VNC extension is available.
 * Get list of clients/viewers attached, and replicate to them.
 * Can't call this until we have an X display name (see CreateContext).
 */
static void
replicatespuStartVnc(const char *dpyName)
{
	int maj, min;

#ifdef CHROMIUM_THREADSAFE_notyet
	crLockMutex(&_ReplicateMutex);
#endif

	if (replicate_spu.StartedVnc) {
#ifdef CHROMIUM_THREADSAFE_notyet
		crUnlockMutex(&_ReplicateMutex);
#endif
		return;
	}

#ifdef CHROMIUM_THREADSAFE_notyet
	crUnlockMutex(&_ReplicateMutex);
#endif

	/* Open the named display */
	if (!replicate_spu.glx_display) {
		replicate_spu.glx_display = XOpenDisplay(dpyName);
		if (!replicate_spu.glx_display) {
			/* Try local display */
			crWarning("Replicate SPU: Unable to open X display %s, trying :0 next",
								dpyName);
			replicate_spu.glx_display = XOpenDisplay(":0");
		}
		if (!replicate_spu.glx_display) {
			crError("Replicate SPU: Unable to open X display :0");
		}
	}

	CRASSERT(replicate_spu.glx_display);

	/* NOTE: should probably check the major/minor version too!! */
	if (!XVncQueryExtension(replicate_spu.glx_display, &maj, &min)) {
		crWarning("Replicate SPU: X server VNC extension is not available.");
		replicate_spu.vncAvailable = GL_FALSE;
	} else {
		crWarning("Replicate SPU: X server VNC extension available.");
		replicate_spu.vncAvailable = GL_TRUE;
	}

	if (replicate_spu.vncAvailable) {
		VncConnectionList *vnclist;
		int num_conn;
		int i;

		replicate_spu.VncEventsBase = XVncGetEventBase(replicate_spu.glx_display);
		XVncSelectNotify(replicate_spu.glx_display, 1);

		vnclist = XVncListConnections(replicate_spu.glx_display, &num_conn);
		crDebug("Replicate SPU: Found %d open VNC connections", num_conn);
		for (i = 0; i < num_conn; i++) {
			if (vnclist[i].ipaddress) {
				replicatespuReplicate(vnclist[i].ipaddress);
			}
			else {
				crWarning("Replicate SPU: vnc client connection with no ipaddress!");
			}
		}

	}

	replicate_spu.StartedVnc = 1;
}



GLint REPLICATESPU_APIENTRY
replicatespu_CreateContext( const char *dpyName, GLint visual, GLint shareCtx )
{
	static GLint freeCtxID = MAGIC_OFFSET;
	char headspuname[10];
	ContextInfo *context, *sharedContext = NULL;
	unsigned int i;

	if (shareCtx > 0) {
		sharedContext = (ContextInfo *)
			crHashtableSearch(replicate_spu.contextTable, shareCtx);
	}

	replicatespuFlushAll( &(replicate_spu.thread[0]) );

#ifdef CHROMIUM_THREADSAFE_notyet
	crLockMutex(&_ReplicateMutex);
#endif

	replicatespuStartVnc(dpyName);

	/*
	 * Alloc new ContextInfo object
	 */
	context = (ContextInfo *) crCalloc(sizeof(ContextInfo));
	if (!context) {
		crWarning("Replicate SPU: Out of memory in CreateContext");
		return -1;
	}

	/* Contexts that don't share display lists get their own
	 * display list managers.  Contexts that do, share the
	 * display list managers of the contexts they're sharing
	 * with (man, some grammarian has to go over that and make
	 * it actually sound like English).
	 */
	if (sharedContext) {
		context->displayListManager = sharedContext->displayListManager;
		/* Let the DLM know that a second context is using the
		 * same display list manager, so it can manage when its
		 * resources are released.
		 */
		crDLMUseDLM(context->displayListManager);
	}
	else {
		context->displayListManager = crDLMNewDLM(0, NULL);
		if (!context->displayListManager) {
			crWarning("Replicate SPU: could not initialize display list manager.");
		}
	}

	/* Fill in the new context info */
	if (sharedContext)
		context->State = crStateCreateContext(NULL, visual, sharedContext->State);
	else
		context->State = crStateCreateContext(NULL, visual, NULL);
	context->rserverCtx[0] = 1; /* not really used */
	context->visBits = visual;
	context->currentWindow = 0; /* not bound */
	context->dlmState
		= crDLMNewContext(context->displayListManager);
	context->displayListMode = GL_FALSE; /* not compiling */
	context->displayListIdentifier = 0;
	context->shareCtx = shareCtx;

#if 0
	/* Set the Current pointers now.... */
	crStateSetCurrentPointers( context->State,
														 &(replicate_spu.thread[0].packer->current) );
#endif

	for (i = 1; i < CR_MAX_REPLICANTS; i++) {
		int r_writeback = 1, rserverCtx = -1;
		int sharedServerCtx;

		sharedServerCtx = sharedContext ? sharedContext->rserverCtx[i] : 0;

		if (!IS_CONNECTED(replicate_spu.rserver[i].conn))
			continue;

		if (replicate_spu.swap)
			crPackCreateContextSWAP( dpyName, visual, sharedServerCtx,
															 &rserverCtx, &r_writeback );
		else
			crPackCreateContext( dpyName, visual, sharedServerCtx,
													 &rserverCtx, &r_writeback );

		/* Flush buffer and get return value */
		replicatespuFlushOne( &(replicate_spu.thread[0]), i );

		while (r_writeback)
			crNetRecv();
		if (replicate_spu.swap)
			rserverCtx = (GLint) SWAP32(rserverCtx);

		if (rserverCtx < 0) {
#ifdef CHROMIUM_THREADSAFE_notyet
			crUnlockMutex(&_ReplicateMutex);
#endif
			crWarning("Replicate SPU: CreateContext failed.");
			return -1;  /* failed */
		}

		context->rserverCtx[i] = rserverCtx;
	}


	if (!crStrcmp( headspuname, "nop" ))
		replicate_spu.NOP = 0;
	else
		replicate_spu.NOP = 1;

#ifdef CHROMIUM_THREADSAFE_notyet
	crUnlockMutex(&_ReplicateMutex);
#endif

	crListPushBack(replicate_spu.contextList, (void *)freeCtxID);
	crHashtableAdd(replicate_spu.contextTable, freeCtxID, context);

	return freeCtxID++;
}

static int CompareIntegers(const void *element1, const void *element2)
{
    return (element1 == element2) ? 0 : 1;
}

void REPLICATESPU_APIENTRY
replicatespu_DestroyContext( GLint ctx )
{
	unsigned int i;
	ContextInfo *context = (ContextInfo *) crHashtableSearch(replicate_spu.contextTable, ctx);
	GET_THREAD(thread);

	if (!context) {
		crWarning("Replicate SPU: DestroyContext, bad context %d", ctx);
		return;
	}
	CRASSERT(thread);

	replicatespuFlushAll( (void *)thread );

	for (i = 0; i < CR_MAX_REPLICANTS; i++) {
		if (!IS_CONNECTED(replicate_spu.rserver[i].conn))
			continue;

		if (replicate_spu.swap)
			crPackDestroyContextSWAP( context->rserverCtx[i] );
		else
			crPackDestroyContext( context->rserverCtx[i] );

		replicatespuFlushOne(thread, i);
	}

	crStateDestroyContext( context->State );

	/* Although we only allocate a display list manager once,
	 * we free it every time; this is okay since the DLM itself
	 * will track its uses and will only release the resources
	 * when the last user has relinquished it.
	 */
	crDLMFreeDLM(context->displayListManager);
	crDLMFreeContext(context->dlmState);

	if (thread->currentContext == context) {
		thread->currentContext = NULL;
		crStateMakeCurrent( NULL );
		crDLMSetCurrentState(NULL);
	}

	/* zero, just to be safe */
	crMemZero(context, sizeof(ContextInfo));

	/* Delete from both the context table, and the context list. */
	crHashtableDelete(replicate_spu.contextTable, ctx, crFree);
	{
	    CRListIterator *foundElement = crListFind(replicate_spu.contextList, (void *)ctx, CompareIntegers);
	    if (foundElement != NULL) {
		crListErase(replicate_spu.contextList, foundElement);
	    }
	}
}


/**
 * Tell VNC server to begin monitoring the application's X window for
 * moves/resizes/destroy/clipping.
 * When the server-side VNC module notices such changes, it'll
 * send an rfbChromiumMoveResizeWindow/etc message to the VNC viewer(s).
 * The VNC server will be able to detect when the X window is destroyed too.
 */
static void
replicatespuMonitorWindow(WindowInfo *winInfo)
{
	int i;
	for (i = 1; i < CR_MAX_REPLICANTS; i++) {
		if (winInfo->id[i] >= 0) {
			XVncChromiumMonitor(replicate_spu.glx_display,
													winInfo->id[i], winInfo->nativeWindow);
		}
	}
}


/**
 * Callback called by crHashtableWalk() below.
 */
static void
destroyContextCallback(unsigned long key, void *data1, void *data2)
{
	if (key >= 1) {
		replicatespu_DestroyContext((GLint) key);
	}
}

static void
destroyWindowCallback(unsigned long key, void *data1, void *data2)
{
	if (key >= 1) {
		replicatespu_WindowDestroy((GLint) key);
	}
}


void
replicatespuDestroyAllWindowsAndContexts(void)
{
	crHashtableWalk(replicate_spu.windowTable, destroyWindowCallback, NULL);
	crHashtableWalk(replicate_spu.contextTable, destroyContextCallback, NULL);
}



void REPLICATESPU_APIENTRY
replicatespu_MakeCurrent( GLint window, GLint nativeWindow, GLint ctx )
{
	unsigned int i;
	unsigned int show_window = 0;
	WindowInfo *winInfo = (WindowInfo *) crHashtableSearch( replicate_spu.windowTable, window );
	ContextInfo *newCtx = (ContextInfo *) crHashtableSearch( replicate_spu.contextTable, ctx );
	GET_THREAD(thread);

	if (!winInfo) {
		crWarning("Replicate SPU: Invalid window ID %d passed to MakeCurrent", window);
		return;
	}

	if (thread)
		replicatespuFlushAll( (void *)thread );

	if (!thread) {
		thread = replicatespuNewThread( crThreadID() );
	}
	CRASSERT(thread);
	CRASSERT(thread->packer);

	if (newCtx && winInfo) {
		newCtx->currentWindow = winInfo;

#if 000
		/* This appears to be obsolete code */
		if (replicate_spu.render_to_crut_window && !nativeWindow) {
			char response[8096];
			CRConnection *conn = crMothershipConnect();
			if (!conn) {
				crError("Replicate SPU: Couldn't connect to the mothership to get CRUT drawable-- "
								"I have no idea what to do!");
			}
			crMothershipGetParam( conn, "crut_drawable", response );
			nativeWindow = crStrToInt(response);
			crDebug("Replicate SPU: using CRUT drawable: 0x%x", nativeWindow);
			crMothershipDisconnect(conn);
		}
#endif

		if (replicate_spu.glx_display
				&& winInfo
				&& winInfo->nativeWindow != nativeWindow)
		{
			winInfo->nativeWindow = nativeWindow;
			replicatespuMonitorWindow(winInfo);
			replicatespuRePositionWindow(winInfo);
			show_window = 1;
		}

		CRASSERT(newCtx->State);  /* verify valid */

		crPackSetContext( thread->packer );
	}

	/*
	 * Send the MakeCurrent to all crservers (vnc viewers)
	 */
	for (i = 0; i < CR_MAX_REPLICANTS; i++) {
		const GLint serverWin = winInfo ? winInfo->id[i] : -1;
		const GLint serverCtx = newCtx ? newCtx->rserverCtx[i] : -1;

		if (!IS_CONNECTED(replicate_spu.rserver[i].conn))
				continue;

		/* Note: app's native window ID not needed on server side; pass zero */
		if (replicate_spu.swap)
			crPackMakeCurrentSWAP(serverWin, 0, serverCtx);
		else
			crPackMakeCurrent(serverWin, 0, serverCtx);

		if (show_window) {
			/* We may find that the window was mapped before we
			 * called MakeCurrent, if that's the case then ensure
			 * the remote end gets the WindowShow event */
			if (winInfo->viewable) {
				if (replicate_spu.swap)
					crPackWindowShowSWAP(serverWin, GL_TRUE);
				else
					crPackWindowShow(serverWin, GL_TRUE);
			}
		}

		replicatespuFlushOne(thread, i);
	}

	if (newCtx) {
		crStateMakeCurrent( newCtx->State );
		crDLMSetCurrentState(newCtx->dlmState);
	}
	else {
		crStateMakeCurrent( NULL );
		crDLMSetCurrentState(NULL);
	}

	thread->currentContext = newCtx;
}
