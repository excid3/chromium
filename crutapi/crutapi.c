/* Written by Dale Beermann (beermann@cs.virginia.edu) */
/* Modified by Mike Houston (mhouston@graphics.stanford.edu) */

#include "crut_api.h"
#include "cr_environment.h"
#include "cr_error.h"
#include "cr_mem.h"
#include "cr_mothership.h"
#include "cr_string.h"
#include "cr_url.h"

/**
 * \mainpage CrutApi 
 *
 * \section CrutApiIntroduction Introduction
 *
 */
/*CRUTAPI crut_api;*/

void 
CRUT_APIENTRY
crutInitAPI(CRUTAPI *crut_api, const char* mothership) 
{    
    if (mothership)
    {
	crSetenv("CRMOTHERSHIP", mothership);
    }

    crut_api->mothershipConn = crMothershipConnect();
    
    if (!crut_api->mothershipConn)
    {
	crError("Couldn't connect to the mothership -- I have no idea what to do!");
    }
    else
    {
	crDebug("CRUT Connected to mothership");
    }
}

void
CRUT_APIENTRY
crutGetWindowParams( CRUTAPI *crut_api)
{
    char response[8096];

    if (!crut_api->mothershipConn)
        crError("Checking for Window Params but no connection!");

    crMothershipGetCRUTServerParam( crut_api->mothershipConn, response, "window_geometry" );
    crDebug("CRUTserver window geometry is %s", response);

    if (response[0] == '[')
        sscanf( response, "[ %i, %i, %i, %i ]", 
		&crut_api->winX, 
		&crut_api->winY, 
		&crut_api->winWidth, 
		&crut_api->winHeight );
    else if (crStrchr(response, ','))
        sscanf( response, "%i, %i, %i, %i", 
		&crut_api->winX, 
		&crut_api->winY, 
		&crut_api->winWidth, 
		&crut_api->winHeight );
    else
        sscanf( response, "%i %i %i %i", 
		&crut_api->winX, 
		&crut_api->winY, 
		&crut_api->winWidth, 
		&crut_api->winHeight );

    crDebug("CRUTserver window geometry is %s", response);

    crMothershipGetCRUTServerParam( crut_api->mothershipConn, response, "composite_mode" );
    crut_api->compositeAlpha = crStrstr(response, "alpha") ? 1 : 0;
    crut_api->compositeDepth = crStrcmp(response, "depth") ? 1 : 0;
}

void
CRUT_APIENTRY
crutGetMenuXML( CRUTAPI *crut_api )
{
    char response[8096];
    
    if (!crut_api->mothershipConn)
	crError("Checking for Menu XML but no connection!"); 
    
    crMothershipGetParam( crut_api->mothershipConn, "crut_menu_xml", response );
    
    if (crStrlen(response) < MENU_MAX_SIZE)
		crMemcpy(crut_api->menuBuffer, response, crStrlen(response));
    else
		crError("Menu XML is too long for buffer");
}


void 
CRUT_APIENTRY
crutSetWindowID(CRUTAPI *crut_api, int windowID)
{
    char to_send[sizeof(int)*8+1];

    sprintf(to_send, "%d", windowID);
    crMothershipSetParam(crut_api->mothershipConn, "crut_drawable", (const char*)to_send);
}

void 
CRUT_APIENTRY
crutConnectToClients( CRUTAPI *crut_api )
{   
    int i, ind;     
    char response[8096], hostname[4096], protocol[4096];
    char **newclients;
    char* client;
    unsigned short port;

    crMothershipGetCRUTClients(crut_api->mothershipConn, response);

    newclients = crStrSplit(response, " ");
    crut_api->numclients = crStrToInt(newclients[0]);
    ind = 1;

    crut_api->crutclients = crAlloc(crut_api->numclients*sizeof(CRUTClientPointer));

    for (i=0; i<crut_api->numclients; i++) {
      
        client = newclients[ind++];
	
	if ( !crParseURL( client, protocol, hostname, &port, DEFAULT_CRUT_PORT ) )
	{
	    crError( "Malformed URL: \"%s\"", response );
	}   
	
	crut_api->crutclients[i].mtu = crMothershipGetMTU( crut_api->mothershipConn );
	
	crut_api->crutclients[i].send_conn = crNetAcceptClient( protocol, hostname, port, crut_api->crutclients[i].mtu, 0 );
	
	if (!crut_api->crutclients[i].send_conn)
	{
	    crError("Couldn't connect to the CRUT client");
	}
		    
    }

}

