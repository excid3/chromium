/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include "replicatespu.h"
#include "cr_environment.h"
#include "cr_error.h"
#include "cr_mem.h"
#include "cr_string.h"
#include "cr_packfunctions.h"
#include "cr_url.h"
#include "cr_process.h"

#include <arpa/inet.h>
#include <X11/Xmd.h>
#include <X11/extensions/vnc.h>


/**
 * When using crHashTableWalk to walk over textures, context, windows,
 * etc we need to know the current server inside some callbacks.
 * Use this var for that.
 */
static int NewServerIndex = -1;

/* This is the longest, in milliseconds, that we'll wait for a
 * XVncChromiumConnected event, after receiving an
 * XVncConnected event.
 */
#define MAX_WAIT_FOR_CHROMIUM_MS 1000



/**
 * This is called periodically to see if we have received any Xvnc
 * events from the X server's VNC module.
 */
void
replicatespuCheckVncEvents(void)
{

	if (replicate_spu.glx_display) {
		int waitForChromiumConnection = 0;
		while (XPending(replicate_spu.glx_display) || waitForChromiumConnection > 0) {
#if 1
			int VncConn = replicate_spu.VncEventsBase + XVncConnected;
			int VncDisconn = replicate_spu.VncEventsBase + XVncDisconnected;
#endif
			int VncChromiumConn = replicate_spu.VncEventsBase + XVncChromiumConnected;
			XEvent event;

			/* When a new VNC viewer attaches, sometimes the
			 * XVncConnected event can come quickly, but the 
			 * XVncChromiumConnected event can be delayed a bit.
			 * If this happens, and we don't catch the XVncChromiumConnected
			 * event in this loop, the application won't be able to
			 * connect to the new viewer until the application issues
			 * a glClear() or a buffer flush; in the case of a static
			 * application (one that just waits for an Expose event),
			 * this may never come, and the window may remain dark on
			 * the new viewer.
			 *
			 * To avoid this situation, once we receive an XVncConnected
			 * event, we remain in this loop until the corresponding
			 * XVncChromiumConnected event appears, or until we've waited
			 * about a second.
			 */
			if (waitForChromiumConnection > 0 && !XPending(replicate_spu.glx_display)) {
			    /* This allows us to exit if we've waited too long,
			     * on the order of 1000 milliseconds, or 1 second.
			     */
			    waitForChromiumConnection--;
			    if (waitForChromiumConnection == 0) {
				/* The main loop will exit, now that the
				 * waitForChromiumConnection variable has been
				 * reduced to 0.  Give a warning, just in case.
				 */
				crWarning("Replicate SPU: got a new viewer, but no associated Chromium connection");
			    }
			    else {
				/* Keep waiting. */
				crMsleep(1);
			    }
			    continue;
			}
		
			/* We must have an X event waiting if we get here. */
			XNextEvent (replicate_spu.glx_display, &event);
			if (event.type == VncChromiumConn) {
				XVncConnectedEvent *e = (XVncConnectedEvent*) &event;
				crWarning("Replicate SPU: new viewer detected.");

				if (e->ipaddress) {
					replicatespuReplicate(e->ipaddress);
				}
				else {
					crWarning("Replicate SPU: Someone connected, but with no ipaddress?");
				}

				/* If we waited at all, let a debugger know. */
				if (waitForChromiumConnection < MAX_WAIT_FOR_CHROMIUM_MS) {
					crDebug("Replicate SPU: waited %d ms for Chromium connection", MAX_WAIT_FOR_CHROMIUM_MS - waitForChromiumConnection);
				}
				/* If we were waiting for the Chromium connection to
				 * appear, we need wait no longer.
				 */
				waitForChromiumConnection = 0;
			} 
#if 1
			else if (event.type == VncConn) {
				XVncConnectedEvent *e = (XVncConnectedEvent*) &event;
				crWarning("Replicate SPU: Received VncConn from IP 0x%x, sock %d",
									(int) e->ipaddress, (int) e->connected);
				/* Wait for the Chromium connection event to appear, 
				 * just in case it isn't already pending, to 
				 * eliminate the race condition.
				 */
				waitForChromiumConnection = MAX_WAIT_FOR_CHROMIUM_MS;
			} 
			else if (event.type == VncDisconn) {
				XVncDisconnectedEvent *e = (XVncDisconnectedEvent*) &event;
				crWarning("Replicate SPU: Received VncDisconn, sock %d", (int) e->connected);
			}
#endif
		}
	}
}



