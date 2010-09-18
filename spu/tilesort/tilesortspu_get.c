/* Copyright (c) 2001, Stanford University
 * All rights reserved.
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */
	
#include "tilesortspu.h"
#include "tilesortspu_gen.h"
#include "cr_packfunctions.h"
#include "cr_mem.h"
#include "cr_string.h"


/* OpenGL doesn't have this token, be we want it here */
#ifndef GL_BOOL
#define GL_BOOL 0x1
#endif


/**
 * If the given <pname> is an OpenGL limit query, query all downstream
 * servers for their limits, find the minimum, return it in <results>
 * and return GL_TRUE.
 * If pname is not a limit, return GL_FALSE.
 * \param pname 
 * \param type
 * \param results
 */
static GLboolean
GetLimit(GLenum pname, GLenum type, void *results)
{
	GET_THREAD(thread);
	GLint numValues = 1;
	GLboolean minMax = GL_FALSE;
	GLfloat params[16];
	GLint i, j;

	switch (pname) {
	case GL_MAX_ATTRIB_STACK_DEPTH:
		params[0] = (GLfloat) CR_MAX_ATTRIB_STACK_DEPTH;
		break;
	case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH:
		params[0] = (GLfloat) CR_MAX_ATTRIB_STACK_DEPTH;
		break;
	case GL_MAX_CLIP_PLANES:
		params[0] = (GLfloat) CR_MAX_CLIP_PLANES;
		break;
	case GL_MAX_ELEMENTS_VERTICES:  /* 1.2 */
		params[0] = (GLfloat) CR_MAX_ELEMENTS_VERTICES;
		break;
	case GL_MAX_ELEMENTS_INDICES:   /* 1.2 */
		params[0] = (GLfloat) CR_MAX_ELEMENTS_INDICES;
		break;
	case GL_MAX_EVAL_ORDER:
		params[0] = (GLfloat) CR_MAX_EVAL_ORDER;
		break;
	case GL_MAX_LIGHTS:
		params[0] = (GLfloat) CR_MAX_LIGHTS;
		break;
	case GL_MAX_LIST_NESTING:
		params[0] = (GLfloat) CR_MAX_LIST_NESTING;
		break;
	case GL_MAX_MODELVIEW_STACK_DEPTH:
		params[0] = (GLfloat) CR_MAX_MODELVIEW_STACK_DEPTH;
		break;
	case GL_MAX_NAME_STACK_DEPTH:
		params[0] = (GLfloat) CR_MAX_NAME_STACK_DEPTH;
		break;
	case GL_MAX_PIXEL_MAP_TABLE:
		params[0] = (GLfloat) CR_MAX_PIXEL_MAP_TABLE;
		break;
	case GL_MAX_PROJECTION_STACK_DEPTH:
		params[0] = (GLfloat) CR_MAX_PROJECTION_STACK_DEPTH;
		break;
	case GL_MAX_TEXTURE_SIZE:
		params[0] = (GLfloat) CR_MAX_TEXTURE_SIZE;
		break;
	case GL_MAX_3D_TEXTURE_SIZE:
		params[0] = (GLfloat) CR_MAX_3D_TEXTURE_SIZE;
		break;
	case GL_MAX_TEXTURE_STACK_DEPTH:
		params[0] = (GLfloat) CR_MAX_TEXTURE_STACK_DEPTH;
		break;
	case GL_MAX_VIEWPORT_DIMS:
		params[0] = (GLfloat) CR_MAX_VIEWPORT_DIM;
		params[1] = (GLfloat) CR_MAX_VIEWPORT_DIM;
		numValues = 2;
		break;
	case GL_SUBPIXEL_BITS:
		params[0] = (GLfloat) CR_SUBPIXEL_BITS;
		break;
	case GL_ALIASED_POINT_SIZE_RANGE:
		params[0] = (GLfloat) CR_ALIASED_POINT_SIZE_MIN;
		params[1] = (GLfloat) CR_ALIASED_POINT_SIZE_MAX;
		numValues = 2;
		minMax = GL_TRUE;
		break;
	case GL_SMOOTH_POINT_SIZE_RANGE:
		params[0] = (GLfloat) CR_SMOOTH_POINT_SIZE_MIN;
		params[1] = (GLfloat) CR_SMOOTH_POINT_SIZE_MAX;
		numValues = 2;
		minMax = GL_TRUE;
		break;
	case GL_SMOOTH_POINT_SIZE_GRANULARITY:
		params[0] = (GLfloat) CR_POINT_SIZE_GRANULARITY;
		break;
	case GL_ALIASED_LINE_WIDTH_RANGE:
		params[0] = (GLfloat) CR_ALIASED_LINE_WIDTH_MIN;
		params[1] = (GLfloat) CR_ALIASED_LINE_WIDTH_MAX;
		numValues = 2;
		minMax = GL_TRUE;
		break;
	case GL_SMOOTH_LINE_WIDTH_RANGE:
		params[0] = (GLfloat) CR_SMOOTH_LINE_WIDTH_MIN;
		params[1] = (GLfloat) CR_SMOOTH_LINE_WIDTH_MAX;
		numValues = 2;
		minMax = GL_TRUE;
		break;
	case GL_SMOOTH_LINE_WIDTH_GRANULARITY:
		params[0] = (GLfloat) CR_LINE_WIDTH_GRANULARITY;
		break;
	/* GL_ARB_multitexture */
	case GL_MAX_TEXTURE_UNITS_ARB:
		params[0] = (GLfloat) CR_MAX_TEXTURE_UNITS;
		break;
	/* GL_NV_register_combiners */
	case GL_MAX_GENERAL_COMBINERS_NV:
		params[0] = (GLfloat) CR_MAX_GENERAL_COMBINERS;
		break;
	case GL_MAX_TEXTURE_LOD_BIAS_EXT:
		params[0] = CR_MAX_TEXTURE_LOD_BIAS;
		break;
#if defined(CR_NV_fragment_program) || defined(GL_ARB_fragment_program)
	case GL_MAX_TEXTURE_COORDS_ARB:
		params[0] = (GLfloat) CR_MAX_TEXTURE_COORDS;
		break;
	case GL_MAX_TEXTURE_IMAGE_UNITS_ARB:
		params[0] = (GLfloat) CR_MAX_TEXTURE_IMAGE_UNITS;
		break;
#endif
	default:
		return GL_FALSE; /* not a GL limit */
	}

	CRASSERT(numValues < 16);

	/* release geometry buffer */
	crPackReleaseBuffer( thread->packer );

	/*
	 * loop over servers, issuing the glGet.
	 * We send it via the zero-th thread's server connections.
	 */
	for (i = 0; i < tilesort_spu.num_servers; i++)
	{
		int writeback = 1;
		GLfloat values[16];

		crPackSetBuffer( thread->packer, &(thread->buffer[i]) );

		if (tilesort_spu.swap)
			crPackGetFloatvSWAP( pname, values, &writeback );
		else
			crPackGetFloatv( pname, values, &writeback );

		/* release server buffer */
		crPackReleaseBuffer( thread->packer );

		/* Flush buffer (send to server) */
		tilesortspuSendServerBuffer( i );

		/* Get return value */
		while (writeback) {
			crNetRecv();
		}

		/* update current minimum(s) */
		if (minMax) {
			CRASSERT(numValues == 2);
			/* element 0 is a minimum, element 1 is a maximum */
			if (values[0] > params[0])
				params[0] = values[0];
			if (values[1] < params[1])
				params[1] = values[1];
		}
		else {
			for (j = 0; j < numValues; j++)
				if (values[j] < params[j])
					params[j] = values[j];
		}
	}

	/* Restore the default pack buffer */
	crPackSetBuffer( thread->packer, &(thread->geometry_buffer) );

	/* return the results */
	if (type == GL_BOOL)
	{
		GLboolean *bResult = (GLboolean *) results;
		for (j = 0; j < numValues; j++)
			bResult[j] = (GLboolean) (params[j] ? GL_TRUE : GL_FALSE);
	}
	else if (type == GL_INT)
	{
		GLint *iResult = (GLint *) results;
		for (j = 0; j < numValues; j++)
			iResult[j] = (GLint) params[j];
	}
	else if (type == GL_FLOAT)
	{
		GLfloat *fResult = (GLfloat *) results;
		for (j = 0; j < numValues; j++)
			fResult[j] = (GLfloat) params[j];
	}
	else if (type == GL_DOUBLE)
	{
		GLdouble *dResult = (GLdouble *) results;
		for (j = 0; j < numValues; j++)
			dResult[j] = (GLdouble) params[j];
	}
	return GL_TRUE;
}



