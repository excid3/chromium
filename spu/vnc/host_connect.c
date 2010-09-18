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
 * $Id: host_connect.c,v 1.3 2005/08/30 18:13:33 brianp Exp $
 * Connecting to a VNC host
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <zlib.h>

#include "rfblib.h"
#include "reflector.h"
#include "logging.h"
#include "async_io.h"
#include "host_io.h"
#include "translate.h"
#include "client_io.h"
#include "encode.h"
#include "host_connect.h"

static int parse_host_info(void);
static void host_init_hook(void);
static void host_listen_init_hook(void);
static void host_accept_hook(void);
static void rf_host_ver(void);
static void rf_host_auth(void);
static void rf_host_conn_failed(void);
static void rf_host_crypt(void);
static void rf_host_auth_done(void);
static void send_client_initmsg(void);
static void rf_host_initmsg(void);
static void rf_host_set_formats(void);


static int s_request_tight;
static int s_tight_level;

static char *s_host_info_file;
static int s_cl_listen_port;

static char s_hostname[256];
static int s_host_port;
static unsigned char s_host_password[9];


/*
 * Set preferred encoding (Hextile or Tight) and compression level
 * for the Tight encoding.
 */

void set_host_encodings(int request_tight, int tight_level)
{
  s_request_tight = request_tight;
  s_tight_level = tight_level;
}

/*
 * Connect to a remote RFB host
 */
#ifndef CHROMIUM
int connect_to_host(char *host_info_file, int cl_listen_port)
{
  int host_fd;
  struct hostent *phe;
  struct sockaddr_in host_addr;

  /* Save arguments in static variables, for later use */
  if (host_info_file != NULL)
    s_host_info_file = host_info_file;
  if (cl_listen_port != 0)
    s_cl_listen_port = cl_listen_port;

  /* Extract hostname, port and password from host info file */
  if (!parse_host_info())
    return 0;

  if (strcmp(s_hostname, "*") != 0) {

    /* Forward reflector -> host connection */
    log_write(LL_MSG, "Connecting to %s, port %d", s_hostname, s_host_port);

    phe = gethostbyname(s_hostname);
    if (phe == NULL) {
      log_write(LL_ERROR, "Could not get host address: %s", strerror(errno));
      return 0;
    }

    host_addr.sin_family = AF_INET;
    memcpy(&host_addr.sin_addr.s_addr, phe->h_addr, phe->h_length);
    host_addr.sin_port = htons((unsigned short)s_host_port);

    host_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (host_fd == -1) {
      log_write(LL_ERROR, "Could not create socket: %s", strerror(errno));
      return 0;
    }

    if (connect(host_fd, (struct sockaddr *)&host_addr,
                sizeof(host_addr)) != 0) {
      log_write(LL_ERROR, "Could not connect: %s", strerror(errno));
      close(host_fd);
      return 0;
    }

    log_write(LL_MSG, "Connection established");

    aio_add_slot(host_fd, NULL, host_init_hook, sizeof(HOST_SLOT));

  } else {

    /* Reversed host -> reflector connection, start listening */
    if (!aio_listen(s_host_port, host_listen_init_hook, host_accept_hook,
                   sizeof(HOST_SLOT))) {
      log_write(LL_ERROR, "Error creating listening socket: %s",
                strerror(errno));
      return 0;
    }
    log_write(LL_MSG, "Listening for host connections on port %d",
              s_host_port);

  }

  return 1;
}

#else

/* Brian's new version */
int wait_for_client(int port)
{
  (void) parse_host_info;
  (void) host_listen_init_hook;
  (void) host_accept_hook;

  if (port)
    s_cl_listen_port = port;

  if (!aio_listen(s_cl_listen_port, NULL, af_client_accept,
                  sizeof(CL_SLOT))) {
     log_write(LL_ERROR, "Listen for clients failed!");
     return 0;
  }
  log_write(LL_MSG, "Listening for host connections on port %d",
            s_cl_listen_port);
  return 1;
}

#endif


