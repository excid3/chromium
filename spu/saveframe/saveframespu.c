/* Copyright (c) 2001, Stanford University
 * All rights reserved
 *
 * See the file LICENSE.txt for information on redistributing this software.
 */

#include <stdio.h>
#include <assert.h>

#include "cr_error.h"
#include "cr_string.h"
#include "cr_spu.h"
#include "cr_mem.h"
#include "saveframespu.h"

#define MAX_FILENAME_LENGTH 511

SaveFrameSPU saveframe_spu;

static int
getFilename( char *filename, int eye, GLboolean stereoOn, const char *spec, int framenum )
{
    int numchars;
    
    if ( stereoOn == FALSE )
    {
#ifdef WINDOWS									/* ? */
	numchars = _snprintf(filename, MAX_FILENAME_LENGTH,
			     saveframe_spu.spec, saveframe_spu.framenum);
#else
	numchars = snprintf(filename, MAX_FILENAME_LENGTH,
			    saveframe_spu.spec, saveframe_spu.framenum);
#endif
    }
    else
    {
	char eyeChar = (eye == 0 ? 'L' : 'R');
#ifdef WINDOWS									/* ? */
	numchars = _snprintf(filename, MAX_FILENAME_LENGTH,
			     saveframe_spu.spec, saveframe_spu.framenum, eyeChar);
#else
	numchars = snprintf(filename, MAX_FILENAME_LENGTH,
			    saveframe_spu.spec, saveframe_spu.framenum, eyeChar);
#endif	
    }

    if (numchars >= MAX_FILENAME_LENGTH)
    {
	crWarning("saveframespu: Filename longer than %d characters isn't allowed. Skipping frame %d.",
		  MAX_FILENAME_LENGTH, saveframe_spu.framenum);
	return FALSE;
    }

    return TRUE;
}

static void
RGBA_to_PPM(char *filename, int width, int height, const GLubyte * buffer,
	    int binary)
{
	FILE *file;
	int i, j;

	file = fopen(filename, "wb");

	if (file == NULL)
	{
		crWarning("Unable to create file %s.\n", filename);
		return;
	}

	if (binary)
	{
		const GLubyte *row;

		fprintf(file, "P6\n%d %d\n255\n", width, height);

		for (i = height - 1; i >= 0; i--)
		{
			row = &buffer[i * width * 4];
			for (j = 0; j < width; j++)
			{
				fwrite(row, 3, 1, file);
				row += 4;
			}
		}
	}
	else
	{
		fprintf(file, "P3\n%d %d\n255\n", width, height);
		for (i = height - 1; i >= 0; i--)
		{
			for (j = 0; j < width; j++)
			{
				fprintf(file, "%d %d %d \n",
					buffer[i * width * 4 + j * 4 + 0],
					buffer[i * width * 4 + j * 4 + 1],
					buffer[i * width * 4 + j * 4 + 2]);
			}
			fprintf(file, "\n");
		}
	}

	fclose(file);
}

static void
RGB_to_RAW(char *filename, int width, int height, const GLubyte * buffer)
{
	const GLubyte *row;
	FILE *file;
	int i, j;

	file = fopen(filename, "wb");

	if (file == NULL)
	{
		crWarning("Unable to create file %s.\n", filename);
		return;
	}

	for (i = 0; i < height; i++)
	{
	    row = &buffer[i * width * 3];
	    for (j = 0; j < width; j++)
	    {
		fwrite(row, 3, 1, file);
		row += 3;
	    }
	}
	fclose(file);
}


