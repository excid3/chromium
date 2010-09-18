/*
 * Display List Test
 * Brian Paul
 * 30 March 2004
 */

#ifndef USE_CHROMIUM
#define USE_CHROMIUM 0
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#if USE_CHROMIUM
#include "chromium.h"
#include <GL/glx.h>
#endif
#include <GL/glut.h>


static GLfloat Xrot = 15, Yrot = 0, Zrot = 0;
static GLboolean Anim = GL_FALSE;
static GLboolean CallLists = GL_FALSE;

#define MAX_OBJECTS 10
static GLuint Objects[MAX_OBJECTS];
static GLuint NumObjects = 0;

#if USE_CHROMIUM
static glChromiumParametervCRProc ChromiumParametervCR = NULL;
#endif


static void
Idle(void)
{
   Yrot = glutGet(GLUT_ELAPSED_TIME) * 0.025;
   glutPostRedisplay();
}


/*
 * Functions to draw some simple objects.
 * XXX NOTE: the objects _must_ be called in order to get the right
 * result since we're doing "interesting" stuff with state changes
 * to test the tilesort SPU's display list state tracking feature.
 */

/* green solid cube */
static void
DrawObject0(void)
{
   static const GLfloat green[4] = {0.0F, 1.0F, 0.0F, 1.0F};
   glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, green);
   glEnable(GL_LIGHTING);
   glutSolidCube(2.0);
   /*glDisable(GL_LIGHTING);*/
}


/* yellow stippled sphere */
static void
DrawObject1(void)
{
   static const GLfloat yellow[4] = {1.0F, 1.0F, 0.0F, 1.0F};
   glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, yellow);
   /*glEnable(GL_LIGHTING);*/
   glEnable(GL_POLYGON_STIPPLE);
   glutSolidSphere(0.95, 20, 40);
   glDisable(GL_POLYGON_STIPPLE);
   glDisable(GL_LIGHTING);
}


/* red wire cube */
static void
DrawObject2(void)
{
   glColor3f(1.0f, 0.2f, 0.2f);
   glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
   glLineWidth(4);
   glutSolidCube(2.0);
   /* Do this outside of a list, below:
   glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
   */
}


/* purple blended sphere */
static void
DrawObject3(void)
{
   static const GLfloat purple[4] = {1.0F, 0.0F, 1.0F, 0.5F};
   glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, purple);
   glColor3f(1.0F, 0.2F, 0.2F);
   glEnable(GL_BLEND);
   glEnable(GL_CULL_FACE);
   glEnable(GL_LIGHTING);
   glutSolidSphere(0.9, 20, 40);
   glDisable(GL_BLEND);
   glDisable(GL_CULL_FACE);
   glDisable(GL_LIGHTING);
}


static void
Draw(void)
{
   static GLfloat radius = 5.0F;
   GLuint i;

   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   glPushMatrix();
   glRotatef(Xrot, 1.0F, 0.0F, 0.0F);
   glRotatef(Yrot, 0.0F, 1.0F, 0.0F);

   for (i = 0; i < NumObjects; i++) {
      GLfloat phi = i * 360 / NumObjects;
      glPushMatrix();
      glRotatef(phi, 0, 1, 0);
      glTranslatef(0, 0, radius);
      glRotatef(Zrot, 1, 0, 0);
      if (CallLists) {
         glCallLists(1, GL_UNSIGNED_INT, Objects + i);
      }
      else {
         glCallList(Objects[i]);
      }
      if (i == 2)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      glPopMatrix();
     
   }

   glPopMatrix();

   glutSwapBuffers();
}


static void
Reshape(int width, int height)
{
   GLfloat aspect = (float) width / (float) height;
   glViewport(0, 0, width, height);
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum(-aspect, aspect, -1.0, 1.0, 5.0, 50.0);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.0, 0.0, -25.0);
}


static void
Key(unsigned char key, int x, int y)
{
   const GLfloat step = 3.0;
   (void) x;
   (void) y;
   switch (key) {
      case 'a':
         Anim = !Anim;
         if (Anim)
            glutIdleFunc(Idle);
         else
            glutIdleFunc(NULL);
         break;
      case 'c':
         CallLists = !CallLists;
         if (CallLists)
            printf("Using glCallLists\n");
         else
            printf("Using glCallList\n");
         break;
      case 'z':
         Zrot -= step;
         break;
      case 'Z':
         Zrot += step;
         break;
      case 27:
         exit(0);
         break;
   }
   glutPostRedisplay();
}