void TILESORTSPU_APIENTRY tilesortspu_GetDoublev( GLenum pname, GLdouble *params )
{
	if (!GetLimit(pname, GL_DOUBLE, params))
		crStateGetDoublev( pname, params );

	if (pname == GL_VIEWPORT) {
		/* undo mural scaling */
		GET_THREAD(thread);
		WindowInfo *winInfo = thread->currentContext->currentWindow;
		CRASSERT(winInfo);
		params[0] = (GLdouble) (params[0] / winInfo->widthScale);
		params[1] = (GLdouble) (params[1] / winInfo->heightScale);
		params[2] = (GLdouble) (params[2] / winInfo->widthScale);
		params[3] = (GLdouble) (params[3] / winInfo->heightScale);
	}
}

void TILESORTSPU_APIENTRY tilesortspu_GetFloatv( GLenum pname, GLfloat *params )
{
	if (!GetLimit(pname, GL_FLOAT, params))
		crStateGetFloatv( pname, params );

	if (pname == GL_VIEWPORT) {
		/* undo mural scaling */
		GET_THREAD(thread);
		WindowInfo *winInfo = thread->currentContext->currentWindow;
		CRASSERT(winInfo);
		params[0] = (GLfloat) (params[0] / winInfo->widthScale);
		params[1] = (GLfloat) (params[1] / winInfo->heightScale);
		params[2] = (GLfloat) (params[2] / winInfo->widthScale);
		params[3] = (GLfloat) (params[3] / winInfo->heightScale);
	}
}