static int parse_host_info(void)
{
  FILE *fp;
  char buf[256];
  char *pos, *colon_pos, *space_pos;
  int len;

  fp = fopen(s_host_info_file, "r");
  if (fp == NULL) {
    log_write(LL_ERROR, "Cannot open host info file: %s", s_host_info_file);
    return 0;
  }

  /* Read the file into a buffer first */
  len = fread(buf, 1, 255, fp);
  buf[len] = '\0';
  fclose(fp);

  if (len == 0) {
    log_write(LL_ERROR, "Error reading host info file (is it empty?)");
    return 0;
  }

  /* Truncate at the end of first line, respecting MS-DOS end-of-lines */
  pos = strchr(buf, '\n');
  if (pos != NULL)
    *pos = '\0';
  pos = strchr(buf, '\r');
  if (pos != NULL)
    *pos = '\0';

  /* FIXME: parsing code below is primitive */

  space_pos = strchr(buf, ' ');
  if (space_pos != NULL) {
    strncpy((char *)s_host_password, space_pos + 1, 8);
    s_host_password[8] = '\0';
    *space_pos = '\0';
  } else {
    log_write(LL_WARN, "Host password not specified, assuming no auth");
    s_host_password[0] = '\0';
  }

  colon_pos = strchr(buf, ':');
  if (colon_pos != NULL) {
    if (!isdigit(colon_pos[1]) && colon_pos[1] != '-') {
      log_write(LL_ERROR, "Non-numeric host display number: %s",
                colon_pos + 1);
      return 0;
    }
    s_host_port = atoi(colon_pos + 1);
    *colon_pos = '\0';
  } else {
    log_write(LL_WARN, "Host display not specified, assuming display :0");
    s_host_port = 0;
  }

  strcpy(s_hostname, buf);
  if (strcmp(s_hostname, "*") != 0) {
    s_host_port += 5900;
  } else {
    if (s_host_port == 0)
      s_host_port = 5500;
  }

  return 1;
}

static void host_init_hook(void)
{
  cur_slot->type = TYPE_HOST_CONNECTING_SLOT;
  aio_setclose(host_close_hook);
  aio_setread(rf_host_ver, NULL, 12);
}

static void host_listen_init_hook(void)
{
  cur_slot->type = TYPE_HOST_LISTENING_SLOT;
}

static void host_accept_hook(void)
{
  log_write(LL_MSG, "Accepted host connection from %s", cur_slot->name);

  host_init_hook();
}

static void rf_host_ver(void)
{
  char *buf = (char *)cur_slot->readbuf;
  int major = 3, minor = 3;
  int remote_major, remote_minor;
  char ver_msg[12];

  if ( strncmp(buf, "RFB ", 4) != 0 || !isdigit(buf[4]) ||
       !isdigit(buf[4]) || !isdigit(buf[5]) || !isdigit(buf[6]) ||
       buf[7] != '.' || !isdigit(buf[8]) || !isdigit(buf[9]) ||
       !isdigit(buf[10]) || buf[11] != '\n' ) {
    log_write(LL_ERROR, "Wrong greeting data received from host");
    aio_close(0);
    return;
  }

  remote_major = atoi(&buf[4]);
  remote_minor = atoi(&buf[8]);
  log_write(LL_INFO, "Remote RFB Protocol version is %d.%d",
            remote_major, remote_minor);

  if (remote_major != major) {
    log_write(LL_ERROR, "Wrong protocol version, expected %d.x", major);
    aio_close(0);
    return;
  } else if (remote_minor != minor) {
    log_write(LL_WARN, "Protocol sub-version does not match (ignoring)");
  }

  sprintf(ver_msg, "RFB %03d.%03d\n", abs(major) % 999, abs(minor) % 999);
  aio_write(NULL, ver_msg, 12);
  aio_setread(rf_host_auth, NULL, 4);
}

static void rf_host_auth(void)
{
  HOST_SLOT *hs = (HOST_SLOT *)cur_slot;
  CARD32 value32;

  value32 = buf_get_CARD32(cur_slot->readbuf);

  switch (value32) {
  case 0:
    hs->temp_len = buf_get_CARD32(&cur_slot->readbuf[4]);
    aio_setread(rf_host_conn_failed, NULL, hs->temp_len);
    break;
  case 1:
    log_write(LL_MSG, "No authentication required at host side");
    send_client_initmsg();
    break;
  case 2:
    log_write(LL_DETAIL, "VNC authentication requested by host");
    aio_setread(rf_host_crypt, NULL, 16);
    break;
  default:
    log_write(LL_ERROR, "Unknown authentication scheme requested by host");
    aio_close(0);
    return;
  }
}

