#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "gc.h"

const GCMATRIX gcmxIdentity =
{
    GCFIX(1), 0, 0,
    0, GCFIX(1), 0,
    0, 0, GCFIX(1)
};

GCMATRIX *GCMxIdentity()
{
    return &gcmxIdentity;
}

void GCMxInitialize(
    GCMATRIX *pMatrix
    )
{
    *pMatrix = gcmxIdentity;
}

void GCMxRotate(
    GCMATRIX *pMatrix,
    float angle
    )
{
	*pMatrix = gcmxIdentity;
	pMatrix->fixed.m11 = GCFIXFLOAT(cos(angle));
	pMatrix->fixed.m12 = GCFIXFLOAT(-sin(angle));
	pMatrix->fixed.m21 = GCFIXFLOAT(sin(angle));
	pMatrix->fixed.m22 = GCFIXFLOAT(cos(angle));
}

void GCMxTranslate(
    GCMATRIX *pMatrix,
    float x,
    float y
    )
{
	*pMatrix = gcmxIdentity;
	pMatrix->fixed.OffsetX = GCFIXFLOAT(x);
	pMatrix->fixed.OffsetY = GCFIXFLOAT(y);
}

void GCMxScale(
    GCMATRIX *pMatrix,
    float scalex,
    float scaley
    )
{
	*pMatrix = gcmxIdentity;
	pMatrix->fixed.m11 = GCFIXFLOAT(scalex);
	pMatrix->fixed.m22 = GCFIXFLOAT(scaley);
}

void GCMxMultiply(
    GCMATRIX *result,
    GCMATRIX *a,
    GCMATRIX *b
    )
{
	int row, col;
	for (row = 0; row < 3; row++)
	{
		for (col = 0; col < 3; col++)
		{
			result->array[row][col] = a->array[row][0] * b->array[0][col] +
									  a->array[row][1] * b->array[1][col] +
									  a->array[row][2] * b->array[2][col];
		}
	}
}

__inline void GCMxTransformPoint(
    GCPOINT *pPoint,
    GCMATRIX *pTransform
    )
{
	GCPOINT point = *pPoint;

	pPoint->x = (point.x * pTransform->array[0][0]) +
			    (point.y * pTransform->array[1][0]) +
			    (1 * pTransform->array[2][0]);

	pPoint->y = point.x * pTransform->array[0][1] +
			    point.y * pTransform->array[1][1] +
			    1 * pTransform->array[2][1];
}

static inline void invert3x3(const GCFIXED * src, GCFIXED * dst)
{
    float det;

    /* Compute adjoint: */

    dst[0] = + src[4] * src[8] - src[5] * src[7];
    dst[1] = - src[1] * src[8] + src[2] * src[7];
    dst[2] = + src[1] * src[5] - src[2] * src[4];
    dst[3] = - src[3] * src[8] + src[5] * src[6];
    dst[4] = + src[0] * src[8] - src[2] * src[6];
    dst[5] = - src[0] * src[5] + src[2] * src[3];
    dst[6] = + src[3] * src[7] - src[4] * src[6];
    dst[7] = - src[0] * src[7] + src[1] * src[6];
    dst[8] = + src[0] * src[4] - src[1] * src[3];

    /* Compute determinant: */

    det = src[0] * dst[0] + src[1] * dst[3] + src[2] * dst[6];

    /* Multiply adjoint with reciprocal of determinant: */

    det = 1.0f / det;

    dst[0] *= det;
    dst[1] *= det;
    dst[2] *= det;
    dst[3] *= det;
    dst[4] *= det;
    dst[5] *= det;
    dst[6] *= det;
    dst[7] *= det;
    dst[8] *= det;
}

void GCMxInverse(
	GCMATRIX *pSrc,
	GCMATRIX *pDst
	)
{
	invert3x3(&pSrc->array, &pDst->array);
}

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

void GCMxGetPolyBounds(
	GC *pGC,
	GCRECT *pBounds,
	GCPOINT *pPoly,
	unsigned long polys,
	GCMATRIX *pMatrix,
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
		GCMxTransformPoint(&point, pMatrix);
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

