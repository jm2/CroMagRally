#include "game.h"
#include <limits.h>

Boolean BG3D_ValidateGeometryHeader(const BG3DGeometryHeader* header, int materialCount)
{
	if (!header || materialCount < 0 || materialCount > MAX_BG3D_MATERIALS
		|| header->type != BG3D_GEOMETRYTYPE_VERTEXELEMENTS
		|| header->numMaterials < 0 || header->numMaterials > MAX_MATERIAL_LAYERS
		|| header->numMaterials > MAX_MULTITEXTURE_LAYERS
		|| header->numPoints == 0 || header->numPoints > INT_MAX / sizeof(OGLColorRGBA)
		|| header->numTriangles == 0 || header->numTriangles > INT_MAX / sizeof(MOTriangleIndecies))
		return false;
	for (int i = 0; i < header->numMaterials; i++)
		if (header->layerMaterialNum[i] >= (uint32_t) materialCount)
			return false;
	return true;
}

Boolean BG3D_ValidateTextureHeader(const BG3DTextureHeader* header)
{
	if (!header || !header->width || !header->height
		|| header->width > INT_MAX || header->height > INT_MAX)
		return false;
	unsigned int bytesPerPixel;
	switch (header->srcPixelFormat)
	{
		case GL_RGB: case GL_BGR: bytesPerPixel = 3; break;
		case GL_RGBA: case GL_BGRA: bytesPerPixel = 4; break;
		default: return false;
	}
	switch (header->dstPixelFormat)
	{
		case GL_RGB: case GL_RGBA: case GL_RGB8: case GL_RGBA8:
		case GL_RGB5: case GL_RGB5_A1: break;
		default: return false;
	}
	uint64_t bytes = (uint64_t) header->width * header->height * bytesPerPixel;
	return bytes <= INT_MAX && bytes == header->bufferSize;
}
