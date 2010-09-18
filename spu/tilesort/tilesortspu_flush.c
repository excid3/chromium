/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include <math.h>
#include <float.h>
#include "tilesortspu.h"
#include "tilesortspu_gen.h"
#include "cr_pack.h"
#include "cr_net.h"
#include "cr_mem.h"
#include "cr_protocol.h"
#include "cr_error.h"
#include "cr_packfunctions.h"
#include "cr_rand.h"

/**
 * The message header lies at the start of the packing buffer, just before
 * the actual operand data.  In preparation for transmission, fill in the
 * message header fields (type and numOpcodes).
 * Input: the packing buffer
 * Output: len - number of bytes to send, including the header.
 * Return: pointer to the header info.
 */
static CRMessageOpcodes *
__applySendBufferHeader( CRPackBuffer *pack, unsigned int *len )
{
	const int num_opcodes = crPackNumOpcodes(pack);
	CRMessageOpcodes *hdr;

	CRASSERT( pack->opcode_current < pack->opcode_start );
	CRASSERT( pack->opcode_current >= pack->opcode_end );
	CRASSERT( pack->data_current > pack->data_start );
	CRASSERT( pack->data_current <= pack->data_end );

	hdr = (CRMessageOpcodes *) 
		( pack->data_start - ( ( num_opcodes + 3 ) & ~0x3 ) - sizeof(*hdr) );

	CRASSERT( (void *) hdr >= pack->pack );

	if (tilesort_spu.swap)
	{
		hdr->header.type= (CRMessageType) SWAP32(CR_MESSAGE_OPCODES);
		hdr->numOpcodes  = SWAP32(num_opcodes);
	}
	else
	{
		hdr->header.type = CR_MESSAGE_OPCODES;
		hdr->numOpcodes  = num_opcodes;
	}

	*len = pack->data_current - (unsigned char *) hdr;

	return hdr;
}


/**
 * Print all the opcodes in the given buffer.
 */
void tilesortspuDebugOpcodes( CRPackBuffer *buffer )
{
	unsigned char *tmp;
	for (tmp = buffer->opcode_start; tmp > buffer->opcode_current; tmp--)
	{
		crDebug( "  %d (0x%p, 0x%p)", *tmp, tmp, buffer->opcode_current );
	}
	crDebug( "\n" );
}


/**
 * Send this thread's packing buffer to the named server using a
 * specific thread's packer.
 */
void tilesortspuSendServerBufferThread( int server_index, ThreadInfo *thread )
{
	CRPackBuffer *buffer;
	CRNetServer *netServer;
	unsigned int len;
	CRMessageOpcodes *hdr;

	CRASSERT(server_index >= 0);
	CRASSERT(server_index < tilesort_spu.num_servers);

	buffer = &(thread->buffer[server_index]);
	netServer = &(thread->netServer[server_index]);

#if 0
	/* buffer should not be bound at this time */
	CRASSERT(buffer->context == NULL);
#endif

	if (crPackNumOpcodes(buffer) == 0) {
		/* buffer is empty */
		CRASSERT(crPackNumData(buffer) == 0);
		return;
	}

	hdr = __applySendBufferHeader( buffer, &len );

	if ( buffer->holds_BeginEnd && buffer->canBarf ) {
		crNetBarf( netServer->conn, &buffer->pack, hdr, len );
	}
	else {
		crNetSend( netServer->conn, &buffer->pack, hdr, len );
	}

	crPackInitBuffer( buffer, crNetAlloc( netServer->conn ),
										netServer->conn->buffer_size, netServer->conn->mtu );
	buffer->canBarf = netServer->conn->Barf ? GL_TRUE : GL_FALSE;
}

/**
 * As above, but use the calling thread.
 */
void tilesortspuSendServerBuffer( int server_index )
{
	GET_THREAD(thread);
	tilesortspuSendServerBufferThread(server_index, thread);
}


/**
 * Append the given buffer onto the current packing buffer.
 */
