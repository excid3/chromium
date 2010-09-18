/* VNC Reflector
 * Copyright (C) 2001-2003 HorizonLive.com, Inc.  All rights reserved.
 *
 * This software is released under the terms specified in the file LICENSE,
 * included.  HorizonLive provides e-Learning and collaborative synchronous
 * presentation solutions in a totally Web-based environment.  For more
 * information about HorizonLive, please see our website at
 * http://www.horizonlive.com.
 *
 * This software was authored by Constantin Kaplinsky <const@ce.cctpu.edu.ru>
 * and sponsored by HorizonLive.com, Inc.
 *
 * $Id: encode.h,v 1.3 2006/04/07 15:53:18 brianp Exp $
 * Encoding screen rectangles.
 */

#ifndef _REFLIB_ENCODE_H
#define _REFLIB_ENCODE_H

/* Max size of hextile-encoded data per one 16x16 tile */
/* FIXME: Bad name? */
#define HEXTILE_MAX_TILE_DATASIZE  260

/* Macros to place pixel values to a byte-aligned memory location */

#define BUF_PUT_PIXEL8(buf, pixel)  *(buf) = (pixel)

#define BUF_PUT_PIXEL16(buf, pixel)             \
{                                               \
  (buf)[0] = ((CARD8 *)&(pixel))[0];            \
  (buf)[1] = ((CARD8 *)&(pixel))[1];            \
}

#define BUF_PUT_PIXEL32(buf, pixel)             \
{                                               \
  (buf)[0] = ((CARD8 *)&(pixel))[0];            \
  (buf)[1] = ((CARD8 *)&(pixel))[1];            \
  (buf)[2] = ((CARD8 *)&(pixel))[2];            \
  (buf)[3] = ((CARD8 *)&(pixel))[3];            \
}

/* encode.c */

int allocate_enc_cache(void);
int sizeof_enc_cache(void);
void invalidate_enc_cache(FB_RECT *r);
void free_enc_cache(void);

int put_rect_header(CARD8 *buf, FB_RECT *r);
void get_hextile_caching_stats(long *hits, long *misses);

AIO_BLOCK *rfb_encode_raw_block(CL_SLOT *cl, FB_RECT *r);
AIO_BLOCK *rfb_encode_raw24_block(CL_SLOT *cl, FB_RECT *r);
AIO_BLOCK *rfb_encode_copyrect_block(CL_SLOT *cl, FB_RECT *r);
AIO_BLOCK *rfb_encode_hextile_block(CL_SLOT *cl, FB_RECT *r);

/* encode-tight.c */

void rfb_reset_tight_encoder(CL_SLOT *cl);
int rfb_encode_tight(CL_SLOT *cl, FB_RECT *r);

#endif /* _REFLIB_ENCODE_H */
