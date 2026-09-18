#include "game.h"
#include "lzss.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

static const unsigned char* input;
static size_t remaining;
static int uploads;
static OSErr ReadFixture(short refNum, long* count, Ptr destination)
{
	(void)refNum;
	CHECK(*count >= 0);
	size_t wanted = *count;
	size_t actual = GAME_MIN(wanted, remaining);
	memcpy(destination, input, actual);
	input += actual;
	remaining -= actual;
	*count = actual;
	return actual == wanted ? noErr : eofErr;
}
static void PixelStore(GLenum parameter, GLint value)
{
	CHECK(parameter == GL_UNPACK_ALIGNMENT && value == 1);
}

// Exercise production parsers with an in-memory file and CPU-only object/GL
// adapters. The real byte readers, validation, group stack, and array loaders run.
#define FSRead ReadFixture
#define glPixelStorei PixelStore
#include "../Source/3D/bg3d.c"
#include "../Source/System/LZSS.c"
#undef glPixelStorei
#undef FSRead

void* AllocPtr(long size) { CHECK(size >= 0); void* p = calloc(1, size); CHECK(p); return p; }
void SafeDisposePtr(void* p) { free(p); }
void DoFatalAlert(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

MetaObjectPtr MO_CreateNewObjectOfType(uint32_t type, uintptr_t subtype, void* data)
{
	size_t size = type == MO_TYPE_GROUP ? sizeof(MOGroupObject)
		: type == MO_TYPE_MATERIAL ? sizeof(MOMaterialObject) : sizeof(MOVertexArrayObject);
	MetaObjectHeader* object = AllocPtr(size);
	object->type = type;
	object->subType = subtype;
	object->refCount = 1;
	if (type == MO_TYPE_MATERIAL)
		((MOMaterialObject*)object)->objectData = *(MOMaterialData*)data;
	else if (type == MO_TYPE_GEOMETRY)
	{
		MOVertexArrayData* geometry = &((MOVertexArrayObject*)object)->objectData;
		*geometry = *(MOVertexArrayData*)data;
		for (int i = 0; i < geometry->numMaterials; i++)
			geometry->materials[i]->objectHeader.refCount++;
	}
	else CHECK(type == MO_TYPE_GROUP);
	return object;
}

void MO_AppendToGroup(MOGroupObject* group, MetaObjectPtr child)
{
	CHECK(group->objectData.numObjectsInGroup < MO_MAX_ITEMS_IN_GROUP);
	group->objectData.groupContents[group->objectData.numObjectsInGroup++] = child;
	((MetaObjectHeader*)child)->refCount++;
}

void MO_DisposeObjectReference(MetaObjectPtr pointer)
{
	MetaObjectHeader* object = pointer;
	if (--object->refCount)
		return;
	if (object->type == MO_TYPE_GROUP)
	{
		MOGroupData* group = &((MOGroupObject*)object)->objectData;
		for (int i = 0; i < group->numObjectsInGroup; i++)
			MO_DisposeObjectReference(group->groupContents[i]);
	}
	else if (object->type == MO_TYPE_GEOMETRY)
	{
		MOVertexArrayData* geometry = &((MOVertexArrayObject*)object)->objectData;
		for (int i = 0; i < geometry->numMaterials; i++)
			MO_DisposeObjectReference(geometry->materials[i]);
		free(geometry->points); free(geometry->normals); free(geometry->uvs);
		free(geometry->colorsByte); free(geometry->colorsFloat); free(geometry->triangles);
	}
	else
	{
		MOMaterialData* material = &((MOMaterialObject*)object)->objectData;
		for (unsigned int i = 0; i < material->numMipmaps; i++)
			free(material->texturePixels[i]);
	}
	free(object);
}

void MO_CalcBoundingBox(MetaObjectPtr object, OGLBoundingBox* box) { (void)object; memset(box, 0, sizeof(*box)); }
GLuint OGL_TextureMap_Load(void* pixels, int width, int height, int src, int dst, int type)
{
	(void)src; (void)dst;
	CHECK(pixels && width > 0 && height > 0 && type == GL_UNSIGNED_BYTE);
	return ++uploads;
}

static void Parse(const unsigned char* bytes, size_t size, Boolean skeleton)
{
	input = bytes;
	remaining = size;
	gBG3D_BytesRemaining = size;
	gBG3D_IsSkeleton = skeleton;
	gBG3D_GroupStackIndex = 0;
	gBG3D_CurrentGeometryObj = NULL;
	gBG3D_CurrentMaterialObj = NULL;
	InitBG3DContainer();
	ReadBG3DHeader(0);
	ParseBG3DFile(0);
	CHECK(gBG3D_GroupStackIndex == 0 && !gBG3D_CurrentGeometryObj);
	PreLoadTextureMaterials();
}
static void Dispose(void)
{
	gBG3DContainerList[0] = gBG3D_CurrentContainer;
	DisposeBG3DContainer(0);
}
static void Tag(unsigned char* bytes, size_t* size, uint32_t tag)
{
	for (int shift = 24; shift >= 0; shift -= 8)
		bytes[(*size)++] = tag >> shift;
}

int main(int argc, char** argv)
{
	const unsigned char literals[] = {7, 'a', 'b', 'c'};
	unsigned char decoded[3] = {0};
	input = literals; remaining = sizeof(literals);
	CHECK(LZSS_Decode(0, (Ptr)decoded, sizeof(literals), sizeof(decoded)) == 3);
	CHECK(!memcmp(decoded, "abc", 3));
	input = literals; remaining = sizeof(literals);
	memset(decoded, 0, sizeof(decoded));
	CHECK(LZSS_Decode(0, (Ptr)decoded, sizeof(literals) + 1, sizeof(decoded)) == -1);
	CHECK(decoded[0] == 0 && decoded[1] == 0 && decoded[2] == 0);

	unsigned char groups[1024] = {'B', 'G', '3', 'D'};
	size_t size = sizeof(BG3DHeaderType);
	Tag(groups, &size, BG3D_TAGTYPE_GROUPSTART);
	Tag(groups, &size, BG3D_TAGTYPE_GROUPSTART);
	Tag(groups, &size, BG3D_TAGTYPE_GROUPEND);
	Tag(groups, &size, BG3D_TAGTYPE_GROUPEND);
	for (int i = 0; i < 60; i++)
	{
		Tag(groups, &size, BG3D_TAGTYPE_GROUPSTART);
		Tag(groups, &size, BG3D_TAGTYPE_GROUPEND);
	}
	Tag(groups, &size, BG3D_TAGTYPE_ENDFILE);
	Parse(groups, size, false);
	MOGroupObject* root = gBG3D_CurrentContainer->root;
	CHECK(root->objectData.numObjectsInGroup == 61);
	MOGroupObject* first = (MOGroupObject*)root->objectData.groupContents[0];
	CHECK(first->objectData.numObjectsInGroup == 1);
	Dispose();

	CHECK(argc > 1);
	for (int i = 1; i < argc; i++)
	{
		FILE* file = fopen(argv[i], "rb");
		CHECK(file && fseek(file, 0, SEEK_END) == 0);
		long length = ftell(file);
		CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
		unsigned char* bytes = AllocPtr(length);
		CHECK(fread(bytes, 1, length, file) == (size_t)length);
		CHECK(fclose(file) == 0);
		Parse(bytes, length, strstr(argv[i], "Skeletons") != NULL);
		Dispose();
		free(bytes);
	}
	CHECK(uploads > 0);
	printf("Asset loader tests passed (%d shipped models)\n", argc - 1);
	return 0;
}