static void
__appendBuffer( const CRPackBuffer *src )
{
	GET_THREAD(thread);

	/*crWarning( "In __appendBuffer: %d bytes left, packing %d bytes", thread->packer->buffer.data_end - thread->packer->buffer.data_current, num_data ); */

	if ( !crPackCanHoldBuffer(src)
			 || thread->packer->buffer.holds_BeginEnd ^ src->holds_BeginEnd)
	{
		/* No room to append -- send now */

		/*crWarning( "OUT OF ROOM!");*/
		crPackReleaseBuffer( thread->packer );

		tilesortspuSendServerBuffer( thread->state_server_index );

		crPackSetBuffer( thread->packer, &(thread->buffer[thread->state_server_index]) );

		CRASSERT(crPackCanHoldBuffer( src ));
	}

	crPackAppendBuffer( src );
	/*crWarning( "Back from crPackAppendBuffer: 0x%x", thread->packer->buffer.data_current ); */
}


/**
 * As above, but with bounding box info.
 */
static void 
__appendBoundedBuffer( const CRPackBuffer *src, const CRrecti *bounds )
{
	GET_THREAD(thread);
	int length = src->data_current - src->opcode_current - 1;

	if (length == 0)
	{
		/* nothing to send. */
		return;
	}

	if ( !crPackCanHoldBoundedBuffer( src )
		|| thread->packer->buffer.holds_BeginEnd ^ src->holds_BeginEnd)
	{
		/* No room to append -- send now */
		crPackReleaseBuffer( thread->packer );
		tilesortspuSendServerBuffer( thread->state_server_index );
		crPackSetBuffer( thread->packer, &(thread->buffer[thread->state_server_index]) );
		CRASSERT(crPackCanHoldBoundedBuffer( src ) || src->holds_BeginEnd );
	}

	crPackAppendBoundedBuffer( src, bounds );
}

void tilesortspuShipBuffers( void )
{
	int i;
	for (i = 0 ; i < tilesort_spu.num_servers; i++)
	{
		tilesortspuSendServerBuffer( i );
	}
}


/**
 * This function is called by the packer when it's got to pack a command
 * that's too large to fit in a normal buffer (such as a big glTexImage2D
 * call).
 * 'buf' will point to the first byte of the GL command parameters (such
 * as glTexImage2D's 'target' parameter.
 * 'opcode' should be the CR opcode, like CR_TEXIMAGE2D_OPCODE.
 */
/**
 * XXX NOTE: there's a lot of duplicate code here common to the
 * pack, tilesort and replicate SPUs.  Try to simplify someday!
 */
void tilesortspuHuge( CROpcode opcode, void *buf )
{
	GET_THREAD(thread);
	unsigned int          len;
	unsigned char        *src;
	CRMessageOpcodes *msg;

	if (thread->currentContext->inDrawPixels)
	{
		tilesortspuFlush( thread );
		return;
	}

	if (thread->currentContext->inZPix)
	{
		tilesortspuFlush( thread );
		return;
	}


	/* packet length is indicated by the variable length field, and
	   includes an additional word for the opcode (with alignment) and
	   a header */
	len = ((unsigned int *) buf)[-1] + 4 + sizeof(CRMessageOpcodes);

	/* Write the opcode in just before the length.
	 * Recall that operands get stored upward through memory while
	 * opcodes go downward.  This next instruction puts the single
	 * opcode just below the first operand data.
	 * With huge buffers, the first 4-byte word of operand data is
	 * always the total command length.
	 */
	((unsigned char *) buf)[-5] = (unsigned char) opcode;

	/* The start of the buffer we're going to send really starts
	 * before the 'buf' pointer.  We skip (downward) the 4-byte space
	 * for opcodes, the 4-byte command size word, and the
	 * sizeof(CRMessageOpcodes).
	 */
	src = (unsigned char *) buf - 8 - sizeof(CRMessageOpcodes);

	msg = (CRMessageOpcodes *) src;

	/* we only transmit one GL command per huge buffer, actually */
	if (tilesort_spu.swap)
	{
		msg->header.type = (CRMessageType) SWAP32(CR_MESSAGE_OPCODES);
		msg->numOpcodes  = SWAP32(1);
	}
	else
	{
		msg->header.type = CR_MESSAGE_OPCODES;
		msg->numOpcodes  = 1;
	}

	/* the pipeserver's buffer might have data in it, and that should
       go across the wire before this big packet */
#if 1
	/* This seems to solve a problem found with OpenSceneGraph and compressed
	 * textures.  If we get a huge packet outside of state differencing,
	 * broadcast it.
	 */
	if (thread->state_server_index == -1) {
		int i;
		for (i = 0; i < tilesort_spu.num_servers; i++) {
			thread->state_server_index = i;
			tilesortspuSendServerBuffer(i);
			crNetSend( thread->netServer[i].conn, NULL, src, len );
		}
		thread->state_server_index = -1;
	}
	else {
		tilesortspuSendServerBuffer( thread->state_server_index );
		CRASSERT(thread->state_server_index >= 0);
		crNetSend( thread->netServer[thread->state_server_index].conn, NULL, src, len );
	}
#else
	tilesortspuSendServerBuffer( thread->state_server_index );
	CRASSERT(thread->state_server_index >= 0);
	crNetSend( thread->netServer[thread->state_server_index].conn, NULL, src, len );
#endif
}