#ifdef JPEG
static void
RGB_to_JPG(char *filename, int width, int height, const GLubyte * buffer)
{
	FILE *file;
	int row_stride;
	JSAMPROW row_pointer[1];

	file = fopen(filename, "wb");

	if (file == NULL)
	{
		crWarning("Unable to create file %s.\n", filename);
		return;
	}

	/* Write image to file */
	jpeg_stdio_dest(&saveframe_spu.cinfo, file);
	saveframe_spu.cinfo.image_width = width;
	saveframe_spu.cinfo.image_height = height;
	saveframe_spu.cinfo.input_components = 3;
	saveframe_spu.cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&saveframe_spu.cinfo);
	saveframe_spu.cinfo.dct_method = JDCT_FLOAT;
	jpeg_set_quality(&saveframe_spu.cinfo, 100, TRUE);

	jpeg_start_compress(&saveframe_spu.cinfo, TRUE);
	row_stride = width * 3;
	while (saveframe_spu.cinfo.next_scanline < saveframe_spu.cinfo.image_height)
	{
		row_pointer[0] = (JSAMPROW) ((buffer + (height - 1) * row_stride)
					     - saveframe_spu.cinfo.next_scanline * row_stride);
		jpeg_write_scanlines(&saveframe_spu.cinfo, row_pointer, 1);
	}
	jpeg_finish_compress(&saveframe_spu.cinfo);

	fclose(file);
}
#endif /* JPEG */


static void SAVEFRAMESPU_APIENTRY
swapBuffers(GLint window, GLint flags)
{
	if (saveframe_spu.enabled)
	{
		int saveThisFrame = 0;

		if ((saveframe_spu.single != -1)
				&& (saveframe_spu.single == saveframe_spu.framenum))
			saveThisFrame = 1;

		if ((saveframe_spu.single == -1)
				&& (saveframe_spu.framenum % saveframe_spu.stride == 0))
			saveThisFrame = 1;

		if (saveframe_spu.width == -1)
		{
			/* We've never been given a size.  Try to get one from OpenGL. */
			GLint geom[4];
			saveframe_spu.child.GetIntegerv(GL_VIEWPORT, geom);
			saveframe_spu.x = geom[0];
			saveframe_spu.y = geom[1];
			saveframe_spu.width = geom[2];
			saveframe_spu.height = geom[3];

			saveframespuResizeBuffer();
		}

		if (saveThisFrame && !(flags & CR_SUPPRESS_SWAP_BIT))
		{
			char filename[MAX_FILENAME_LENGTH + 1];
			int i;
			GLboolean stereoOn = FALSE;
			GLint pixelFormat;
			int nEyes;
			
			if ( !crStrcmp( saveframe_spu.format, "ppm" ) )
			    pixelFormat = GL_RGBA;
			else
			    pixelFormat = GL_RGB;			
			saveframe_spu.child.GetBooleanv( GL_STEREO, &stereoOn );
			
			nEyes = (stereoOn ? 2 : 1);
			for ( i = 0; i < nEyes; i++ )
			{
			    if ( getFilename( filename, i, stereoOn, saveframe_spu.spec, saveframe_spu.framenum ) )
			    {
				GLint buffer = GL_BACK;
				if ( stereoOn )
				    buffer = (i==0 ? GL_BACK_LEFT : GL_BACK_RIGHT);

				saveframe_spu.child.ReadBuffer(buffer);
				saveframe_spu.child.PixelStorei(GL_PACK_ALIGNMENT, 1);
				saveframe_spu.child.ReadPixels(0, 0, saveframe_spu.width,
							       saveframe_spu.height, pixelFormat,
							       GL_UNSIGNED_BYTE,
							       saveframe_spu.buffer);

				if ( !crStrcmp( saveframe_spu.format, "ppm" ) )
				    RGBA_to_PPM(filename, saveframe_spu.width, saveframe_spu.height,
						saveframe_spu.buffer, saveframe_spu.binary);
#ifdef JPEG
				else if (!crStrcmp(saveframe_spu.format, "jpeg"))
				    RGB_to_JPG(filename, saveframe_spu.width, saveframe_spu.height,
					       saveframe_spu.buffer);
#endif
				else if (!crStrcmp( saveframe_spu.format, "raw"))
				    RGB_to_RAW(filename, saveframe_spu.width, saveframe_spu.height,
					       saveframe_spu.buffer);
				else
				    crWarning("Invalid value for saveframe_spu.format: %s",
					              saveframe_spu.format);
			    }
			}
		}
	}

	if (!(flags & CR_SUPPRESS_SWAP_BIT))
		saveframe_spu.framenum++;

	saveframe_spu.child.SwapBuffers(window, flags);
}