void TILESORTSPU_APIENTRY tilesortspu_GetIntegerv( GLenum pname, GLint *params )
{
	if (!GetLimit(pname, GL_INT, params))
		crStateGetIntegerv( pname, params );

	if (pname == GL_VIEWPORT) {
		/* undo mural scaling */
		GET_THREAD(thread);
		WindowInfo *winInfo;
		if (thread->currentContext) {
			winInfo = thread->currentContext->currentWindow;
			CRASSERT(winInfo);
			params[0] = (GLint) (params[0] / winInfo->widthScale);
			params[1] = (GLint) (params[1] / winInfo->heightScale);
			params[2] = (GLint) (params[2] / winInfo->widthScale);
			params[3] = (GLint) (params[3] / winInfo->heightScale);
		}
	}
}

void TILESORTSPU_APIENTRY tilesortspu_GetBooleanv( GLenum pname, GLboolean *params )
{
	if (!GetLimit(pname, GL_BOOL, params))
		crStateGetBooleanv( pname, params );

	if (pname == GL_VIEWPORT) {
		/* undo mural scaling */
		/* Don't need to do it for GetBooleanv */
	}
}


/**
 * Query all downstream servers for their extension strings, merge them,
 * intersect with Chromium's known extensions, and append the Chromium-
 * specific extensions.
 */
const GLubyte *
tilesortspuGetExtensionsString(void)
{
	GET_THREAD(thread);
	const GLubyte **extensions, *ext;
	GLint i;

	extensions = (const GLubyte **) crCalloc(tilesort_spu.num_servers * sizeof(GLubyte *));
	if (!extensions)
	{
		crWarning("Out of memory in tilesortspu::GetExtensionsString");
		return NULL;
	}

	/* release geometry buffer */
	crPackReleaseBuffer( thread->packer );

	/*
	 * loop over servers, issuing the glGet.
	 * We send it via the zero-th thread's server connections.
	 */
	for (i = 0; i < tilesort_spu.num_servers; i++)
	{
		int writeback = 1;

		extensions[i] = crCalloc(50 * 1000);
		CRASSERT(extensions[i]);

		crPackSetBuffer( thread->packer, &(thread->buffer[i]) );

		if (tilesort_spu.swap)
			crPackGetStringSWAP( GL_EXTENSIONS, extensions[i], &writeback );
		else
			crPackGetString( GL_EXTENSIONS, extensions[i], &writeback );

		/* release server buffer */
		crPackReleaseBuffer( thread->packer );

		/* Flush buffer (send to server) */
		tilesortspuSendServerBuffer( i );

		/* Get return value */
		while (writeback) {
			crNetRecv();
		}

		/* make sure we didn't overflow the buffer */
		CRASSERT(crStrlen((char *) extensions[i]) < 50*1000);
	}

	/* Restore the default pack buffer */
	crPackSetBuffer( thread->packer, &(thread->geometry_buffer) );

	ext = crStateMergeExtensions(tilesort_spu.num_servers, extensions);

	for (i = 0; i < tilesort_spu.num_servers; i++)
		crFree((void *) extensions[i]);
	crFree((void *)extensions);

	return ext;
}

