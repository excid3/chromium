/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include "tilesortspu.h"
#include "tilesortspu_gen.h"
#include "cr_error.h"
#include "cr_mem.h"
#include "cr_mothership.h"
#include "cr_packfunctions.h"
#include "cr_string.h"


#ifdef CHROMIUM_THREADSAFE
static ThreadInfo *tilesortspuNewThread(void);
#endif

/**
 * This function is registered with the DLM and will get called when
 * the DLM detects any OpeNGL errors.
 */
static void
ErrorCallback(int line, const char *file, GLenum error, const char *info)
{
	crStateError(line, file, error, info);
}


/**
 * Initialize per-thread data.
 */
void tilesortspuInitThreadPacking( ThreadInfo *thread )
{
	int i;

	thread->pinchState.numRestore = 0;
	thread->pinchState.wind = 0;
	thread->pinchState.isLoop = 0;

	thread->packer = crPackNewContext( tilesort_spu.swap );
	if (!thread->packer)
		crError("tilesortspuInitThread failed!");

	crPackErrorFunction(thread->packer, ErrorCallback);

	crPackSetContext( thread->packer ); /* sets the packer's per-thread context */
	crPackInitBuffer( &(thread->geometry_buffer),
										crAlloc( tilesort_spu.geom_buffer_size ),
										tilesort_spu.geom_buffer_size,
										tilesort_spu.geom_buffer_mtu );

	thread->geometry_buffer.geometry_only = GL_TRUE;
	crPackSetBuffer( thread->packer, &(thread->geometry_buffer) );
	crPackFlushFunc( thread->packer, tilesortspuFlush_callback );
	crPackFlushArg( thread->packer, (void *) thread );
	crPackSendHugeFunc( thread->packer, tilesortspuHuge );
	crPackResetBoundingBox( thread->packer );

	CRASSERT(thread->netServer[0].conn);
	CRASSERT(tilesort_spu.num_servers > 0);

	for (i = 0; i < tilesort_spu.num_servers; i++)
	{
		crPackInitBuffer( &(thread->buffer[i]),
					crNetAlloc( thread->netServer[i].conn ),
					thread->netServer[i].conn->buffer_size,
					thread->netServer[i].conn->mtu );

		if (thread->netServer[i].conn->Barf)
		{
			thread->buffer[i].canBarf = GL_TRUE;
			thread->packer->buffer.canBarf = GL_TRUE;
			thread->geometry_buffer.canBarf = GL_TRUE;
		}
	}


	thread->currentContext = NULL;
}

/**
 * Allocate a new ThreadInfo structure, setup a connection to the
 * server, allocate/init a packer context, bind this ThreadInfo to
 * the calling thread with crSetTSD().
 */
#ifdef CHROMIUM_THREADSAFE
static ThreadInfo *tilesortspuNewThread(void)
{
	ThreadInfo *thread;
	int i;

	crLockMutex(&_TileSortMutex);

	CRASSERT(tilesort_spu.numThreads > 0);
	CRASSERT(tilesort_spu.numThreads < MAX_THREADS);
	thread = &(tilesort_spu.thread[tilesort_spu.numThreads]);
	tilesort_spu.numThreads++;

	thread->state_server_index = -1;

	thread->netServer = (CRNetServer *) crCalloc( tilesort_spu.num_servers * sizeof(CRNetServer) );
	thread->buffer = (CRPackBuffer *) crCalloc( tilesort_spu.num_servers * sizeof(CRPackBuffer) );

	for (i = 0; i < tilesort_spu.num_servers; i++) {
		thread->netServer[i].name = crStrdup( tilesort_spu.thread[0].netServer[i].name );
		thread->netServer[i].buffer_size = tilesort_spu.thread[0].netServer[i].buffer_size;
		/* Establish new connection to server[i] */
		crNetNewClient( tilesort_spu.thread[0].netServer[i].conn, &(thread->netServer[i]));

	}

	tilesortspuInitThreadPacking( thread );

	crSetTSD(&_ThreadTSD, thread);

	crUnlockMutex(&_TileSortMutex);

	return thread;
}
#endif