static void SAVEFRAMESPU_APIENTRY
viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	int sizeChange= 0;

	if (height>saveframe_spu.height) {
		saveframe_spu.height = height;
		sizeChange= 1;
	}
	if (width>saveframe_spu.width) {
		saveframe_spu.width = width;
		sizeChange= 1;
	}
	saveframe_spu.x = x;
	saveframe_spu.y = y;

	if (sizeChange)
		saveframespuResizeBuffer();
	saveframe_spu.child.Viewport(x, y, width, height);
}

static void SAVEFRAMESPU_APIENTRY
saveframeChromiumParameteri(GLenum param, GLint i)
{
	switch (param)
	{
	case GL_SAVEFRAME_ENABLED_CR:
		saveframe_spu.enabled = i == GL_TRUE ? GL_TRUE : GL_FALSE;
		break;
	case GL_SAVEFRAME_FRAMENUM_CR:
		if (i >= 0)
			saveframe_spu.framenum = i;
		break;
	case GL_SAVEFRAME_STRIDE_CR:
		if (i > 0)
			saveframe_spu.stride = i;
		break;
	case GL_SAVEFRAME_SINGLE_CR:
		i = i > -2 ? i : -1;
		saveframe_spu.single = i;
		break;
	default:
		break;
	}
}

static void SAVEFRAMESPU_APIENTRY
saveframeChromiumParameterv(GLenum param, GLenum type, GLsizei count,
			    const GLvoid * p)
{
	switch (param)
	{
	case GL_SAVEFRAME_FILESPEC_CR:
		if (!p)
			break;
		if (saveframe_spu.spec)
			crFree(saveframe_spu.spec);
		saveframe_spu.spec = crStrdup((const char *) p);
		break;
	default:
		break;
	}
}

static void SAVEFRAMESPU_APIENTRY
saveframeGetChromiumParameterv(GLenum target, GLuint index, GLenum type,
			       GLsizei count, GLvoid * values)
{
	switch (target)
	{
	case GL_SAVEFRAME_FRAMENUM_CR:
		if (type == GL_INT && count == 1)
			*((GLint *) values) = saveframe_spu.framenum;
		return;
	case GL_SAVEFRAME_STRIDE_CR:
		if (type == GL_INT && count == 1)
			*((GLint *) values) = saveframe_spu.stride;
		return;
	case GL_SAVEFRAME_SINGLE_CR:
		if (type == GL_INT && count == 1)
			*((GLint *) values) = saveframe_spu.single;
		return;
	case GL_SAVEFRAME_FILESPEC_CR:
		if (type == GL_BYTE && count > 0)
			crStrncpy((char *) values, saveframe_spu.spec, count);
		return;
	default:
		saveframe_spu.super.GetChromiumParametervCR(target, index, type, count,
																								values);
		return;
	}
}

SPUNamedFunctionTable _cr_saveframe_table[] = {
	{"SwapBuffers", (SPUGenericFunction) swapBuffers},
	{"Viewport", (SPUGenericFunction) viewport},
	{"ChromiumParameteriCR", (SPUGenericFunction) saveframeChromiumParameteri},
	{"ChromiumParametervCR", (SPUGenericFunction) saveframeChromiumParameterv},
	{"GetChromiumParametervCR", (SPUGenericFunction) saveframeGetChromiumParameterv},
	{NULL, NULL}
};

void
saveframespuResizeBuffer(void)
{
	if (saveframe_spu.buffer != NULL)
		crFree(saveframe_spu.buffer);

	saveframe_spu.buffer =
		(GLubyte *) crAlloc(sizeof(GLubyte) * saveframe_spu.height *
											 saveframe_spu.width * 4);
}
