#pragma once

#if defined(__ANDROID__)

#include <GLES/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Define GL_QUADS as it's missing in GLES
#ifndef GL_QUADS
#define GL_QUADS 0x0007 
// Note: 0x0007 is GL_QUADS in desktop GL. We use it as a token.
#endif

void Android_glBegin(GLenum mode);
void Android_glEnd(void);
void Android_glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void Android_glTexCoord2f(GLfloat u, GLfloat v);
void Android_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void Android_glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);

// Emulate missing functions
#define glBegin Android_glBegin
#define glEnd Android_glEnd
#define glVertex3f Android_glVertex3f
#define glTexCoord2f Android_glTexCoord2f
#define glColor4f Android_glColor4f
#define glNormal3f Android_glNormal3f

// Double mapping if they are mostly floats
#define glColor4fv(v) Android_glColor4f((v)[0], (v)[1], (v)[2], (v)[3])
#define glColor3f(r,g,b) Android_glColor4f(r, g, b, 1.0f)

// glOrtho is double in Desktop, float in GLES
#define glOrtho glOrthof
#define glFrustum glFrustumf
#define glFogi(pname, param) glFogf(pname, (float)param)

// Stubs for unsupported features in GLES 1.x (or not critical for now)
#define glColorMaterial(face, mode) ((void)0)
#define glPolygonMode(face, mode) ((void)0)

#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif

// Extensions / Formats
#ifndef GL_BGRA
#ifdef GL_BGRA_EXT
#define GL_BGRA GL_BGRA_EXT
#else
#define GL_BGRA 0x80E1
#endif
#endif

#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV 0x8366
#endif

#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8034
#endif

#ifdef __cplusplus
}
#endif

#endif