#if 0
static void replicatespuDebugOpcodes( CRPackBuffer *pack )
{
	unsigned char *tmp;
	for (tmp = pack->opcode_start; tmp > pack->opcode_current; tmp--)
	{
		crDebug( "  %d (0x%p, 0x%p)", *tmp, tmp, pack->opcode_current );
	}
	crDebug( "\n" );
}
#endif

static int caught_x11_error = 0;

static int (*old_xerror_handler)( Display *dpy, XErrorEvent *ev);

static int x11_error_handler( Display *dpy, XErrorEvent *ev )
{
	if (ev->error_code == BadWindow ||
	    ev->error_code == BadGC ||
	    ev->error_code == BadPixmap ||
 	    ev->error_code == BadDrawable) {
		caught_x11_error = 1;
	}
	else {
		if (old_xerror_handler) {
			(*old_xerror_handler)( dpy, ev );
		}
		caught_x11_error = 0;
	}
	return 0;
}


/**
 * Called from crHashtableWalk().  Basically, setup args and call
 * crStateTextureObjectDiff().  This is used to replicate all our local
 * texture objects on the new server.
 */
static void
TextureObjDiffCallback( unsigned long key, void *data1, void *data2 )
{
	CRContext *ctx = (CRContext *) data2;
	CRTextureObj *tobj = (CRTextureObj *) data1;
	CRbitvalue *bitID = NULL, *nbitID = NULL; /* not used */
	GLboolean alwaysDirty = GL_TRUE;

	if (!tobj)
		return;

	crStateTextureObjectDiff(ctx, bitID, nbitID, tobj, alwaysDirty);
}



static void
replicatespuReplicateTextures(CRContext *tempState, CRContext *state)
{
	CRTextureState *texstate = &(state->texture);

	/* use unit 0 for sending textures */
	if (replicate_spu.swap)
		crPackActiveTextureARBSWAP(GL_TEXTURE0);
	else
		crPackActiveTextureARB(GL_TEXTURE0);

	/* replicate all texture objects */
	crHashtableWalk(state->shared->textureTable, TextureObjDiffCallback, tempState);

	/* default texture objects */
	{
		CRbitvalue *bitID = NULL, *nbitID = NULL; /* not used */
		GLboolean alwaysDirty = GL_TRUE;
		crStateTextureObjectDiff(tempState, bitID, nbitID,
														 &texstate->base1D, alwaysDirty);
		crStateTextureObjectDiff(tempState, bitID, nbitID,
														 &texstate->base2D, alwaysDirty);
		crStateTextureObjectDiff(tempState, bitID, nbitID,
														 &texstate->base3D, alwaysDirty);
		crStateTextureObjectDiff(tempState, bitID, nbitID,
														 &texstate->baseCubeMap, alwaysDirty);
		crStateTextureObjectDiff(tempState, bitID, nbitID,
														 &texstate->baseRect, alwaysDirty);
	}

	/* restore unit 0 bindings */
	if (replicate_spu.swap) {
		crPackActiveTextureARBSWAP(GL_TEXTURE0);
		crPackBindTextureSWAP(GL_TEXTURE_1D, texstate->unit[0].currentTexture1D->name);
		crPackBindTextureSWAP(GL_TEXTURE_2D, texstate->unit[0].currentTexture2D->name);
		crPackBindTextureSWAP(GL_TEXTURE_3D, texstate->unit[0].currentTexture3D->name);
		crPackBindTextureSWAP(GL_TEXTURE_CUBE_MAP_ARB, texstate->unit[0].currentTextureCubeMap->name);
		crPackBindTextureSWAP(GL_TEXTURE_RECTANGLE_NV, texstate->unit[0].currentTextureRect->name);
	}
	else {
		crPackActiveTextureARB(GL_TEXTURE0);
		crPackBindTexture(GL_TEXTURE_1D, texstate->unit[0].currentTexture1D->name);
		crPackBindTexture(GL_TEXTURE_2D, texstate->unit[0].currentTexture2D->name);
		crPackBindTexture(GL_TEXTURE_3D, texstate->unit[0].currentTexture3D->name);
		crPackBindTexture(GL_TEXTURE_CUBE_MAP_ARB, texstate->unit[0].currentTextureCubeMap->name);
		crPackBindTexture(GL_TEXTURE_RECTANGLE_NV, texstate->unit[0].currentTextureRect->name);
	}

	/* finally, set active texture unit again */
	crPackActiveTextureARB(GL_TEXTURE0 + texstate->curTextureUnit);
}