GLint TILESORTSPU_APIENTRY
tilesortspu_CreateContext( const char *dpyName, GLint visBits, GLint shareCtx )
{
	static GLint freeContextID = 200;
	ThreadInfo *thread0 = &(tilesort_spu.thread[0]);
	ContextInfo *contextInfo;
	int i;

	crDebug("Tilesort SPU: CreateContext(%s, 0x%x)", dpyName, visBits);

	if (shareCtx > 0) {
		crWarning("Tilesort SPU: Context sharing not supported yet.");
		shareCtx = 0;
	}

	if (tilesort_spu.forceQuadBuffering && tilesort_spu.stereoMode == CRYSTAL)
		visBits |= CR_STEREO_BIT;

	/* release geometry buffer */
	crPackReleaseBuffer( thread0->packer );

#ifdef CHROMIUM_THREADSAFE
	crLockMutex(&_TileSortMutex);
#endif

	contextInfo = (ContextInfo *) crCalloc(sizeof(ContextInfo));
	if (!contextInfo)
		return -1;


#ifdef WINDOWS
	crDebug("Tilesort SPU: HDC = %s", dpyName);
	if (!dpyName)
		contextInfo->client_hdc = GetDC(NULL);
	else
		contextInfo->client_hdc = (HDC) crStrToInt(dpyName);
#elif defined(Darwin)
	/** XXX \todo Fill in Darwin */
	if( !dpyName )
		crDebug("tilesortspu_CreateContext: dont' know what to do without a dpyName!");
	else
		contextInfo->context = (AGLContext) crStrToInt(dpyName);
#else
	/* GLX */
	if (!dpyName || dpyName[0] == 0)
		contextInfo->dpy = XOpenDisplay(tilesort_spu.displayString);
	else
		contextInfo->dpy = XOpenDisplay(dpyName);
	if (contextInfo->dpy) {
		crDebug("Tilesort SPU: Opened display %s", DisplayString(contextInfo->dpy));
	}
	else {
		crWarning("Tilesort SPU: CreateContext: Failed to open display %s",
							dpyName);
	}
#endif

	crPackSetContext( thread0->packer );
	/*
	 * Allocate the state tracker state for this context.
	 * The GL limits were computed in tilesortspuGatherConfiguration().
	 */
	contextInfo->State = crStateCreateContext( &tilesort_spu.limits, visBits, NULL );
	if (!contextInfo->State) {
		crWarning( "tilesortspuCreateContext: crStateCreateContext() failed");
#ifdef CHROMIUM_THREADSAFE
		crUnlockMutex(&_TileSortMutex);
#endif
		return 0;  /* out of memory? */
	}

#ifdef CR_ARB_vertex_buffer_object
	contextInfo->State->bufferobject.retainBufferData = GL_TRUE;
#endif

	/* Initialize viewport & scissor */
	/* Set to -1 then set them for the first time in MakeCurrent */
	contextInfo->State->viewport.viewportW = -1;
	contextInfo->State->viewport.viewportH = -1;
	contextInfo->State->viewport.scissorW = -1;
	contextInfo->State->viewport.scissorH = -1;

	contextInfo->providedBBOX = GL_DEFAULT_BBOX_CR;
	contextInfo->stereoDestFlags = EYE_LEFT | EYE_RIGHT;

	/* Set the Current pointers now...., then reset vtx_count below... */
	crStateSetCurrentPointers( contextInfo->State, &(thread0->packer->current) );

	/* Set the vtx_count to nil, this MUST come after the
	 * crStateSetCurrentPointers above... */
	contextInfo->State->current.current->vtx_count = 0;

	/*
	 * Per-server context stuff.
	 */
	contextInfo->server = (ServerContextInfo *) crCalloc(tilesort_spu.num_servers * sizeof(ServerContextInfo));

	/*
	 * Allocate a CRContext for each server and initialize its buffer.
	 * This was originally in tilesortSPUInit()
	 */
	for (i = 0; i < tilesort_spu.num_servers; i++)
	{
		contextInfo->server[i].State = crStateCreateContext( &tilesort_spu.limits,
																												 visBits, NULL );
		crStateSetCurrentPointers( contextInfo->server[i].State,
															 &(thread0->packer->current) );
	}

	/*
	 * Send a CreateContext msg to each server.
	 * We send it via the zero-th thread's server connections.
	 */
	for (i = 0; i < tilesort_spu.num_servers; i++)
	{
		int writeback = 1;
		GLint return_val = 0;

		crPackSetBuffer( thread0->packer, &(thread0->buffer[i]) );

		if (tilesort_spu.swap)
			crPackCreateContextSWAP( dpyName, visBits, shareCtx, &return_val, &writeback);
		else
			crPackCreateContext( dpyName, visBits, shareCtx, &return_val, &writeback );

		/* release server buffer */
		crPackReleaseBuffer( thread0->packer );

		/* Flush buffer (send to server) */
		tilesortspuSendServerBufferThread( i, thread0 );

		if (!thread0->netServer[0].conn->actual_network)
		{
			/* HUMUNGOUS HACK TO MATCH SERVER NUMBERING (from packspu_context.c)
			 *
			 * The hack exists solely to make file networking work for now.  This
			 * is totally gross, but since the server expects the numbers to start
			 * from 5000, we need to write them out this way.  This would be
			 * marginally less gross if the numbers (500 and 5000) were maybe
			 * some sort of #define'd constants somewhere so the client and the
			 * server could be aware of how each other were numbering things in
			 * cases like file networking where they actually
			 * care. 
			 *
			 * 	-Humper 
			 *
			 */
			return_val = 5000;
		}
		else
		{
			/* Get return value */
			while (writeback) {
				crNetRecv();
			}

			if (tilesort_spu.swap)
				return_val = (GLint) SWAP32(return_val);

			if (!return_val)
				return 0;  /* something went wrong on the server */
		}

		contextInfo->server[i].serverContext = return_val;
	}

	/* The default pack buffer */
	crPackSetBuffer( thread0->packer, &(thread0->geometry_buffer) );

#ifdef CHROMIUM_THREADSAFE
	crUnlockMutex(&_TileSortMutex);
#endif

	contextInfo->id = freeContextID;
	crHashtableAdd(tilesort_spu.contextTable, freeContextID, contextInfo);

	{
		/* Our configuration for the DLM we're going to create.
		 * It says that we want the DLM to handle everything
		 * itself, making display lists transparent to 
		 * the host SPU.
		 */
		CRDLMConfig dlmConfig = {
			CRDLM_DEFAULT_BUFFERSIZE,	/* bufferSize */
		};

		/* Supplement that with our DLM.  In a more correct situation, we should
		 * see if we've been called through glXCreateContext, which has a parameter
		 * for sharing DLMs.  We don't currently get that information, so for now
		 * give each context its own DLM.
		 */

		if (!tilesort_spu.dlm) {
			tilesort_spu.dlm = crDLMNewDLM(sizeof(dlmConfig), &dlmConfig);
			if (!tilesort_spu.dlm) {
				crDebug("tilesort: couldn't get DLM!");
			}
			//crDLMErrorFunction(ErrorCallback);
		}

		contextInfo->dlmContext = crDLMNewContext(tilesort_spu.dlm);
		if (!contextInfo->dlmContext) {
			crDebug("tilesort: couldn't get dlmContext");
			/** XXX \todo need graceful error handling here */
		}

		/* We're not going to hold onto the dlm ourselves, so we can
		 * free it.  It won't be actually freed until all the structures
		 * that refer to it (like our dlmContext) are freed.
		 */
		/*
		crDLMFreeDLM(dlm);
		*/

		contextInfo->displayListMode = GL_FALSE;
		contextInfo->displayListIdentifier = 0;
	}

	return freeContextID++;
}