/**
 * Query all downstream servers for their version strings, find the
 * minimum.
 */
GLfloat
tilesortspuGetVersionNumber(void)
{
	GET_THREAD(thread);
	GLfloat *versions, minVersion;
	GLint i;

	versions = (GLfloat *) crCalloc(tilesort_spu.num_servers * sizeof(GLfloat));
	if (!versions)
	{
		crWarning("Out of memory in tilesortspu::GetVersionString");
		return 0.0;
	}

	/* release geometry buffer */
	crPackReleaseBuffer( thread->packer );

	/*
	 * loop over servers, issuing the glGet.
	 * We send it via the zero-th thread's server connections.
	 */
	for (i = 0; i < tilesort_spu.num_servers; i++)
	{
		int writeback = 1;
		GLubyte version[1000];

		crPackSetBuffer( thread->packer, &(thread->buffer[i]) );

		if (tilesort_spu.swap)
			crPackGetStringSWAP( GL_VERSION, version, &writeback );
		else
			crPackGetString( GL_VERSION, version, &writeback );

		/* release server buffer */
		crPackReleaseBuffer( thread->packer );

		/* Flush buffer (send to server) */
		tilesortspuSendServerBuffer( i );

		/* Get return value */
		while (writeback) {
			crNetRecv();
		}

		versions[i] = crStrToFloat((char *) version);
		CRASSERT(versions[i] > 0.0);
	}

	/* Restore the default pack buffer */
	crPackSetBuffer( thread->packer, &(thread->geometry_buffer) );

	/* find min */
	minVersion = versions[0];
	for (i = 1; i < tilesort_spu.num_servers; i++) {
		if (versions[i] < minVersion)
			minVersion = versions[i];
	}

	crFree((void *) versions);

	minVersion = crStateComputeVersion(minVersion);

	return minVersion;
}


const GLubyte * TILESORTSPU_APIENTRY tilesortspu_GetString( GLenum pname )
{
	if (pname == GL_EXTENSIONS)
	{
		GET_CONTEXT(ctx);
		if (!ctx->limits.extensions) {
			/* Query all servers for their extensions, return the intersection */
			ctx->limits.extensions = tilesortspuGetExtensionsString();
		}
		return ctx->limits.extensions;
	}
	else if (pname == GL_VERSION) {
		GLfloat version = tilesortspuGetVersionNumber();
		GET_THREAD(thread);
		sprintf(thread->currentContext->glVersion, "%g Chromium %s",
						version, CR_VERSION_STRING);
		return (const GLubyte *) thread->currentContext->glVersion;
	}
	else
	{
		/** XXX \todo for GL_PROGRAM_ERROR_STRING_NV it would be nice to get the
		 * real error string from the servers.
		 */
		return crStateGetString(pname);
	}
}


GLboolean TILESORTSPU_APIENTRY tilesortspu_AreTexturesResident( GLsizei n, const GLuint *textures, GLboolean *residences )
{
	/* lie for now */
	int i;
	(void) textures;
	for (i = 0; i < n; i++) {
		residences[i] = GL_TRUE;
	}
	return GL_TRUE;
}


GLboolean TILESORTSPU_APIENTRY tilesortspu_AreProgramsResidentNV( GLsizei n, const GLuint * ids, GLboolean * residences )
{
	/* lie for now */
	int i;
	(void) ids;
	for (i = 0; i < n; i++) {
		residences[i] = GL_TRUE;
	}
	return GL_TRUE;
}
