#include "game.h"

// Geometry is authoritative: old authoring lists may omit live normals or retain
// slots removed by deduplication. Each bone must transform every normal used by
// its points before Bones.c copies those transformed normals into the mesh.
// The caller allocates normalList for numDecomposedNormals entries.
int BuildBoneNormalList(const SkeletonDefType* skeleton, BoneDefinitionType* bone)
{
	if (skeleton->numDecomposedNormals < 0 || skeleton->numDecomposedNormals > MAX_DECOMPOSED_NORMALS)
		return -1;
	Boolean used[MAX_DECOMPOSED_NORMALS] = {false};
	int count = 0;
	for (int p = 0; p < bone->numPointsAttachedToBone; p++)
	{
		if (bone->pointList[p] >= skeleton->numDecomposedPoints)
			return -1;
		const DecomposedPointType* point = &skeleton->decomposedPointList[bone->pointList[p]];
		if (point->numRefs > MAX_POINT_REFS)
			return -1;
		for (int r = 0; r < point->numRefs; r++)
		{
			int normal = point->whichNormal[r];
			if (normal < 0 || normal >= skeleton->numDecomposedNormals)
				return -1;
			if (!used[normal])
			{
				used[normal] = true;
				bone->normalList[count++] = normal;
			}
		}
	}
	return count;
}