void TILESORTSPU_APIENTRY tilesortspu_MakeCurrent( GLint window, GLint nativeWindow, GLint ctx )
{
	GET_THREAD(thread);
	ContextInfo *newCtx;
	int i;
	WindowInfo *winInfo;

#ifdef CHROMIUM_THREADSAFE
	if (!thread)
		thread = tilesortspuNewThread();
#endif
	CRASSERT(thread);

	if (thread->currentContext)
		tilesortspuFlush( thread );

	if (ctx) {
		newCtx = (ContextInfo *) crHashtableSearch(tilesort_spu.contextTable, ctx);
		CRASSERT(newCtx);

		winInfo = tilesortspuGetWindowInfo(window, nativeWindow);
		CRASSERT(winInfo);

		/* Now's our chance to grab the native window handle and do special
		 * stuff related to it.  For example, with GLX nativeWindow will be
		 * the X window ID of the application-created Xwindow.  Later, we'll
		 * use XGetGeometry() to query that window's size and update our mural,
		 * etc.
		 */
#if defined(WINDOWS)
		winInfo->client_hwnd = WindowFromDC( newCtx->client_hdc );
#elif defined(Darwin)
		/** XXX \todo Fill in Darwin */
		/* If Darwin needs to be aware of some native application window
		 * information. Grab 'nativeWindow' now.
		 */
#else
		/* GLX / DMX */
		{
			const GLboolean newDisplay = winInfo->dpy ? GL_FALSE : GL_TRUE;
			winInfo->dpy = newCtx->dpy;
			if (tilesort_spu.useDMX && newDisplay) {
				/* This is the first time we're using this window.
				 * Check if it's a DMX window.
				 */
#if USE_DMX
				if (crDMXSupported(winInfo->dpy))
					winInfo->isDMXWindow = GL_TRUE;
				else
					crWarning("tilesort SPU: use_dmx is set, but %s doesn't support DMX",
										DisplayString(winInfo->dpy));
#else /* USE_DMX */
				crWarning("tilesort SPU: use_dmx is set, but Chromium wasn't compiled with DMX support");
#endif /* USE_DMX */
			}
		}
#endif /* WINDOWS */

		thread->currentContext = newCtx;
		thread->currentContext->currentWindow = winInfo;

		crStateSetCurrentPointers( newCtx->State, &(thread->packer->current) );

		/** XXX \todo this might be excessive to do here */
		/* have to do it at least once for new windows to get back-end info */
		(void) tilesortspuUpdateWindowInfo(winInfo);
	}
	else {
		winInfo = NULL;
		thread->currentContext = NULL;
		newCtx = NULL;
	}

	/* release geometry buffer */
	crPackReleaseBuffer( thread->packer );

	if (newCtx) {
		crPackSetContext( thread->packer );
		crStateSetCurrent( newCtx->State );
		crStateFlushArg( (void *) thread );
	}
	else {
		crStateSetCurrent( NULL );
		crStateFlushArg( NULL );
	}

	/*
	 * Send MakeCurrent msg to each server using thread 0's server connections.
	 */
	for (i = 0; i < tilesort_spu.num_servers; i++)
	{
		/* Now send MakeCurrent to the i-th server */
		crPackSetBuffer( thread->packer, &(thread->buffer[i]) );

		if (ctx) {
			const int serverCtx = newCtx ? newCtx->server[i].serverContext : 0;
			int serverWindow;

			if (winInfo) {
#ifdef USE_DMX
				if (tilesort_spu.useDMX) {
					/* translate nativeWindow to a back-end child window ID */
					nativeWindow = (GLint) winInfo->backendWindows[i].xsubwin;
				}
#endif
				/* translate Cr window number to server window number */
				serverWindow = winInfo->server[i].window;
			}
			else {
				serverWindow = 0;
			}

			if (tilesort_spu.swap)
				crPackMakeCurrentSWAP( serverWindow, nativeWindow, serverCtx );
			else
				crPackMakeCurrent( serverWindow, nativeWindow, serverCtx );
		}
		else {
			if (tilesort_spu.swap)
				crPackMakeCurrentSWAP( -1, 0, -1 );
			else
				crPackMakeCurrent( -1, 0, -1 );
		}

		/* release server buffer */
		crPackReleaseBuffer( thread->packer );
	}

	/* Both the context and window have these flags.  If either one is
	 * invalid, we have to reset the contexts' raster origins.
	 */
	newCtx->validRasterOrigin = GL_TRUE;
	winInfo->validRasterOrigin = GL_TRUE;

	/* Do one-time initializations */
	if (newCtx) {
		if (newCtx->State->viewport.viewportW == -1 ||
				newCtx->State->viewport.viewportH == -1) {
			/* set initial viewport and scissor bounds */
			tilesortspu_Viewport(0, 0, winInfo->lastWidth, winInfo->lastHeight);
			tilesortspu_Scissor(0, 0, winInfo->lastWidth, winInfo->lastHeight);
		}
	}

	/* Restore the default buffer */
	crPackSetBuffer( thread->packer, &(thread->geometry_buffer) );

	crDLMSetCurrentState(newCtx->dlmContext);

	if (newCtx && !newCtx->everCurrent) {
		/* This is the first time the context has been made current.  Query
		 * the servers' extension strings and update our notion of which
		 * extensions we have and don't have (for this context and the servers'
		 * contexts).  Omit for file network.
		 */
		if (thread->netServer->conn->actual_network) {
			int i;
			const GLubyte *ext = tilesortspuGetExtensionsString();
			crStateSetExtensionString( newCtx->State, ext );
			for (i = 0; i < tilesort_spu.num_servers; i++)
				crStateSetExtensionString(newCtx->server[i].State, ext);
			crFree((void *) ext);
		}
		newCtx->everCurrent = GL_TRUE;

		/* one-time stereo init */
		if (tilesort_spu.stereoMode != NONE || tilesort_spu.forceQuadBuffering) {
			tilesortspuStereoContextInit(newCtx);
		}
	}
}


