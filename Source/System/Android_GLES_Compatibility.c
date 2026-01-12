#include "game.h"

#if defined(__ANDROID__)

#include <GLES/gl.h>
#include <GLES/glext.h>

// Minimal Immediate Mode Emulation for GLES 1.x

#define MAX_VERTICES 4096

static GLfloat gVertices[MAX_VERTICES * 3];
static GLfloat gTexCoords[MAX_VERTICES * 2];
static GLfloat gColors[MAX_VERTICES * 4];
static GLfloat gNormals[MAX_VERTICES * 3];

static int gVertexCount = 0;
static GLenum gCurrentMode = GL_POINTS;

static float gCurrentColor[4] = {1, 1, 1, 1};
static float gCurrentNormal[3] = {0, 0, 1};
static float gCurrentTexCoord[2] = {0, 0};

void Android_glBegin(GLenum mode)
{
	gVertexCount = 0;
	gCurrentMode = mode;
}

void Android_glEnd(void)
{
	if (gVertexCount == 0) return;

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(3, GL_FLOAT, 0, gVertices);
	glColorPointer(4, GL_FLOAT, 0, gColors);
	glNormalPointer(GL_FLOAT, 0, gNormals);
	glTexCoordPointer(2, GL_FLOAT, 0, gTexCoords);

	if (gCurrentMode == GL_QUADS)
	{
		// Convert QUADS to TRIANGLES on the fly?
		// Or use GL_TRIANGLES index buffer?
		// Simple approach: Draw as TRIANGLES if count is multiple of 4.
		// (0,1,2) (0,2,3)
		
		// If we use indices, we need a buffer.
		// Let's implement immediate indexing.
		GLushort indices[MAX_VERTICES / 4 * 6];
		int idx = 0;
		for (int i = 0; i < gVertexCount; i += 4)
		{
			indices[idx++] = i + 0;
			indices[idx++] = i + 1;
			indices[idx++] = i + 2;
			
			indices[idx++] = i + 0;
			indices[idx++] = i + 2;
			indices[idx++] = i + 3;
		}
		glDrawElements(GL_TRIANGLES, idx, GL_UNSIGNED_SHORT, indices);
	}
	else
	{
		glDrawArrays(gCurrentMode, 0, gVertexCount);
	}

	// CRITICAL FIX: Clear all array pointers BEFORE disabling to prevent
	// stale pointer state that causes GL_INVALID_OPERATION (0x502) when
	// MO_DrawGeometry_VertexArray tries to set new pointers.
	glVertexPointer(3, GL_FLOAT, 0, NULL);
	glColorPointer(4, GL_FLOAT, 0, NULL);
	glNormalPointer(GL_FLOAT, 0, NULL);
	glTexCoordPointer(2, GL_FLOAT, 0, NULL);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void Android_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	if (gVertexCount >= MAX_VERTICES) return;

	int i = gVertexCount;
	gVertices[i*3 + 0] = x;
	gVertices[i*3 + 1] = y;
	gVertices[i*3 + 2] = z;

	gColors[i*4 + 0] = gCurrentColor[0];
	gColors[i*4 + 1] = gCurrentColor[1];
	gColors[i*4 + 2] = gCurrentColor[2];
	gColors[i*4 + 3] = gCurrentColor[3];

	gNormals[i*3 + 0] = gCurrentNormal[0];
	gNormals[i*3 + 1] = gCurrentNormal[1];
	gNormals[i*3 + 2] = gCurrentNormal[2];

	gTexCoords[i*2 + 0] = gCurrentTexCoord[0];
	gTexCoords[i*2 + 1] = gCurrentTexCoord[1];

	gVertexCount++;
}

void Android_glTexCoord2f(GLfloat u, GLfloat v)
{
	gCurrentTexCoord[0] = u;
	gCurrentTexCoord[1] = v;
}

void Android_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	gCurrentColor[0] = r;
	gCurrentColor[1] = g;
	gCurrentColor[2] = b;
	gCurrentColor[3] = a;
}

// Emulate glColor3fv if needed? 
// Not seen in errors yet, but possible.

void Android_glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
	gCurrentNormal[0] = nx;
	gCurrentNormal[1] = ny;
	gCurrentNormal[2] = nz;
}

#endif
