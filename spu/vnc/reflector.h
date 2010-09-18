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
 * $Id: reflector.h,v 1.4 2006/05/22 19:47:33 brianp Exp $
 * Global include file
 */

#ifndef _REF_REFLECTOR_H
#define _REF_REFLECTOR_H

#define VERSION  "1.2.4"

/* FIXME: Too many header files with too many dependencies */

/* Framebuffer and related metadata */

extern RFB_SCREEN_INFO g_screen_info;
#if 0
extern CARD32 *g_framebuffer;
extern CARD16 g_fb_width, g_fb_height;
#endif

/* actions.c */

extern int set_actions_file(char *file_path);
extern int perform_action(char *action_str);

/* active.c */

extern int write_active_file(void);
extern int remove_active_file(void);
extern int set_active_file(char *file_path);

/* control.c */

extern void set_control_signals(void);

/* fbs_files.c */

extern void fbs_set_prefix(char *fbs_prefix, int join_sessions);
extern void fbs_open_file(CARD16 fb_width, CARD16 fb_height);
extern void fbs_write_data(void *buf, size_t len);
extern void fbs_spool_byte(CARD8 b);
extern void fbs_spool_data(void *buf, size_t len);
extern void fbs_flush_data(void);
extern void fbs_close_file(void);


/* new */
extern CARD32 *GetFrameBuffer(CARD16 *w, CARD16 *h);
extern void vncspuLockFrameBuffer(void);
extern void vncspuUnlockFrameBuffer(void);

#endif /* _REF_REFLECTOR_H */