static void rf_host_conn_failed(void)
{
  HOST_SLOT *hs = (HOST_SLOT *)cur_slot;

  log_write(LL_ERROR, "VNC connection failed: %.*s",
            (int)hs->temp_len, cur_slot->readbuf);
  aio_close(0);
}

static void rf_host_crypt(void)
{
  unsigned char response[16];

  log_write(LL_DEBUG, "Received random challenge");

  rfb_crypt(response, cur_slot->readbuf, s_host_password);
  log_write(LL_DEBUG, "Sending DES-encrypted response");
  aio_write(NULL, response, 16);

  aio_setread(rf_host_auth_done, NULL, 4);
}

static void rf_host_auth_done(void)
{
  CARD32 value32;

  value32 = buf_get_CARD32(cur_slot->readbuf);

  switch(value32) {
  case 0:
    log_write(LL_MSG, "Authentication successful");
    send_client_initmsg();
    break;
  case 1:
    log_write(LL_ERROR, "Authentication failed");
    aio_close(0);
    break;
  case 2:
    log_write(LL_ERROR, "Authentication failed, too many tries");
    aio_close(0);
    break;
  default:
    log_write(LL_ERROR, "Unknown authentication result");
    aio_close(0);
  }
}

static void send_client_initmsg(void)
{
  CARD8 shared_session = 1;

  log_write(LL_DETAIL, "Requesting %s session",
            (shared_session != 0) ? "non-shared" : "shared");
  aio_write(NULL, &shared_session, 1);

  aio_setread(rf_host_initmsg, NULL, 24);
}

static void rf_host_initmsg(void)
{
  HOST_SLOT *hs = (HOST_SLOT *)cur_slot;

  hs->fb_width = buf_get_CARD16(cur_slot->readbuf);
  hs->fb_height = buf_get_CARD16(&cur_slot->readbuf[2]);
  log_write(LL_MSG, "Remote desktop geometry is %dx%d",
            (int)hs->fb_width, (int)hs->fb_height);

  hs->temp_len = buf_get_CARD32(&cur_slot->readbuf[20]);
  aio_setread(rf_host_set_formats, NULL, hs->temp_len);
}