static void
SpecialKey(int key, int x, int y)
{
   const GLfloat step = 3.0;
   (void) x;
   (void) y;
   switch (key) {
      case GLUT_KEY_UP:
         Xrot -= step;
         break;
      case GLUT_KEY_DOWN:
         Xrot += step;
         break;
      case GLUT_KEY_LEFT:
         Yrot -= step;
         break;
      case GLUT_KEY_RIGHT:
         Yrot += step;
         break;
   }
   glutPostRedisplay();
}


static void
MakeObjects(void)
{
   static const GLubyte stipple[4 * 32] = {
      0xff, 0xff, 0x00, 0x00,      0xff, 0xff, 0x00, 0x00,
      0xff, 0xff, 0x00, 0x00,      0xff, 0xff, 0x00, 0x00,
      0xff, 0xff, 0x00, 0x00,      0xff, 0xff, 0x00, 0x00,
      0xff, 0xff, 0x00, 0x00,      0xff, 0xff, 0x00, 0x00,

      0xff, 0xff, 0x00, 0x00,      0xff, 0xff, 0x00, 0x00,
      0xff, 0xff, 0x00, 0x00,      0xff, 0xff, 0x00, 0x00,
      0xff, 0xff, 0x00, 0x00,      0xff, 0xff, 0x00, 0x00,
      0xff, 0xff, 0x00, 0x00,      0xff, 0xff, 0x00, 0x00,

      0x00, 0x00, 0xff, 0xff,      0x00, 0x00, 0xff, 0xff,
      0x00, 0x00, 0xff, 0xff,      0x00, 0x00, 0xff, 0xff,
      0x00, 0x00, 0xff, 0xff,      0x00, 0x00, 0xff, 0xff,
      0x00, 0x00, 0xff, 0xff,      0x00, 0x00, 0xff, 0xff,

      0x00, 0x00, 0xff, 0xff,      0x00, 0x00, 0xff, 0xff,
      0x00, 0x00, 0xff, 0xff,      0x00, 0x00, 0xff, 0xff,
      0x00, 0x00, 0xff, 0xff,      0x00, 0x00, 0xff, 0xff,
      0x00, 0x00, 0xff, 0xff,      0x00, 0x00, 0xff, 0xff
   };

   glPolygonStipple(stipple);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   Objects[0] = glGenLists(1);
   glNewList(Objects[0], GL_COMPILE);
   DrawObject0();
   glEndList();

   Objects[1] = glGenLists(1);
   glNewList(Objects[1], GL_COMPILE);
   DrawObject1();
   glEndList();

   Objects[2] = glGenLists(1);
   glNewList(Objects[2], GL_COMPILE);
   DrawObject2();
   glEndList();

#if USE_CHROMIUM
   {
      static GLfloat bbox[6] = { -1000.0F, -1000.0F, -1000.0F, 1000.0F, 1000.0F, 1000 .0F};
      ChromiumParametervCR(GL_OBJECT_BBOX_CR, GL_FLOAT, 6, bbox);
   }
#endif
   Objects[3] = glGenLists(1);
   glNewList(Objects[3], GL_COMPILE);
   DrawObject3();
   glEndList();
#if USE_CHROMIUM
   ChromiumParametervCR(GL_DEFAULT_BBOX_CR, GL_FLOAT, 0, NULL);
#endif

   NumObjects = 4;
}


static void
Init(void)
{
#if USE_CHROMIUM
   ChromiumParametervCR = (glChromiumParametervCRProc) glXGetProcAddress("glChromiumParametervCR");
   assert(ChromiumParametervCR);
#endif
   /* setup lighting, etc */
   glClearColor(0.1F, 0.5F, 1.0F, 0.0F);
   glEnable(GL_DEPTH_TEST);
   glEnable(GL_LIGHT0);
   MakeObjects();
}


int
main(int argc, char *argv[])
{
   glutInit(&argc, argv);
   glutInitWindowPosition(500, 0);
   glutInitWindowSize(600, 400);
   glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
   glutCreateWindow(argv[0]);
   glutReshapeFunc(Reshape);
   glutKeyboardFunc(Key);
   glutSpecialFunc(SpecialKey);
   glutDisplayFunc(Draw);
   if (Anim)
      glutIdleFunc(Idle);
   Init();
   glutMainLoop();
   return 0;
}
