/* Copyright (c) 2004, Tungsten Graphics, Inc.
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include "vncspu.h"

#include "cr_mothership.h"
#include "cr_string.h"
#include "cr_environment.h"


extern int opt_write_coalescing;

/**
 * Set default options for SPU
 */
static void setDefaults( void )
{
	vnc_spu.pixel_size = 32;
}


static void set_server_port(VncSPU *vnc_spu, const char *response)
{
	vnc_spu->server_port = crStrToInt(response);
}

static void set_screen_size( VncSPU *vnc_spu, const char *response )
{
	CRASSERT(response[0] == '[');
	sscanf(response, "[ %d, %d ]",
				 &vnc_spu->screen_width,	&vnc_spu->screen_height);
}

static void set_max_update_rate(VncSPU *vnc_spu, const char *response)
{
	vnc_spu->max_update_rate = crStrToInt(response);
}

static void set_use_bounding_boxes(VncSPU *vnc_spu, const char *response)
{
	vnc_spu->use_bounding_boxes = crStrToInt(response);
}

static void set_frame_drop(VncSPU *vnc_spu, const char *response)
{
	vnc_spu->frame_drop = crStrToInt(response);
}

static void set_coalesce_writes(VncSPU *vnc_spu, const char *response)
{
	opt_write_coalescing = crStrToInt(response);
}

static void set_double_buffer(VncSPU *vnc_spu, const char *response)
{
	vnc_spu->double_buffer = crStrToInt(response);
}

#ifdef NETLOGGER
static void set_netlogger_url(VncSPU *vnc_spu, const char *response)
{
	/* Example URLs:
	 *   -                                   // stdout
	 *   &                                   // stderr
	 *   /tmp/vncspu.log                     // local file
	 *   x-netlog://hostname.domain          // netlogd on remote system
	 *   x-netlog://hostname.domain:7234     // " " " on specific port
	 *   ""                                  // empty string -> no output
	 */
	if (response && response[0])
		vnc_spu->netlogger_url = crStrdup(response);
	else
		vnc_spu->netlogger_url = NULL;
}
#endif


/** 
 * SPU options
 * option, type, nr, default, min, max, title, callback
 */
SPUOptions vncSPUOptions[] = {
	{ "server_port", CR_INT, 1, "-1", NULL, NULL,
		"VNC Server Port Number", (SPUOptionCB) set_server_port },
	{ "screen_size", CR_INT, 2, "[1024, 768]", "[1, 1]", NULL,
		"Screen Size", (SPUOptionCB) set_screen_size },
	{ "max_update_rate", CR_INT, 1, "10", "1", NULL,
		"Max client frame rate", (SPUOptionCB) set_max_update_rate },
	{ "use_bounding_boxes", CR_BOOL, 1, "1", NULL, NULL,
		"Use Bounding Boxes", (SPUOptionCB) set_use_bounding_boxes },
	{ "frame_drop", CR_BOOL, 1, "1", NULL, NULL,
		"Allow frame dropping", (SPUOptionCB) set_frame_drop },
	{ "coalesce_writes", CR_BOOL, 1, "0", NULL, NULL,
		"Coalesce Writes", (SPUOptionCB) set_coalesce_writes },
	{ "double_buffer", CR_BOOL, 1, "1", NULL, NULL,
		"Double Buffer", (SPUOptionCB) set_double_buffer },
#ifdef NETLOGGER
	{ "netlogger_url", CR_STRING, 1, NULL, NULL, NULL,
		"NetLogger log URL", (SPUOptionCB) set_netlogger_url },
#endif
	{ NULL, CR_BOOL, 0, NULL, NULL, NULL, NULL, NULL },
};


/**
 * Gather the config info for the SPU
 */
void vncspuGatherConfiguration( void )
{
	CRConnection *conn;

	setDefaults();

	/* Connect to the mothership and identify ourselves. */
	
	conn = crMothershipConnect( );
	if (!conn)
	{
		/* The mothership isn't running.  Some SPU's can recover gracefully, some 
		 * should issue an error here. */
		crSPUSetDefaultParams( &vnc_spu, vncSPUOptions );
		return;
	}
	crMothershipIdentifySPU( conn, vnc_spu.id );

	crSPUGetMothershipParams( conn, &vnc_spu, vncSPUOptions );

	/* We need to use the same display as the Render SPU, from which we're
	 * derived.
	 */
	if (!crMothershipGetSPUParam(conn, vnc_spu.display_string,
															 "display_string"))
	{
		const char *display = crGetenv("DISPLAY");
		if (display)
			crStrncpy(vnc_spu.display_string,
								display, sizeof(vnc_spu.display_string));
		else
			crStrcpy(vnc_spu.display_string, ""); /* empty string */
	}
	CRASSERT(crStrlen(vnc_spu.display_string)
					 < (int) sizeof(vnc_spu.display_string));

	crMothershipDisconnect( conn );
}