static void rf_host_set_formats(void)
{
  HOST_SLOT *hs = (HOST_SLOT *)cur_slot;
  CARD8 *new_name;
  unsigned char setpixfmt_msg[4 + SZ_RFB_PIXEL_FORMAT];
  unsigned char setenc_msg[32] = {
    2,                          /* Message id */
    0,                          /* Padding -- not used */
    0, 0                        /* Number of encodings */
  };
  int setenc_msg_size;

#ifdef CHROMIUM
  CARD32 *g_framebuffer;
  CARD16 g_fb_width, g_fb_height;
  g_framebuffer = GetFrameBuffer(&g_fb_width, &g_fb_height);
#endif

  /* FIXME: Don't change g_screen_info while there is an active host
     connection! */

  log_write(LL_INFO, "Remote desktop name: %.*s",
            (int)hs->temp_len, cur_slot->readbuf);

  new_name = malloc((size_t)hs->temp_len + 1);
  if (new_name != NULL) {
    if (g_screen_info.name != NULL)
      free(g_screen_info.name);

    g_screen_info.name = new_name;
    g_screen_info.name_length = hs->temp_len;
    memcpy(g_screen_info.name, cur_slot->readbuf, hs->temp_len);
    g_screen_info.name[hs->temp_len] = '\0';
  }

  log_write(LL_DETAIL, "Setting up pixel format");

  memset(setpixfmt_msg, 0, 4);
  buf_put_pixfmt(&setpixfmt_msg[4], &g_screen_info.pixformat);
  log_write(LL_DEBUG, "Sending SetPixelFormat message");
  aio_write(NULL, setpixfmt_msg, sizeof(setpixfmt_msg));

  if (s_request_tight) {
    log_write(LL_DETAIL, "Requesting Tight encoding");
    buf_put_CARD32(&setenc_msg[4],  RFB_ENCODING_TIGHT);
    buf_put_CARD32(&setenc_msg[8],  RFB_ENCODING_HEXTILE);
    buf_put_CARD32(&setenc_msg[12], RFB_ENCODING_COPYRECT);
    buf_put_CARD32(&setenc_msg[16], RFB_ENCODING_RAW);
    buf_put_CARD32(&setenc_msg[20], RFB_ENCODING_LASTRECT);
    buf_put_CARD32(&setenc_msg[24], RFB_ENCODING_NEWFBSIZE);
    if (s_tight_level >= 0 && s_tight_level <= 9) {
      log_write(LL_DETAIL, "Requesting compression level %d", s_tight_level);
      buf_put_CARD32(&setenc_msg[28], (RFB_ENCODING_COMPESSLEVEL0 +
                                       (CARD32)s_tight_level));
      buf_put_CARD16(&setenc_msg[2], 7);
      setenc_msg_size = 32;
    } else {
      buf_put_CARD16(&setenc_msg[2], 6);
      setenc_msg_size = 28;
    }
  } else {
    log_write(LL_DETAIL, "Requesting Hextile encoding");
    buf_put_CARD32(&setenc_msg[4],  RFB_ENCODING_HEXTILE);
    buf_put_CARD32(&setenc_msg[8],  RFB_ENCODING_COPYRECT);
    buf_put_CARD32(&setenc_msg[12], RFB_ENCODING_RAW);
    buf_put_CARD32(&setenc_msg[16], RFB_ENCODING_NEWFBSIZE);
    buf_put_CARD16(&setenc_msg[2], 4);
    setenc_msg_size = 20;
  }

  log_write(LL_DEBUG, "Sending SetEncodings message");
  aio_write(NULL, setenc_msg, setenc_msg_size);

#ifndef CHROMIUM
  /* If there was no local framebuffer yet, start listening for client
     connections, assuming we are mostly ready to serve clients. */
  if (g_framebuffer == NULL) {
    if (!aio_listen(s_cl_listen_port, NULL, af_client_accept,
                    sizeof(CL_SLOT))) {
      log_write(LL_ERROR, "Error creating listening socket: %s",
                strerror(errno));
      aio_close(1);
      return;
    }
  }
#endif

  host_activate();
}

/*
 * Allocate the framebuffer, or extend its size if it exists already.
 */

int alloc_framebuffer(int w, int h)
{
#ifdef CHROMIUM
  /* we should never get here */
  abort();
#else

  int fb_size;

  if (g_framebuffer == NULL) {

    /* Just set initial dimentions of the framebuffer. */
    g_fb_width = w;
    g_fb_height = h;

  } else {

    /* Nothing to do if neither width nor height was increased. */
    if (w <= g_fb_width && h <= g_fb_height) {
      log_write(LL_DETAIL, "No need to reallocate framebuffer and cache");
      return 1;
    }

    /* If framebuffer exists, free it's memory first. */
    /* FIXME: Maybe preserve framebuffer contents? */
    log_write(LL_DETAIL, "Freeing the framebuffer");
    free(g_framebuffer);

    /* Framebuffer dimentions may not be decreased. */
    g_fb_width = (w > g_fb_width) ? w : g_fb_width;
    g_fb_height = (h > g_fb_height) ? h : g_fb_height;

  }

  fb_size = (int)g_fb_width * (int)g_fb_height;
  g_framebuffer = malloc(fb_size * sizeof(CARD32));
  if (g_framebuffer == NULL) {
    log_write(LL_ERROR, "Error allocating framebuffer");
    return 0;
  }
  log_write(LL_DETAIL, "(Re)allocated framebuffer, %d bytes",
            fb_size * sizeof(CARD32));

  /* Note: If the cache is already allocated, allocate_enc_cache()
     function frees the cache memory first. */
  if (!allocate_enc_cache()) {
    free(g_framebuffer);
    g_framebuffer = NULL;
    log_write(LL_ERROR, "Error allocating cache for encoded data");
    return 0;
  }
  log_write(LL_DETAIL, "(Re)allocated cache for encoded data, %d bytes",
            sizeof_enc_cache());

#endif
  return 1;
}