/**
 * Send over all the display lists for this context. The temporary
 * context should have all the client information needed, so that
 * we can restore correct client state after we're done.
 */
static void
replicatespuReplicateLists(CRContext *tempState, CRDLM *displayListManager)
{
	crDLMSetupClientState(&replicate_spu.diff_dispatch);
	crDLMSendAllDLMLists(displayListManager, 
											 &replicate_spu.diff_dispatch);
	crDLMRestoreClientState(&tempState->client, &replicate_spu.diff_dispatch);
}


void replicatespuRePositionWindow(WindowInfo *winInfo)
{
	Window root;
	int x, y;
	unsigned int width, height, bw, d;
	XWindowAttributes winAtt;

	old_xerror_handler = XSetErrorHandler( x11_error_handler );

	XSync(replicate_spu.glx_display, 0);
	XGetGeometry(replicate_spu.glx_display, (Window)winInfo->nativeWindow, 
		      &root, &x, &y, &width, &height, &bw, &d);
	XGetWindowAttributes(replicate_spu.glx_display, (Window)winInfo->nativeWindow, &winAtt);
#if 0
	XMoveWindow(replicate_spu.glx_display, winInfo->nativeWindow, x, y);
#else
	XResizeWindow(replicate_spu.glx_display, winInfo->nativeWindow, width, height-1);
	XResizeWindow(replicate_spu.glx_display, winInfo->nativeWindow, width, height);
#endif
	/* Check if the window is mapped */
	if (winAtt.map_state == IsViewable) 
		winInfo->viewable = GL_TRUE;
	else
		winInfo->viewable = GL_FALSE;

	XSync(replicate_spu.glx_display, 0);
	if (caught_x11_error) {
		caught_x11_error = 0;
	}
	XSetErrorHandler( old_xerror_handler );
}



/**
 * Callback called by crHashTableWalk() below.
 * Used to create viewer-side windows for all the application windows.
 */
static void
replicatespuReplicateWindow(unsigned long key, void *data1, void *data2)
{
	ThreadInfo *thread = (ThreadInfo *) data2;
	WindowInfo *winInfo = (WindowInfo *) data1;
	GLint window = 0;
	GLint writeback = 1;
	Window root;
	int x, y;
	unsigned int width, height, bw, d;
	GLboolean unviewable = GL_FALSE;
	XWindowAttributes winAtt;

	CRASSERT(NewServerIndex >= 0);

	/* clear this flag, in case it was still set from an old connection */
	winInfo->writeback[NewServerIndex] = 0;

	/**
	 * Get application window's attributes
	 */
	old_xerror_handler = XSetErrorHandler( x11_error_handler );
	XSync(replicate_spu.glx_display, 0);
	XGetGeometry(replicate_spu.glx_display, (Window)winInfo->nativeWindow,
							 &root, &x, &y, &width, &height, &bw, &d );
	XGetWindowAttributes(replicate_spu.glx_display,
											 (Window) winInfo->nativeWindow, &winAtt);
	XSetErrorHandler( old_xerror_handler );
	if (!caught_x11_error) {
		 unviewable = (winAtt.map_state == IsUnviewable);
	}
	caught_x11_error = 0;

	/*
	 * Create the server-side window
	 */
	if (replicate_spu.swap)
		crPackWindowCreateSWAP( replicate_spu.dpyName, winInfo->visBits, &window, &writeback);
	else
		crPackWindowCreate( replicate_spu.dpyName, winInfo->visBits, &window, &writeback);

	replicatespuFlushOne(thread, NewServerIndex);

	/* Get return value */
	while (writeback) {
		crNetRecv();
	}
	if (replicate_spu.swap)
		window = (GLint) SWAP32(window);

	/* save the server-side window ID */
	winInfo->id[NewServerIndex] = window;

	crDebug("Replicate SPU: created server-side window %d", window);

	if (window < 0) {
		crWarning("Replicate SPU: failed to create server-side window");
		return;
	}

	XVncChromiumMonitor(replicate_spu.glx_display,
											window, winInfo->nativeWindow);

	/*
	 * If the app window is not visible, hide the server-side window too.
	 */
	if (unviewable)
	{
		if (replicate_spu.swap)
			crPackWindowShowSWAP( window, GL_FALSE );
		else
			crPackWindowShow( window, GL_FALSE );
		
		replicatespuFlushOne(thread, NewServerIndex);
	}
}