static void __drawBBOX(const TileSortBucketInfo * bucket_info)
{
	GET_THREAD(thread);
#define DRAW_BBOX_MAX_SERVERS 128
	static int init=0;
	static GLfloat c[DRAW_BBOX_MAX_SERVERS][3];
	unsigned int i;
	CRbitvalue a;
	GLfloat outcolor[3] = {0.0f, 0.0f, 0.0f};
	GLfloat tot;
	GLfloat xmin = bucket_info->objectMin.x;
	GLfloat xmax = bucket_info->objectMax.x;
	GLfloat ymin = bucket_info->objectMin.y;
	GLfloat ymax = bucket_info->objectMax.y;
	GLfloat zmin = bucket_info->objectMin.z;
	GLfloat zmax = bucket_info->objectMax.z;

	if (!init) 
	{
		/* pick random colors for bounding boxes */
		for (i=0; i<DRAW_BBOX_MAX_SERVERS; i++) 
		{
			c[i][0] = crRandFloat(0.0, 100.0);
			c[i][1] = crRandFloat(0.0, 100.0);
			c[i][2] = crRandFloat(0.0, 100.0);
			tot = (GLfloat) sqrt (c[i][0]*c[i][0] + c[i][1]*c[i][1] + c[i][2]*c[i][2]);
			c[i][0] /= tot;
			c[i][1] /= tot;
			c[i][2] /= tot;
		}
		init = 1;
	}		

	/* find average of colors for dirty/hit servers */
	tot = 0.0f;
	for (i=0, a=1; i<DRAW_BBOX_MAX_SERVERS; i++, a <<= 1)
	{
#if 0
		if (CHECKDIRTY(bucket_info->hits, a)) 
#endif
		{
			outcolor[0] += c[i][0];
			outcolor[1] += c[i][1];
			outcolor[2] += c[i][2];
			tot+=1.0f;
		}
	}
	outcolor[0] /= tot;
	outcolor[1] /= tot;
	outcolor[2] /= tot;

	if (tilesort_spu.swap)
	{
		if (thread->currentContext->providedBBOX == GL_SCREEN_BBOX_CR)
			crPackPushAttribSWAP(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_TRANSFORM_BIT);
		else
			crPackPushAttribSWAP(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_LINE_BIT);
		crPackDisableSWAP(GL_TEXTURE_2D);
		crPackDisableSWAP(GL_TEXTURE_1D);
		crPackDisableSWAP(GL_LIGHTING);
		crPackDisableSWAP(GL_BLEND);
		crPackDisableSWAP(GL_ALPHA_TEST);
		crPackDisableSWAP(GL_DEPTH_TEST);
		crPackDisableSWAP(GL_FOG);
		crPackDisableSWAP(GL_STENCIL_TEST);
		crPackDisableSWAP(GL_SCISSOR_TEST);
		crPackDisableSWAP(GL_LOGIC_OP);

		crPackLineWidthSWAP(tilesort_spu.bboxLineWidth);
		crPackColor3fvSWAP(outcolor);

		if (thread->currentContext->providedBBOX == GL_SCREEN_BBOX_CR) {
			crPackMatrixModeSWAP( GL_MODELVIEW ) ;
			crPackPushMatrixSWAP() ;
			crPackLoadIdentitySWAP() ;
			crPackMatrixModeSWAP( GL_PROJECTION ) ;
			crPackPushMatrixSWAP() ;
			crPackLoadIdentitySWAP() ;

			crPackBeginSWAP(GL_LINE_LOOP);
			crPackVertex2fSWAP(xmin, ymin);
			crPackVertex2fSWAP(xmax, ymin);
			crPackVertex2fSWAP(xmax, ymax);
			crPackVertex2fSWAP(xmin, ymax);
			crPackEndSWAP();

			crPackPopMatrixSWAP() ;
			crPackMatrixModeSWAP( GL_MODELVIEW ) ;
			crPackPopMatrixSWAP() ;
		} else {
			crPackBeginSWAP(GL_LINE_LOOP);
			crPackVertex3fSWAP(xmin, ymin, zmin);
			crPackVertex3fSWAP(xmin, ymin, zmax);
			crPackVertex3fSWAP(xmin, ymax, zmax);
			crPackVertex3fSWAP(xmin, ymax, zmin);
			crPackEndSWAP();
			crPackBeginSWAP(GL_LINE_LOOP);
			crPackVertex3fSWAP(xmax, ymin, zmin);
			crPackVertex3fSWAP(xmax, ymin, zmax);
			crPackVertex3fSWAP(xmax, ymax, zmax);
			crPackVertex3fSWAP(xmax, ymax, zmin);
			crPackEndSWAP();
			crPackBeginSWAP(GL_LINE_LOOP);
			crPackVertex3fSWAP(xmin, ymin, zmin);
			crPackVertex3fSWAP(xmax, ymin, zmin);
			crPackVertex3fSWAP(xmax, ymax, zmin);
			crPackVertex3fSWAP(xmin, ymax, zmin);
			crPackEndSWAP();
			crPackBeginSWAP(GL_LINE_LOOP);
			crPackVertex3fSWAP(xmin, ymin, zmax);
			crPackVertex3fSWAP(xmax, ymin, zmax);
			crPackVertex3fSWAP(xmax, ymax, zmax);
			crPackVertex3fSWAP(xmin, ymax, zmax);
			crPackEndSWAP();
		}

		crPackPopAttribSWAP();
	}
	else
	{
		if (thread->currentContext->providedBBOX == GL_SCREEN_BBOX_CR)
			crPackPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_TRANSFORM_BIT);
		else
			crPackPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_LINE_BIT);
		crPackDisable(GL_TEXTURE_2D);
		crPackDisable(GL_TEXTURE_1D);
		crPackDisable(GL_LIGHTING);
		crPackDisable(GL_BLEND);
		crPackDisable(GL_ALPHA_TEST);
		crPackDisable(GL_DEPTH_TEST);
		crPackDisable(GL_FOG);
		crPackDisable(GL_STENCIL_TEST);
		crPackDisable(GL_SCISSOR_TEST);
		crPackDisable(GL_LOGIC_OP);

		crPackLineWidth(tilesort_spu.bboxLineWidth);
		crPackColor3fv(outcolor);
		if (thread->currentContext->providedBBOX == GL_SCREEN_BBOX_CR) {
			crPackMatrixMode( GL_MODELVIEW ) ;
			crPackPushMatrix() ;
			crPackLoadIdentity() ;
			crPackMatrixMode( GL_PROJECTION ) ;
			crPackPushMatrix() ;
			crPackLoadIdentity() ;

			crPackBegin(GL_LINE_LOOP);
			crPackVertex2f(xmin, ymin);
			crPackVertex2f(xmax, ymin);
			crPackVertex2f(xmax, ymax);
			crPackVertex2f(xmin, ymax);
			crPackEnd();

			crPackPopMatrix() ;
			crPackMatrixMode( GL_MODELVIEW ) ;
			crPackPopMatrix() ;
		} else {
			crPackBegin(GL_LINE_LOOP);
			crPackVertex3f(xmin, ymin, zmin);
			crPackVertex3f(xmin, ymin, zmax);
			crPackVertex3f(xmin, ymax, zmax);
			crPackVertex3f(xmin, ymax, zmin);
			crPackEnd();
			crPackBegin(GL_LINE_LOOP);
			crPackVertex3f(xmax, ymin, zmin);
			crPackVertex3f(xmax, ymin, zmax);
			crPackVertex3f(xmax, ymax, zmax);
			crPackVertex3f(xmax, ymax, zmin);
			crPackEnd();
			crPackBegin(GL_LINE_LOOP);
			crPackVertex3f(xmin, ymin, zmin);
			crPackVertex3f(xmax, ymin, zmin);
			crPackVertex3f(xmax, ymax, zmin);
			crPackVertex3f(xmin, ymax, zmin);
			crPackEnd();
			crPackBegin(GL_LINE_LOOP);
			crPackVertex3f(xmin, ymin, zmax);
			crPackVertex3f(xmax, ymin, zmax);
			crPackVertex3f(xmax, ymax, zmax);
			crPackVertex3f(xmin, ymax, zmax);
			crPackEnd();
		}

		crPackPopAttrib();
	}
}

