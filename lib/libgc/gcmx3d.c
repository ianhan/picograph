#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "gc.h"

typedef struct tagGCMATRIXFIXED3D
{
    GCFIXED m11;
    GCFIXED m21;
    GCFIXED m31;
    GCFIXED m41;
    GCFIXED m12;
    GCFIXED m22;
    GCFIXED m32;
    GCFIXED m42;
    GCFIXED m13;
    GCFIXED m23;
    GCFIXED m33;
    GCFIXED m43;
    GCFIXED OffsetX;
    GCFIXED OffsetY;
    GCFIXED OffsetZ;
    GCFIXED W;
} GCMATRIXFIXED3D;

typedef struct tagGCMATRIX3D
{
	union
	{
		GCMATRIXFIXED3D fixed;
		GCFIXED array[4][4];
	};
} GCMATRIX3D;

GCMATRIX3D *GCMxIdentity3D();

void GCMxInitialize3D(
    GCMATRIX3D *pMatrix
    );

void GCMxRotate3D(
    GCMATRIX3D *pMatrix,
    float angle
    );

void GCMxTranslate3D(
    GCMATRIX3D *pMatrix,
    float x,
    float y
    );

void GCMxScale3D(
    GCMATRIX3D *pMatrix,
    float scalex,
    float scaley
    );

void GCMxInverse3D(
	GCMATRIX3D *pSrc,
	GCMATRIX3D *pDst
	);

void GCMxMultiply3D(
    GCMATRIX3D *result,
    GCMATRIX3D *a,
    GCMATRIX3D *b
    );

void GCMxTransformPoint3D(
    GCPOINT *pPoint,
    GCMATRIX3D *pTransform
    );

void GCMxGetPolyBounds3D(
	GC *pGC,
	GCRECT *pBounds,
	GCPOINT *pPoly,
	unsigned long polys,
	GCMATRIX3D *pMatrix,
	GCPOINT *pOrigin
	);

const GCMATRIX3D gcmxIdentity3D =
{
    GCFIX(1), 0, 0, 0,
    0, GCFIX(1), 0, 0,
    0, 0, GCFIX(1), 0,
    0, 0, 0, GCFIX(1)
};

GCMATRIX3D *GCMxIdentity3D()
{
    return &gcmxIdentity3D;
}

void GCMxInitialize3D(
    GCMATRIX3D *pMatrix
    )
{
    *pMatrix = gcmxIdentity3D;
}

void GCMxRotate3D(
    GCMATRIX3D *pMatrix,
    float angle
    )
{
	*pMatrix = gcmxIdentity3D;
	pMatrix->fixed.m11 = GCFIXFLOAT(cos(angle));
	pMatrix->fixed.m12 = GCFIXFLOAT(-sin(angle));
	pMatrix->fixed.m21 = GCFIXFLOAT(sin(angle));
	pMatrix->fixed.m22 = GCFIXFLOAT(cos(angle));
}

void GCMxTranslate3D(
    GCMATRIX3D *pMatrix,
    float x,
    float y
    )
{
	*pMatrix = gcmxIdentity3D;
	pMatrix->fixed.OffsetX = GCFIXFLOAT(x);
	pMatrix->fixed.OffsetY = GCFIXFLOAT(y);
}

void GCMxScale3D(
    GCMATRIX3D *pMatrix,
    float scalex,
    float scaley
    )
{
	*pMatrix = gcmxIdentity3D;
	pMatrix->fixed.m11 = GCFIXFLOAT(scalex);
	pMatrix->fixed.m22 = GCFIXFLOAT(scaley);
}

void GCMxMultiply3D(
    GCMATRIX3D *result,
    GCMATRIX3D *a,
    GCMATRIX3D *b
    )
{
	int row, col;
	for (row = 0; row < 4; row++)
	{
		for (col = 0; col < 4; col++)
		{
			result->array[row][col] = a->array[row][0] * b->array[0][col] +
									  a->array[row][1] * b->array[1][col] +
									  a->array[row][2] * b->array[2][col] +
									  a->array[row][3] * b->array[3][col];
		}
	}
}

void GCMxTransformPoint3D(
    GCPOINT *pPoint,
    GCMATRIX3D *pTransform
    )
{
}

void GCMxInverse3D(
	GCMATRIX3D *pSrc,
	GCMATRIX3D *pDst
	)
{
}

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

void GCMxGetPolyBounds3D(
	GC *pGC,
	GCRECT *pBounds,
	GCPOINT *pPoly,
	unsigned long polys,
	GCMATRIX3D *pMatrix,
	GCPOINT *pOrigin
	)
{
	pBounds->left = INT_MAX;
	pBounds->top = INT_MAX;
	pBounds->right = INT_MIN;
	pBounds->bottom = INT_MIN;
	unsigned long i;
	for (i = 0; i < polys; i++)
	{
		GCPOINT point = pPoly[i];
		GCMxTransformPoint3D(&point, pMatrix);
        pBounds->left = min(pBounds->left, point.x);
        pBounds->top = min(pBounds->top, point.y);
        pBounds->right = max(pBounds->right, point.x);
        pBounds->bottom = max(pBounds->bottom, point.y);
	}

	pBounds->left += pOrigin->x;
	pBounds->top += pOrigin->y;
	pBounds->right += pOrigin->x;
	pBounds->bottom += pOrigin->y;
}