void TILESORTSPU_APIENTRY tilesortspu_DestroyContext( GLint ctx )
{
	ThreadInfo *thread0 = &(tilesort_spu.thread[0]);
	ContextInfo *contextInfo;
	GET_THREAD(thread);
	int i;

	contextInfo = (ContextInfo *) crHashtableSearch(tilesort_spu.contextTable, ctx);
	if (!contextInfo)
		return;

	/* release geometry buffer */
	crPackReleaseBuffer( thread0->packer );

	if (thread->currentContext == contextInfo) {
		/* flush the current context */
		tilesortspuFlush(thread);
		/* unbind */
		crStateSetCurrent(NULL);
	}

	/*
	 * Send DestroyCurrent msg to each server using zero-th thread's connection.
	 */
	for (i = 0; i < tilesort_spu.num_servers; i++)
	{
		int serverCtx;

		crPackSetBuffer( thread0->packer, &(thread0->buffer[i]) );

		serverCtx = contextInfo->server[i].serverContext;

		if (tilesort_spu.swap)
			crPackDestroyContextSWAP( serverCtx );
		else
			crPackDestroyContext( serverCtx );

		/* release server buffer */
		crPackReleaseBuffer( thread0->packer );

		/* send the command to the server */
		tilesortspuSendServerBufferThread( i, thread0 );

		contextInfo->server[i].serverContext = 0;
		crStateDestroyContext(contextInfo->server[i].State);
		contextInfo->server[i].State = NULL;
	}

	/* Check if we're deleting the currently bound context */
	if (thread->currentContext == contextInfo) {
		/* unbind */
		thread->currentContext = NULL;
	}

	/* Destroy the tilesort state context */
	crFree(contextInfo->server);
	crStateDestroyContext(contextInfo->State);

	/* The default buffer */
	crPackSetBuffer( thread0->packer, &(thread->geometry_buffer) );

	crDLMFreeContext(contextInfo->dlmContext);

	crHashtableDelete(tilesort_spu.contextTable, ctx, crFree);
}