static void
doFlush( CRContext *ctx, GLboolean broadcast, GLboolean send_state_anyway,
				 TileSortBucketInfo *bucket_info)
{
	GET_THREAD(thread);
	WindowInfo *winInfo = thread->currentContext->currentWindow;
	CRMessageOpcodes *big_packet_hdr = NULL;
	unsigned int big_packet_len = 0;
	TileSortBucketInfo local_bucket_info;
	int i;

	if (!bucket_info)
		bucket_info = &local_bucket_info;

	/*crDebug( "in doFlush (broadcast = %d)", broadcast ); */

	if (thread->state_server_index != -1)
	{
		/* This means that the context differencer had so much state 
		 * to dump that it overflowed the server's network buffer 
		 * while doing its difference.  In this case, we just want 
		 * to get the buffer out the door so the differencer can 
		 * keep doing what it's doing. */

		/*crDebug( "Overflowed while doing a context difference!" ); */
		CRASSERT( !broadcast );
		
		/* Unbind the current buffer to sync it up */
		crPackReleaseBuffer( thread->packer );

		/* Now, get the buffer out of here. */
		tilesortspuSendServerBuffer( thread->state_server_index );
		
		/* Finally, restore the packing state so we can continue 
		 * This isn't the same as calling crPackResetPointers, 
		 * because the state server now has a new pack buffer 
		 * from the buffer pool. */

		crPackSetBuffer( thread->packer, &(thread->buffer[thread->state_server_index]) );

		return;
	}

	/*crDebug( "Looks like it was not a state overflow" ); */

	/* First, test to see if this is a big packet. */
	
	if (thread->packer->buffer.size > tilesort_spu.buffer_size )
	{
		big_packet_hdr = __applySendBufferHeader( &(thread->packer->buffer), &big_packet_len );
	}

	/* Here's the big part -- call the bucketer! */

	if (bucket_info != &local_bucket_info) {
		broadcast = GL_FALSE;
	}
	else if (!broadcast)
	{
		/* We've called the flush routine when no vertices
		 * have been sent. Therefore we have no bounding box.
		 * In this case, we have to broadcast, so set the
		 * broadcast flag and continue.
		 */
		if (thread->packer->bounds_min.x == FLT_MAX &&
		    thread->packer->bounds_max.x == -FLT_MAX) {
			/*crDebug("No vertices or bounding box set during this flush - Broadcasting geometry\n");*/
			broadcast = GL_TRUE;
			/* we'll broadcast the current geometry, but we still need to do
			 * state differencing to take care of outstanding state changes!
			 */
			send_state_anyway = GL_TRUE;
		} else {
			/*crDebug( "About to bucket the geometry" ); */
			bucket_info->objectMin = thread->packer->bounds_min;
			bucket_info->objectMax = thread->packer->bounds_max;
			tilesortspuBucketGeometry(winInfo, bucket_info);
			if (thread->currentContext->providedBBOX == GL_DEFAULT_BBOX_CR)
				crPackResetBoundingBox( thread->packer );
		}
	}
	else
	{
		/*crDebug( "Broadcasting the geometry!" ); */
	}

	/* Now, we want to let the state tracker extract the "current" state 
	 * from the collection of pointers that we have in the geometry 
	 * buffer. */
	crStateCurrentRecover( );

	/* At this point, we invoke the "pincher", which will check if 
	 * the current buffer needs to be stopped mid-triangle (or whatever), 
	 * and tuck away the appropriate state so that the triangle can be 
	 * re-started once this flush is done. */

	tilesortspuPinch();

	/* If GL_COLOR_MATERIAL is enabled, we need to copy the color values 
	 * into the state element for materials so that Gets will work and so 
	 * that the material will get sent down. 
	 * 
	 * We know we can do this now because this function will get called if 
	 * glDisable turns off COLOR_MATERIAL, so that would be the very very 
	 * latest time we could actually get away with that. */
	crStateColorMaterialRecover( );

	/* Okay.  Now, we need to un-hide the bonus space for the extra glEnd packet 
	 * and try to close off the begin/end if it exists.  This is a pretty 
	 * serious hack, but we *did* reserve space in the pack buffer just 
	 * for this purpose. */
	
	if ( thread->currentContext->State->current.inBeginEnd )
	{
		/*crDebug( "Closing this Begin/end!!!" ); */
		thread->packer->buffer.mtu += END_FLUFF + 4; /* 4 for opcode */
		if (tilesort_spu.swap)
		{
			crPackEndSWAP();
		}
		else
		{
			crPackEnd();
		}
	}

	/* At this point, the thread is currently packing into the thread's
	 * geometry buffer.
	 * Now we're about to do state differencing and send state change commands
	 * to the servers.
	 * Before we do so, we have to release the geometry buffer to make sure
	 * it's all synced up.
	 */
	crPackReleaseBuffer( thread->packer );

	/* Now, see if we need to do things to each server */

	for ( i = 0 ; i < tilesort_spu.num_servers; i++ )
	{
		const int node32 = i >> 5, node = i & 0x1f;

		thread->state_server_index = i;

		/* Check to see if this server needs geometry from us. */
		if (!broadcast && !(bucket_info->hits[node32] & (1 << node)))
		{
			/*crDebug( "NOT sending to server %d", i );*/
			continue;
		}
		/*crDebug( "Sending to server %d", i ); */

		/* Okay, it does.  */

		thread->currentContext->server[i].vertexCount += thread->packer->current.vtx_count;

		/* Set the destination for packing state difference commands */
		crPackSetBuffer( thread->packer, &(thread->buffer[thread->state_server_index]) );

		/* We're going to do lazy state evaluation now */
		if (!broadcast || send_state_anyway)
		{
			/*crDebug( "pack buffer before differencing" ); 
			 *tilesortspuDebugOpcodes( &(thread->packer->buffer) ); */
			CRContext *serverState = thread->currentContext->server[i].State;
			crStateDiffContext( serverState, ctx );
			if (tilesort_spu.drawBBOX && !broadcast)
			{
				__drawBBOX( bucket_info );
			}

			/* Unbind the buffer of state-change commands to sync it up before
			 * sending it to a server.
			 */
			crPackReleaseBuffer( thread->packer );

			/*tilesortspuDebugOpcodes( &(thread->state_server->pack) ); */
		}

		/* The state server's buffer now contains the commands 
		 * needed to bring it up to date on the state.  All 
		 * that's left to do is to append the geometry. */

		if ( big_packet_hdr != NULL )
		{
			/* The packet was big, so send the state commands 
			 * by themselves, followed by the big packet. */
			crDebug( "Sending big packet of %d bytes to server %d",
							 big_packet_len, i );

			/* Send state-change commands */
			tilesortspuSendServerBuffer( thread->state_server_index );
			/* send geometry */
			crNetSend( thread->netServer[thread->state_server_index].conn, NULL,
								 big_packet_hdr, big_packet_len );
		}
		else
		{
			/* Now, we have to decide whether or not to append the geometry buffer 
			 * as a BOUNDS_INFO packet or just a bag-o-commands.  			 
			 * 
			 * As it turns out, we *always* send geometry as a BOUNDS_INFO 
			 * packet.  All the code lying around to deal with non-bounds-info 
			 * geometry was just a red herring. 
			 * 
			 * Now we see why I tucked away the geometry buffer earlier. */

			/* We're about to append onto the per-server buffer */
			crPackSetBuffer( thread->packer, &(thread->buffer[thread->state_server_index]) );

			if ( !broadcast )
			{
				/*
				crDebug( "Appending a bounded buffer: %d, %d .. %d, %d",
								 bucket_info->pixelBounds.x1, 
								 bucket_info->pixelBounds.y1, 
								 bucket_info->pixelBounds.x2, 
								 bucket_info->pixelBounds.y2 );
				*/
				__appendBoundedBuffer( &(thread->geometry_buffer),
									   &bucket_info->pixelBounds );
			}
			else
			{
				/*
				crDebug( "Appending a NON-bounded buffer" ); 
				tilesortspuDebugOpcodes( &(thread->geometry_buffer) ); 
				tilesortspuDebugOpcodes( &(thread->packer->buffer) );
				*/
				__appendBuffer( &(thread->geometry_buffer) );
				/*tilesortspuDebugOpcodes( &(thread->packer->buffer) ); */
			}

			/* Release the buffer before sending, to sync it up */
			crPackReleaseBuffer( thread->packer );

		}
	} /* loop over servers */

	/* We're done with the servers.  Wipe the thread->state_server 
	 * variable so we know that.  */

	thread->state_server_index = -1;

	/* In case the network detected a smaller mtu,
	 * shrink buffer's and tilesortspu's mtu */
	for ( i = 0 ; i < tilesort_spu.num_servers; i++ )
	{
		if (thread->buffer[i].mtu > thread->netServer[i].conn->mtu)
			thread->buffer[i].mtu = thread->netServer[i].conn->mtu;
		if (tilesort_spu.MTU > thread->netServer[i].conn->mtu) {
			tilesort_spu.MTU = thread->netServer[i].conn->mtu;
		}
	}
	/* the geometry must also fit in the mtu */
	/* 24 is the size of the bounds info packet
	 * END_FLUFF is the size of data of the extra End opcode if needed
	 * 4 since BoundsInfo opcode may take a whole 4 bytes
	 * and 4 to let room for extra End's opcode, if needed
	 */
	if (tilesort_spu.geom_buffer_mtu >
		tilesort_spu.MTU - sizeof(CRMessageOpcodes) - (24+END_FLUFF+4+4))
		tilesort_spu.geom_buffer_mtu =
			tilesort_spu.MTU - sizeof(CRMessageOpcodes) - (24+END_FLUFF+4+4);
	thread->geometry_buffer.mtu = tilesort_spu.geom_buffer_mtu;

	if ( big_packet_hdr != NULL )
	{
		/* Throw away the big packet and setup a new one using the original
		 * geometry buffer size.
		 */
		void *p = crAlloc( tilesort_spu.geom_buffer_size );
		crDebug( "Throwing away the big packet" );
		crFree( thread->geometry_buffer.pack );
		crPackInitBuffer( &(thread->geometry_buffer),
											p, 
											tilesort_spu.geom_buffer_size,
											tilesort_spu.geom_buffer_mtu );

		crPackSetBuffer( thread->packer, &(thread->geometry_buffer ) );
		/* No need to call crPackResetPointers, crPackInitBuffer did it */
	}
	else
	{
		/*crDebug( "Reverting to the old geometry buffer" ); */
		crPackSetBuffer( thread->packer, &(thread->geometry_buffer ) );
		crPackResetPointers( thread->packer );
	}

	/*crDebug( "Resetting the current vertex count and friends" ); */

	thread->packer->current.vtx_count = 0;
	thread->packer->current.vtx_count_begin = 0;

	/*crDebug( "Setting all the Current pointers to NULL" ); */
	crPackNullCurrentPointers();

	tilesortspuPinchRestoreTriangle();
}