/**
 * Callback called from crHashTableWalk() from in
 * replicatespuReplicate() to update window sizes.
 */
static void
replicatespuResizeWindows(unsigned long key, void *data1, void *data2)
{
	WindowInfo *winInfo = (WindowInfo *) data1;
	const int serverWin = winInfo->id[NewServerIndex];
	CRASSERT(NewServerIndex >= 0);

	if (winInfo->width > 0) {
		if (replicate_spu.swap)
			crPackWindowSizeSWAP( serverWin, winInfo->width, winInfo->height );
		else
			crPackWindowSize( serverWin, winInfo->width, winInfo->height );
	}

	/* XXX what's this for? */
#if 1
	if (winInfo->nativeWindow)
		replicatespuRePositionWindow(winInfo);
#endif
}


/**
 * Replicate our contexts on a new server (indicated by NewServerIndex).
 * XXX It may be a problem if we try to attach to a shared context,
 * when that shared context has not yet been created.
 */
static void
replicatespuReplicateContext(void *element, void *arg)
{
	GLint ctx = (GLint) element;
	ThreadInfo *thread = (ThreadInfo *) arg;
	ContextInfo *context = crHashtableSearch(replicate_spu.contextTable, ctx);
	ContextInfo *sharedContext = NULL;
	CRContext *tempState;
	GLint return_val = 0, shareCtx = context->shareCtx, sharedServerCtx = 0;
	int writeback;

	if (!context->State) { /* XXX need this? */
		crWarning("ReplicateSPU: replicating context with no state!");
		return;
	}

	if (shareCtx > 0) {
		sharedContext = (ContextInfo *)
			crHashtableSearch(replicate_spu.contextTable, shareCtx);
		if (sharedContext)
			sharedServerCtx = sharedContext->rserverCtx[NewServerIndex];
	}


	/*
	 * Send CreateContext to new server and get return value
	 */
	if (replicate_spu.swap)
		crPackCreateContextSWAP( replicate_spu.dpyName, context->visBits,
														 sharedServerCtx, &return_val, &writeback);
	else
		crPackCreateContext( replicate_spu.dpyName, context->visBits,
												 sharedServerCtx, &return_val, &writeback);
	replicatespuFlushOne(thread, NewServerIndex);
	writeback = 1;
	while (writeback)
		crNetRecv();
	if (replicate_spu.swap)
		return_val = (GLint) SWAP32(return_val);
	if (return_val <= 0) {
		crWarning("Replicate SPU: CreateContext failed");
		return;
	}

	context->rserverCtx[NewServerIndex] = return_val;

	/*
	 * Create a new CRContext record representing the state of the new
	 * server (all default state).  We'll diff against this to send all the
	 * needed state to the server.
	 * When done, we can dispose of this context.
	 */
	tempState = crStateCreateContext(NULL, context->visBits, NULL);

	/* Bind the remote context. The window's not really significant. */
	{
		int serverWindow;
		if (context->currentWindow)
			serverWindow = context->currentWindow->id[NewServerIndex];
		else
			serverWindow = 0;

		if (replicate_spu.swap)
			crPackMakeCurrentSWAP( serverWindow, 0, return_val );
		else
			crPackMakeCurrent( serverWindow, 0, return_val );
	}

	/* Send state differences, all texture objects and all display lists
	 * to the new server.
	 * XXX We could be more efficient; in the case of a shared context,
	 * we only need to replicate textures and display lists once...
	 */
	crStateDiffContext( tempState, context->State );
	replicatespuReplicateTextures(tempState, context->State);
	replicatespuReplicateLists(tempState, context->displayListManager);
			
	/* XXX this call may not be needed */
	replicatespuFlushOne(thread, NewServerIndex);

	/* Destroy the temporary context, no longer needed */
	crStateDestroyContext( tempState );
}