void crutSendEvent( CRUTAPI *crut_api, void *msg, int size )
{
    int i;
    
    for (i=0; i<crut_api->numclients; ++i)
    {
        crNetSend( crut_api->crutclients[i].send_conn, NULL, msg, size );
    }
}

void 
CRUT_APIENTRY
crutSendMouseEvent( CRUTAPI *crut_api, int button, int state, int x, int y ) 
{
    CRUTMouseMsg *msg = crAlloc(sizeof(CRUTMouseMsg));
    msg->header.type = CR_MESSAGE_CRUT;
    msg->msg_type = CRUT_MOUSE_EVENT;
    msg->button = button;
    msg->state = state;
    msg->x = x;
    msg->y = y;
    
    crutSendEvent( crut_api, msg, sizeof(CRUTMouseMsg) );

    crFree(msg);
}

void 
CRUT_APIENTRY
crutSendKeyboardEvent( CRUTAPI *crut_api, int key, int x, int y ) 
{
    CRUTKeyboardMsg *msg = crAlloc(sizeof(CRUTKeyboardMsg));
    msg->header.type = CR_MESSAGE_CRUT;
    msg->msg_type = CRUT_KEYBOARD_EVENT;
    msg->key = key;
    msg->x = x;
    msg->y = y;

    crutSendEvent( crut_api, msg, sizeof(CRUTKeyboardMsg) );

    crFree(msg);
}

void 
CRUT_APIENTRY
crutSendReshapeEvent( CRUTAPI *crut_api, int width, int height )
{
    CRUTReshapeMsg *msg = crAlloc(sizeof(CRUTReshapeMsg));
    msg->header.type = CR_MESSAGE_CRUT;
    msg->msg_type = CRUT_RESHAPE_EVENT;
    msg->width = width;
    msg->height = height;

    crutSendEvent( crut_api, msg, sizeof(CRUTReshapeMsg) );

    crFree(msg);
}

void 
CRUT_APIENTRY
crutSendMotionEvent( CRUTAPI *crut_api, int x, int y )
{
    CRUTMotionMsg *msg = crAlloc(sizeof(CRUTMotionMsg));
    msg->header.type = CR_MESSAGE_CRUT;
    msg->msg_type = CRUT_MOTION_EVENT;
    msg->x = x;
    msg->y = y;

    crutSendEvent( crut_api, msg, sizeof(CRUTMotionMsg) );

    crFree(msg);
}

void 
CRUT_APIENTRY
crutSendPassiveMotionEvent( CRUTAPI *crut_api, int x, int y )
{
    CRUTPassiveMotionMsg *msg = crAlloc(sizeof(CRUTPassiveMotionMsg));
    msg->header.type = CR_MESSAGE_CRUT;
    msg->msg_type = CRUT_PASSIVE_MOTION_EVENT;
    msg->x = x;
    msg->y = y;

    crutSendEvent( crut_api, msg, sizeof(CRUTPassiveMotionMsg) );

    crFree(msg);
}

void
CRUT_APIENTRY
crutSendMenuEvent( CRUTAPI *crut_api, int menuID, int value )
{
    CRUTMenuMsg *msg = crAlloc(sizeof(CRUTMenuMsg));
    msg->header.type = CR_MESSAGE_CRUT;
    msg->msg_type = CRUT_MENU_EVENT;
    msg->menuID = menuID;
    msg->value = value;

    crutSendEvent( crut_api, msg, sizeof(CRUTMenuMsg) );

    crFree(msg);
}


void
CRUT_APIENTRY
crutSendVisibilityEvent( CRUTAPI *crut_api, int state )
{
    CRUTVisibilityMsg *msg = crAlloc(sizeof(CRUTVisibilityMsg));
    msg->header.type = CR_MESSAGE_CRUT;
    msg->msg_type = CRUT_VISIBILITY_EVENT;
    msg->state = state;

    crutSendEvent( crut_api, msg, sizeof(CRUTVisibilityMsg) );

    crFree(msg);
}