void tilesortspuBroadcastGeom( GLboolean send_state_anyway )
{
	GET_THREAD(thread);
	doFlush( thread->currentContext->State, GL_TRUE, send_state_anyway, NULL );
}


/**
 * This callback function gets called by the state tracker (when a state
 * change happens) and the packer (when a buffer is filled).
 */
void tilesortspuFlush_callback( void *arg )
{
	tilesortspuFlush( (ThreadInfo *) arg );
}


/**
 * Like the tilesortspuFlush() routine below, but only send buffered data
 * to the servers indicated by the bucket_info->hit[] arrays.
 */
void
tilesortspuFlushToServers(ThreadInfo *thread, TileSortBucketInfo *bucket_info)
{
	CRContext *ctx;

	CRASSERT(thread);
	CRASSERT(thread->currentContext);
	CRASSERT(thread->currentContext->State);

	ctx = thread->currentContext->State;
	doFlush( ctx, GL_FALSE, GL_FALSE, bucket_info );
}


void tilesortspuFlush( ThreadInfo *thread )
{
	CRContext *ctx;
	int newSize;
	CRPackBuffer old_geometry_buffer;

	CRASSERT(thread);
	CRASSERT(thread->currentContext);
	CRASSERT(thread->currentContext->State);

	ctx = thread->currentContext->State;

	/*crDebug( "In tilesortspuFlush" ); 
	 *crDebug( "BBOX MIN: (%f %f %f)", thread->packer->bounds_min.x, thread->packer->bounds_min.y, thread->packer->bounds_min.z ); 
	 *crDebug( "BBOX MAX: (%f %f %f)", thread->packer->bounds_max.x, thread->packer->bounds_max.y, thread->packer->bounds_max.z ); */

	/* If we're not in a begin/end, or if we're splitting them, go ahead and 
	 * do the flushing. */
	
	if ( tilesort_spu.splitBeginEnd ||
			 !(ctx->current.inBeginEnd || ctx->lists.currentIndex) )
	{
		doFlush( ctx, GL_FALSE, GL_FALSE, NULL );
		return;
	}

	/* Otherwise, we need to grow the buffer to make room for more stuff. 
	 * Since we don't want to do this forever, we mark the context as 
	 * "Flush on end", which will force a flush the next time glEnd() 
	 * is called. 
	 * 
	 * Note that this can only happen to the geometry buffer, so we 
	 * assert that.
	 */

	CRASSERT( ctx == thread->currentContext->State );

	ctx->current.flushOnEnd = 1;

	/* release the geometry buffer to sync it up */
	CRASSERT(thread->packer->buffer.pack == thread->geometry_buffer.pack);
	crPackReleaseBuffer( thread->packer );

	/* Save copy of geoemtry_buffer information.  We'll copy this into the
	 * new, bigger buffer below.
	 */
	old_geometry_buffer = thread->geometry_buffer; /* struct copy */ 


	newSize = thread->geometry_buffer.size * 2;
	crDebug( "-=-=-=- Growing the buffer to %d -=-=-=-", newSize );

	crPackInitBuffer( &(thread->geometry_buffer),
										crAlloc( newSize ), newSize,
										newSize - (24+END_FLUFF+8) );

	thread->geometry_buffer.geometry_only = GL_TRUE;

	/* Use the new geometry buffer */
	crPackSetBuffer( thread->packer, &(thread->geometry_buffer) );

	/* Copy the old buffer's contents into the new buffer */
	crPackAppendBuffer(&(old_geometry_buffer));

	/* The location of the geometry buffer has changed, so we need to 
	 * tell the state tracker to update its "current" pointers. 
	 * The "offset" computed here does *not* require that the two 
	 * buffers be contiguous -- in fact they almost certainly won't be.
	 */
	crPackOffsetCurrentPointers( thread->packer->buffer.data_current -
															 old_geometry_buffer.data_current );

	/* Discard the old, small buffer */
	crFree( old_geometry_buffer.pack );
}