/**
 * Send an OpenGL glFlush command to the named server.
 * This is just used to test if a CRConnection is really valid.
 */
static void
FlushConnection(int i)
{
	GET_THREAD(thread);
	if (replicate_spu.swap)
		crPackFlushSWAP();
	else
		crPackFlush();
	replicatespuFlushOne(thread, i);
}


/**
 * This will be called by the Cr packer when it needs to send out a full
 * buffer.  During replication, we need to send to a specific server.
 */
static void
replicatespuFlushOnePacker(void *arg)
{
	ThreadInfo *thread = (ThreadInfo *) arg;
	CRASSERT(NewServerIndex != -1);
	replicatespuFlushOne(thread, NewServerIndex);
}

static void
replicatespuHugeOnePacker(CROpcode opcode, void *buf)
{
	CRASSERT(NewServerIndex != -1);
	replicatespuHugeOne(opcode, buf, NewServerIndex);
}


/**
 * This is the main routine responsible for replicating our GL state
 * for a new VNC viewer.  Called when we detect that a new VNC viewer
 * has been started.
 * \param ipaddress  the IP address where the new viewer is running.
 */
void
replicatespuReplicate(int ipaddress) 
{
	GET_THREAD(thread);
	struct in_addr addr;
	char *hosturl;
	char *ipstring;
	int i, r_slot;
	int serverPort;

	crDebug("Replicate SPU: Enter replicatespuReplicate(ipaddress=0x%x)", ipaddress);

	replicatespuFlushAll( (void *)thread );

#ifdef CHROMIUM_THREADSAFE_notyet
	crLockMutex(&_ReplicateMutex);
#endif

	/*
	 * Find empty slot.
	 * Slot 0 is the dummy slot (a non-existant server on devnull connection)
	 */
	for (r_slot = 1; r_slot < CR_MAX_REPLICANTS; r_slot++) {
		if (!IS_CONNECTED(replicate_spu.rserver[r_slot].conn))
			break;
	}
	if (r_slot == CR_MAX_REPLICANTS) {
		crWarning("Replicate SPU: no more replicant slots available");
		return;
	}

	/**
	 ** OK, now rserver[r_slot] is free for use.
	 **/

	/*
	 * At this time, we can only support one VNC viewer per host.
	 * Check for that here.
	 */
	for (i = 1; i < CR_MAX_REPLICANTS; i++) {
		if (replicate_spu.ipnumbers[i] == ipaddress) {
			CRConnection *conn = replicate_spu.rserver[i].conn;
			/* If the connection appears to be active, it may actually be a dangling
			 * connection.  Try flushing it now.  If flushing fails, the connection
			 * type will definitely be CR_NO_CONNECTION (which we test below).
			 */
			if (conn) {
				if (conn->type != CR_NO_CONNECTION) {
					FlushConnection(i);
				}
				if (conn->type != CR_NO_CONNECTION) {
					crWarning("Replicate SPU: Can't connect to multiple VNC viewers on one host.");
#if 0
					/* The above test isn't 100% reliable and can prevent successful
					 * restart of a viewer on a host.  For now, just print warning and
					 * continue.
					 */
					return;
#endif
				}
			}
		}
	}

	replicate_spu.ipnumbers[r_slot] = ipaddress;

	serverPort = replicate_spu.chromium_start_port + r_slot;

	if (replicate_spu.vncAvailable) {
		/* Send a ChromiumStart message to the VNC Server.  The VNC Server will
		 * in turn send a ChromiumStart message to all the attached VNC viewers.
		 * The VNC Viewer/Chromium module will tell its crserver that there's
		 * a new OpenGL client.
		 * We pass the mothership port and crserver port in this message.
		 */
		char protocol[100], hostname[1000];
		const char *mothershipURL;
		unsigned short mothershipPort;
		mothershipURL = crGetenv("CRMOTHERSHIP");
		crDebug("Replicate SPU: CRMOTHERSHIP env var = %s", mothershipURL);
		if (mothershipURL)
			crParseURL(mothershipURL, protocol, hostname, &mothershipPort,
								 DEFAULT_MOTHERSHIP_PORT);
		else
			mothershipPort = DEFAULT_MOTHERSHIP_PORT;
		crDebug("Replicate SPU: Sending ChromiumStart msg to VNC server, port=%d",
						serverPort);

		if (!XVncChromiumStart(replicate_spu.glx_display, ipaddress,
													 serverPort, mothershipPort)) {
			 crWarning("Replicate SPU: XVncChromiumStart() call failed");
		}
	}

	addr.s_addr = ipaddress;
	ipstring = inet_ntoa(addr);
	hosturl = crAlloc(crStrlen(ipstring) + 9);
	sprintf(hosturl, "tcpip://%s", ipstring);

	crDebug("Replicate SPU attaching to %s on port %d", hosturl, serverPort);

	/* connect to the remote VNC server */
	replicate_spu.rserver[r_slot].name = crStrdup( replicate_spu.name );
	replicate_spu.rserver[r_slot].buffer_size = replicate_spu.buffer_size;
	replicate_spu.rserver[r_slot].conn
		= crNetConnectToServer( hosturl, serverPort,
														replicate_spu.rserver[r_slot].buffer_size, 1);

	if (!replicate_spu.rserver[r_slot].conn) {
		crWarning("Replicate SPU: failed to connect to %s", hosturl);
		return;
	}

	CRASSERT(replicate_spu.rserver[r_slot].conn);

	/*
	 * Setup the the packer flush function.  While replicating state to
	 * a particular server we definitely don't want to do any broadcast
	 * flushing!
	 */
	crPackFlushFunc( thread->packer, replicatespuFlushOnePacker );
	crPackSendHugeFunc( thread->packer, replicatespuHugeOnePacker );

	NewServerIndex = r_slot;

	/*
	 * Create server-side windows and contexts by walking tables of app windows
	 * and contexts.  Note that windows can be created in random order,
	 * but contexts must be created in the order they were originally
	 * created, or shared contexts will break.
	 */
	crHashtableWalk(replicate_spu.windowTable, replicatespuReplicateWindow, thread);
	crListApply(replicate_spu.contextList, replicatespuReplicateContext, thread);

	/* MakeCurrent, the current context */
	if (thread->currentContext) {
		int serverWindow = thread->currentContext->currentWindow->id[r_slot];
		int serverContext = thread->currentContext->rserverCtx[r_slot];
		if (replicate_spu.swap)
			crPackMakeCurrentSWAP(serverWindow, 0, serverContext);
	 	else
			crPackMakeCurrent(serverWindow, 0, serverContext);

		crStateMakeCurrent( thread->currentContext->State );
	}

	/*
	 * Set window sizes
	 */
	crHashtableWalk(replicate_spu.windowTable, replicatespuResizeWindows, thread);
	replicatespuFlushOne(thread, NewServerIndex);

	/*
	 * restore normal, broadcasting packer flush function now.
	 */
	crPackFlushFunc( thread->packer, replicatespuFlush );
	crPackSendHugeFunc( thread->packer, replicatespuHuge );

	NewServerIndex = -1;

	crDebug("Replicate SPU: leaving replicatespuReplicate");

#ifdef CHROMIUM_THREADSAFE_notyet
	crUnlockMutex(&_ReplicateMutex);
#endif
}
