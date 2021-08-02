
// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneSoftwareOcclusion.cpp 
=============================================================================*/
#include<algorithm>
#include "SceneSoftwareOcclusion.h"
#include"Library/Math/Matrix.h"

//�����Χ����Ļ�ռ�Ĵ�С �������Сȥ��Ϊ�ڵ����Ȩ�� Ȼ��ü���һЩ�ڵ��������
float ComputeBoundsScreenSize(const FVector4& BoundsOrigin, const float SphereRadius, const FVector4& ViewOrigin, const FMatrix& ProjMatrix)
{
	//��Χ�����ĺ�����ľ���
	const float Dist = FVector::Dist(BoundsOrigin, ViewOrigin);

	// M[0][0] = 1.0f / FMath::Tan(HalfFOV)  M[1][1] = Width / FMath::Tan(HalfFOV) / Height,
	const float ScreenMultiple = FMath::Max(0.5f * ProjMatrix.M[0][0], 0.5f * ProjMatrix.M[1][1]);

	const float ScreenRadius = ScreenMultiple * SphereRadius / FMath::Max(1.0f, Dist);

	return ScreenRadius * 2.0f;
}

//�����Χ����Ļ�ռ�Ĵ�С
float ComputeBoundsScreenSize(const FVector4& Origin, const float SphereRadius, const FSceneView& View)
{
	return ComputeBoundsScreenSize(Origin, SphereRadius, View.ViewMatrices.ViewOrigin, View.ViewMatrices.ProjectionMatrix);
}

// �õ�Ͱ����ÿһ�У�һ�о���һ��ɨ���ߣ���mask
inline uint64 ComputeBinRowMask(int32 BinMinX, float fX0, float fX1)
{
	//�����fX0 fX1�����ڵ�64bit�����bt��λ�� x0 x1
	int32 X0 = FMath::RoundToInt(fX0) - BinMinX;
	int32 X1 = FMath::RoundToInt(fX1) - BinMinX;

	//x0 x1 Ҫ��0-63֮��
	if (X0 >= BIN_WIDTH || X1 < 0)
	{
		// not in bin
		return 0ull;
	}
	else
	{
		//x0 x1 Ҫ��0-63֮��
		X0 = FMath::Max(0, X0);
		X1 = FMath::Min(BIN_WIDTH-1, X1);
		int32 Num = (X1 - X0) + 1;
		//(1) ��������һ�� ��ô���� 11111111.... 64bit
		//(2) �������Ȳ�һ��  ����x0=1 Num3 ��ô���� ����8bit������
		// 00000001 << 3 -> 00001000
		// 00001000 - 1  -> 00000111
		// 00000111 << 1 -> 00001110
		// ͬ��ȥ����64bit��
		return (Num == BIN_WIDTH) ? ~0ull : ((1ull << Num) - 1) << X0;
	}
}


/* ��դ����������� ɨ���߻�������ηֲ�����
*  ��ͼ��һ��half DX0 DX1�ǲ���
*  Row1         /\
*              /  \
*             /    \
*            /      \
*           /        \
*          /          \
*        x0------------x1
*        /              \
*  Row0 /----------------\
*/   
inline void RasterizeHalf(float X0, float X1, float DX0, float DX1, int32 Row0, int32 Row1, uint64* BinData, int32 BinMinX)
{
	//����ÿһ�� X0����һ�о�DX0�Ĳ��� ͬ��X1
	for (int32 Row = Row0; Row <= Row1; Row++, X0+=DX0, X1+=DX1)
	{
		//��ǰ�е�mask����
		uint64 FrameBufferMask = BinData[Row];
		//�����ȫ��դ���������ٹ�դ����
		if (FrameBufferMask != ~0ull) // whether this row is already fully rasterized
		{
			//�õ������һ�е�mask����
			uint64 RowMask = ComputeBinRowMask(BinMinX, X0, X1);
			//�������1��
			if (RowMask)
			{
				//mask �� ����
				BinData[Row] = (FrameBufferMask | RowMask);
			}
		}
	}
}

//��դ���ڵ����������
static void RasterizeOccluderTri(const FScreenTriangle& Tri, uint64* BinData, int32 BinMinX)
{
	//�ڵ������Ļ���� A B C ע����������������˵� AddTriangle��ʱ��� ABC������ C��Y ���  A��Y��С 
	FScreenPosition A = Tri.V[0];
	FScreenPosition B = Tri.V[1];
	FScreenPosition C = Tri.V[2];
	//��С�к� ����к�
	int32 RowMin = FMath::Max<int32>(A.Y, 0);
	int32 RowMax = FMath::Min<int32>(FRAMEBUFFER_HEIGHT-1, C.Y);

	bool bRasterized = false;

	int32 RowS = RowMin;
	//����еĵ������С�� ��ô��դ�������ε��²��� A->B �ⲿ��
	if ((B.Y - RowMin) > 0)
	{
		// A -> B
		int32 RowE = FMath::Min<int32>(RowMax, B.Y);
		// �����ߵ��ݶ�
		float dX0 = float(B.X - A.X)/(B.Y - A.Y);
		float dX1 = float(C.X - A.X)/(C.Y - A.Y);
		if (dX0 > dX1)
		{
			Swap(dX0, dX1);
		}
		float X0 = A.X + dX0*(RowS - A.Y);
		float X1 = A.X + dX1*(RowS - A.Y);
		ensure(X0 <= X1);
		//��դ�����half
		RasterizeHalf(X0, X1, dX0, dX1, RowS, RowE, BinData, BinMinX);
		bRasterized|= true;
		RowS = RowE + 1;
	}

	//����еĵ������С�� ��ô��դ�������ε��ϲ��� B -> C
	if ((RowMax - RowS) > 0)
	{
		// B -> C
		// Edge gradients
		float dX0 = float(C.X - A.X)/(C.Y - A.Y);
		float dX1 = float(C.X - B.X)/(C.Y - B.Y);
		float X0 = A.X + dX0*(RowS - A.Y);
		float X1 = B.X + dX1*(RowS - B.Y);
		if (X0 > X1) 
		{
			Swap(X0, X1);
			Swap(dX0, dX1);
		}
		RasterizeHalf(X0, X1, dX0, dX1, RowS, RowMax, BinData, BinMinX);
		bRasterized|= true;
	}

	// ��û����դ�� ��ô��һ����
	if (!bRasterized)
	{
		float X0 = FMath::Min3(A.X, B.X, C.X);
		float X1 = FMath::Max3(A.X, B.X, C.X);
		RasterizeHalf(X0, X1, 0.0f, 0.0f, RowS, RowS, BinData, BinMinX);
	}
}

//��դ�����ڵ����Quad
//	
//  +----+ V1 RowMax
//  |    |
//  |    |
//  +----+ V0 RowMin
// 
static bool RasterizeOccludeeQuad(const FScreenTriangle& Tri, uint64* BinData, int32 BinMinX)
{
	int32 RowMin = Tri.V[0].Y; 
	int32 RowMax = Tri.V[2].Y; 

	// clip X to bin bounds
	int32 X0 =  FMath::Max(Tri.V[0].X - BinMinX, 0);
	int32 X1 =  FMath::Min(Tri.V[1].X - BinMinX, BIN_WIDTH - 1);

	// �õ����quad ����mask  ûһ�ж�һ�� 
	int32 NumBits = (X1 - X0) + 1;
	uint64 RowMask = (NumBits == BIN_WIDTH) ? ~0ull : ((1ull << NumBits) - 1) << X0;

	// ����ÿһ�� ��mask ��ѯ
	for (int32 Row = RowMin; Row <= RowMax; ++Row)
	{
		uint64 FrameBufferMask = BinData[Row];
		if ((~FrameBufferMask & RowMask))
		{
			return true;
		}
	}
	
	return false;
}

//�����޳�
static bool TestFrontface(const FScreenTriangle& Tri)
{
	if ((Tri.V[2].X - Tri.V[0].X) * (Tri.V[1].Y - Tri.V[0].Y) >= (Tri.V[2].Y - Tri.V[0].Y) * (Tri.V[1].X - Tri.V[0].X))
	{
		return false; 
	}
	return true;
}

//���������
inline bool AddTriangle(FScreenTriangle& Tri, float TriDepth, FPrimitiveComponentId PrimitiveId, uint8 MeshFlags, FOcclusionFrameData& InData)
{
	if (MeshFlags == 1) // occluder tri
	{
		// ����һ��
		if (Tri.V[0].Y > Tri.V[1].Y) Swap(Tri.V[0], Tri.V[1]);
		if (Tri.V[1].Y > Tri.V[2].Y) Swap(Tri.V[1], Tri.V[2]);
		if (Tri.V[0].Y > Tri.V[1].Y) Swap(Tri.V[0], Tri.V[1]);
		//�����Ļ�����β�����Ļ�� ���޳�
		if (Tri.V[0].Y >= FRAMEBUFFER_HEIGHT || Tri.V[2].Y < 0)
		{
			return false;
		}
	}

	// ����������
	InData.ScreenTriangles.push_back(Tri);
	int32 TriangleID = InData.ScreenTriangles.size() - 1;
	InData.ScreenTrianglesPrimID.push_back(PrimitiveId);
	InData.ScreenTrianglesFlags.push_back(MeshFlags);
	
	// ������������ο�Խ����ЩͰ BinMin ----> BinMax
	int32 MinX = FMath::Min3(Tri.V[0].X, Tri.V[1].X, Tri.V[2].X) / BIN_WIDTH; 
	int32 MaxX = FMath::Max3(Tri.V[0].X, Tri.V[1].X, Tri.V[2].X) / BIN_WIDTH;
	int32 BinMin = FMath::Max(MinX, 0);
	int32 BinMax = FMath::Min(MaxX, BIN_NUM-1);

	// ���������ε����
	FSortedIndexDepth SortedIndexDepth;
	SortedIndexDepth.Index = TriangleID;
	SortedIndexDepth.Depth = TriDepth;

	// ����Ͱ����
	for (int32 BinIdx = BinMin; BinIdx <= BinMax; ++BinIdx)
	{
		InData.SortedTriangles[BinIdx].push_back(SortedIndexDepth);
	}

	return true;
}

// ��clip�ռ� ת��buffer ����
static const VectorRegister vFramebufferBounds = MakeVectorRegister(FRAMEBUFFER_WIDTH-1, FRAMEBUFFER_HEIGHT-1, 1.0f, 1.0f);
static const VectorRegister vXYHalf = MakeVectorRegister(0.5f, 0.5f, 0.0f, 0.0f);

// BEGIN Intel
static const int32 NUM_CUBE_VTX = 8;
// 0 = min corner, 1 = max corner
static const uint32 sBBxInd[NUM_CUBE_VTX] = { 1, 0, 0, 1, 1, 1, 0, 0 };
static const uint32 sBByInd[NUM_CUBE_VTX] = { 1, 1, 1, 1, 0, 0, 0, 0 };
static const uint32 sBBzInd[NUM_CUBE_VTX] = { 1, 1, 0, 0, 0, 1, 1, 0 };
// END Intel

// ���ڵ�������դ�ļ��ν׶� SIMD�Ż�
static void ProcessOccludeeGeomSIMD(const FMatrix& InMat, const FVector* InMinMax, int32 Num, int32* RESTRICT OutQuads, float* RESTRICT OutQuadDepth, int32* RESTRICT OutQuadClipped)
{
	const float W_CLIP = InMat.M[3][2];
	VectorRegister vClippingW = VectorLoadFloat1(&W_CLIP);
	VectorRegister mRow0  = VectorLoadAligned(InMat.M[0]);
	VectorRegister mRow1  = VectorLoadAligned(InMat.M[1]);
	VectorRegister mRow2  = VectorLoadAligned(InMat.M[2]);
	VectorRegister mRow3  = VectorLoadAligned(InMat.M[3]);
	VectorRegister xRow[2], yRow[2], zRow[2];
	
	for (int32 k = 0; k < Num; ++k)
	{
		FVector BoxMin = *(InMinMax++);
		FVector BoxMax = *(InMinMax++);

// BEGIN Intel
		// Project primitive bounding box to screen
		xRow[0] = VectorMultiply(VectorLoadFloat1(&BoxMin.X), mRow0);
		xRow[1] = VectorMultiply(VectorLoadFloat1(&BoxMax.X), mRow0);
		yRow[0] = VectorMultiply(VectorLoadFloat1(&BoxMin.Y), mRow1);
		yRow[1] = VectorMultiply(VectorLoadFloat1(&BoxMax.Y), mRow1);
		zRow[0] = VectorMultiply(VectorLoadFloat1(&BoxMin.Z), mRow2);
		zRow[1] = VectorMultiply(VectorLoadFloat1(&BoxMax.Z), mRow2);
		
		VectorRegister vClippedFlag = VectorZero();	
		VectorRegister vScreenMin = GlobalVectorConstants::BigNumber;
		VectorRegister vScreenMax = VectorNegate(vScreenMin);
			
		for (int32 i = 0; i < NUM_CUBE_VTX; ++i)
		{
			VectorRegister V;
			V = VectorAdd(mRow3,	xRow[sBBxInd[i]]);
			V = VectorAdd(V,		yRow[sBByInd[i]]);
			V = VectorAdd(V,		zRow[sBBzInd[i]]);
		
			VectorRegister W = VectorReplicate(V, 3);
			vClippedFlag = VectorBitwiseOr(vClippedFlag, VectorCompareLT(W, vClippingW));
			V = VectorDivide(V, W);
				
			vScreenMin = VectorMin(vScreenMin, V);
			vScreenMax = VectorMax(vScreenMax, V);
		}
// END Intel

		// For pixel snapping
		vScreenMin = VectorAdd(vScreenMin, vXYHalf);
		vScreenMax = VectorAdd(vScreenMax, vXYHalf); 
	
		// Clip against screen rect
		vScreenMin = VectorMax(vScreenMin, VectorZero());
		vScreenMax = VectorMin(vScreenMax, vFramebufferBounds); // Z should be unaffected
		
		// Make: MinX, MinY, MaxX, MaxY
		VectorRegisterInt IntMinMax = VectorFloatToInt(VectorCombineLow(vScreenMin, vScreenMax));
			
		// Store
		VectorIntStoreAligned(IntMinMax, OutQuads);
		VectorStoreFloat1(vClippedFlag, OutQuadClipped);
		*OutQuadDepth = VectorGetComponent(vScreenMax, 2);
		
		OutQuads+=4;
		OutQuadDepth++;
		OutQuadClipped++;
	}
}

// ���ڵ�������դ�ļ��ν׶�
// 512��һ��
//InMat : WorldToFB
//InMinMax : ��Χ��
//Num : ����
//OutQuads �����
//OutQuadDepth �� ���
//OutQuadClipped �� ���
static void ProcessOccludeeGeomScalar(const FMatrix& InMat, const FVector* InMinMax, int32 Num, int32* RESTRICT OutQuads, float* RESTRICT OutQuadDepth, int32* RESTRICT OutQuadClipped)
{
	// �������� ת�� FBO����ľ���
	const float W_CLIP =  InMat.M[3][2];
	FVector4 AX = FVector4(InMat.M[0][0], InMat.M[0][1], InMat.M[0][2], InMat.M[0][3]);
	FVector4 AY = FVector4(InMat.M[1][0], InMat.M[1][1], InMat.M[1][2], InMat.M[1][3]);
	FVector4 AZ = FVector4(InMat.M[2][0], InMat.M[2][1], InMat.M[2][2], InMat.M[2][3]);
	FVector4 AW = FVector4(InMat.M[3][0], InMat.M[3][1], InMat.M[3][2], InMat.M[3][3]);
	FVector4 xRow[2], yRow[2], zRow[2];

	// ����512Ϊһ��İ�Χ��
	for (int32 k = 0; k < Num; ++k)
	{
		FVector BoxMin = *(InMinMax++);
		FVector BoxMax = *(InMinMax++);
		// Project primitive bounding box to screen
		xRow[0] = FVector4(BoxMin.X, BoxMin.X, BoxMin.X, BoxMin.X) * AX;
		xRow[1] = FVector4(BoxMax.X, BoxMax.X, BoxMax.X, BoxMax.X) * AX;
		yRow[0] = FVector4(BoxMin.Y, BoxMin.Y, BoxMin.Y, BoxMin.Y) * AY;
		yRow[1] = FVector4(BoxMax.Y, BoxMax.Y, BoxMax.Y, BoxMax.Y) * AY;
		zRow[0] = FVector4(BoxMin.Z, BoxMin.Z, BoxMin.Z, BoxMin.Z) * AZ;
		zRow[1] = FVector4(BoxMax.Z, BoxMax.Z, BoxMax.Z, BoxMax.Z) * AZ;
			
		FVector2D MinXY = FVector2D(MAX_flt, MAX_flt);
		FVector2D MaxXY = FVector2D(-MAX_flt, -MAX_flt);
		float Depth = 0.f;
		bool bClippedNear = false;
	
		for (int32 i = 0; i < NUM_CUBE_VTX; i++)
		{
			FVector4 V = AW;
			V = V + xRow[sBBxInd[i]];
			V = V + yRow[sBByInd[i]];
			V = V + zRow[sBBzInd[i]];
			
			if (V.W < W_CLIP)
			{
				bClippedNear = true;
				break;
			}

			V = V / V.W;

			MinXY.X = FMath::Min(MinXY.X, V.X);
			MinXY.Y = FMath::Min(MinXY.Y, V.Y);
			MaxXY.X = FMath::Max(MaxXY.X, V.X);
			MaxXY.Y = FMath::Max(MaxXY.Y, V.Y);
			Depth = FMath::Max(Depth, V.Z);
		}

		if (bClippedNear)
		{
			OutQuadClipped[0] = 1;
		}
		else
		{
			// For pixel snapping
			MinXY = MinXY + FVector2D(0.5f, 0.5f);
			MaxXY = MaxXY + FVector2D(0.5f, 0.5f);

			// Clip against screen rect
			MinXY.X = FMath::Max(0.f, MinXY.X);
			MinXY.Y = FMath::Max(0.f, MinXY.Y);
			MaxXY.X = FMath::Min(FRAMEBUFFER_WIDTH-1.f, MaxXY.X);
			MaxXY.Y = FMath::Min(FRAMEBUFFER_HEIGHT-1.f, MaxXY.Y);

			// Make MinX, MinY, MaxX, MaxY
			OutQuads[0] = (int32)MinXY.X;
			OutQuads[1] = (int32)MinXY.Y;
			OutQuads[2] = (int32)MaxXY.X;
			OutQuads[3] = (int32)MaxXY.Y;
		
			OutQuadDepth[0] = Depth;
			OutQuadClipped[0] = 0;
		}
			
		OutQuads+=4;
		OutQuadDepth++;
		OutQuadClipped++;
	}
}

const FMatrix FramebufferMat(
			FVector(0.5f*(float)FRAMEBUFFER_WIDTH,  0.0f,							0.0f),
			FVector(0.0f,							0.5f*(float)FRAMEBUFFER_HEIGHT, 0.0f),
			FVector(0.0f,							0.0f,							1.0f),
			FVector(0.5f*(float)FRAMEBUFFER_WIDTH,	0.5f*(float)FRAMEBUFFER_HEIGHT, 0.0f)
		);


static bool ProcessOccludeeGeom(const FOcclusionSceneData& SceneData, FOcclusionFrameData& FrameData, TMap<FPrimitiveComponentId, bool>& VisibilityMap)
{
	const int32 RUN_SIZE = 512;
	const bool bUseSIMD = GSOSIMD != 0;
		
	int32 NumBoxes = SceneData.OccludeeBoxMinMax.size()/2;
	const FVector* MinMax = SceneData.OccludeeBoxMinMax.data();

	const FPrimitiveComponentId* PrimIds = SceneData.OccludeeBoxPrimId.data();

	FMatrix WorldToFB = SceneData.ViewProj * FramebufferMat;
	
	// ����Ҫ���£���
	MS_ALIGN(SIMD_ALIGNMENT) int32 Quads[RUN_SIZE*4] GCC_ALIGN(SIMD_ALIGNMENT);
	float QuadDepths[RUN_SIZE];
	int32 QuadClipFlags[RUN_SIZE];

	int32 NumRuns = NumBoxes/RUN_SIZE + 1;
	int32 NumBoxesProcessed = 0;

	// 512��Ϊһ��Runs 
	for (int32 RunIdx = 0; RunIdx < NumRuns; ++RunIdx)
	{
		int32 RunSize = FMath::Min(NumBoxes - NumBoxesProcessed, RUN_SIZE);
		
		// Generate quads
		if (bUseSIMD)
		{
			ProcessOccludeeGeomSIMD(WorldToFB, MinMax, RunSize, Quads, QuadDepths, QuadClipFlags);
		}
		else
		{
			ProcessOccludeeGeomScalar(WorldToFB, MinMax, RunSize, Quads, QuadDepths, QuadClipFlags);
		}
							
		// ���ڵ����Quads
		int32 QuadIdx = 0;
		for (int32 i = 0; i < RunSize; ++i)
		{
			int32 MinX = Quads[QuadIdx++];
			int32 MinY = Quads[QuadIdx++];
			int32 MaxX = Quads[QuadIdx++];
			int32 MaxY = Quads[QuadIdx++];
			
			FPrimitiveComponentId PrimitiveId = PrimIds[i];

			if (QuadClipFlags[i] != 0)
			{
				// clipped by near plane, visible
				VisibilityMap[PrimitiveId] = true;
				continue;
			}

			// Check MinX <= MaxX and MinY <= MaxY
			if (MinX > MaxX || MinY > MaxY)
			{
				// Do not rasterize if not on screen, occluded
				VisibilityMap[PrimitiveId] = false;
				continue;
			}
						
			float Depth = QuadDepths[i];
						
			// Quad�洢һ�������ξͿ���
			FScreenTriangle ST;
			ST.V[0] = {MinX, MinY};
			ST.V[1] = {MaxX, MaxY};
			ST.V[2] = {MinX, MaxY};
			AddTriangle(ST, Depth, PrimitiveId, 0, FrameData);
		}

		MinMax+= (RunSize*2);
		PrimIds+= RunSize;
		NumBoxesProcessed+= RunSize;
		
	} // for each run

	return true;
}

//�ռ����ڵ���
static void CollectOccludeeGeom(const FBoxSphereBounds& Bounds, FPrimitiveComponentId PrimitiveId, FOcclusionSceneData& SceneData)
{
	const FBox Box = Bounds.GetBox();
	SceneData.OccludeeBoxMinMax.push_back(Box.Min);
	SceneData.OccludeeBoxMinMax.push_back(Box.Max);
	SceneData.OccludeeBoxPrimId.push_back(PrimitiveId);
}

//�ü�
static bool ClippedVertexToScreen(const FVector4& XFV, FScreenPosition& OutSP, float& OutDepth)
{
	//͸��У�������w ��һ�� Ȼ��õ���Ļ����
	FVector4 FSP = XFV / XFV.W;
	int32 X = FMath::RoundToInt((FSP.X + 1.f) * FRAMEBUFFER_WIDTH/2.0);
	int32 Y = FMath::RoundToInt((FSP.Y + 1.f) * FRAMEBUFFER_HEIGHT/2.0);
	
	OutSP.X = X;
	OutSP.Y = Y;
	OutDepth = FSP.Z;
	return false;
}

//ͶӰ������Ƶ��ǣ� 
//-Z < x1 x Z < Z
//-Z < y1 x Z < Z
//-Z < z1 x Z < Z 
// => 1/aspect cot(FOV/2) 0   0   0  �����Ƶ�������Ӧ�ø�Z�Ƚ�  ��Z�Ǵ���W����� ���Ҫ���ÿ���ͶӰ������Ƶ����������涼�Ǹ�w���Ƚ� 
static uint8 ProcessXFormVertex(const FVector4& XFV, float W_CLIP)
{
	uint8 Flags = 0;
	float W = XFV.W;

	if (W < W_CLIP)
	{
		Flags|= EScreenVertexFlags::ClippedNear;
	}
		
	if (XFV.X < -W)
	{
		Flags|= EScreenVertexFlags::ClippedLeft;
	}
		
	if (XFV.X > W)
	{
		Flags|= EScreenVertexFlags::ClippedRight;
	}
		
	if (XFV.Y < -W)
	{
		Flags|= EScreenVertexFlags::ClippedTop;
	}
		
	if (XFV.Y > W)
	{
		Flags|= EScreenVertexFlags::ClippedBottom;
	}

	return Flags;
}

static void ProcessOccluderGeom(const FOcclusionSceneData& SceneData, FOcclusionFrameData& OutData)
{
	const float W_CLIP = SceneData.ViewProj.M[3][2];

	const int32 NumMeshes = SceneData.OccluderData.size();
	const FOcclusionMeshData* MeshData = SceneData.OccluderData.data();

	TArray<FVector4>	ClipVertexBuffer;
	TArray<uint8>		ClipVertexFlagsBuffer;

	for (int32 MeshIdx = 0; MeshIdx < NumMeshes; ++MeshIdx)
	{
		const FOcclusionMeshData& Mesh = MeshData[MeshIdx];
		int32 NumVtx = Mesh.VerticesSP.size();
		
		//ZB todo performance
		ClipVertexBuffer.resize(NumVtx);
		ClipVertexFlagsBuffer.resize(NumVtx);

		const FVector* MeshVertices = Mesh.VerticesSP.data();
		FVector4* MeshClipVertices = ClipVertexBuffer.data();
		uint8*	MeshClipVertexFlags = ClipVertexFlagsBuffer.data();
		
		
		{
			const FMatrix LocalToClip = Mesh.LocalToWorld * SceneData.ViewProj;
			// ����(4) x ����(4x4) ��SIMD�Ż�
			// ��SIMD:16 mul,12 add
			// SIMD: 4 laod, 4 Mul,3add ԭ����1/4
			VectorRegister mRow0  = VectorLoadAligned(LocalToClip.M[0]);
			VectorRegister mRow1  = VectorLoadAligned(LocalToClip.M[1]);
			VectorRegister mRow2  = VectorLoadAligned(LocalToClip.M[2]);
			VectorRegister mRow3  = VectorLoadAligned(LocalToClip.M[3]);
			
			for (int32 i = 0; i < NumVtx; ++i)
			{
				VectorRegister VTempX = VectorLoadFloat1(&MeshVertices[i].X);
				VectorRegister VTempY = VectorLoadFloat1(&MeshVertices[i].Y);
				VectorRegister VTempZ = VectorLoadFloat1(&MeshVertices[i].Z);
				VectorRegister VTempW;
				// Mul by the matrix
				VTempX = VectorMultiply(VTempX, mRow0);
				VTempY = VectorMultiply(VTempY, mRow1);
				VTempZ = VectorMultiply(VTempZ, mRow2);
				VTempW = VectorMultiply(GlobalVectorConstants::FloatOne, mRow3);
				// Add them all together
				VTempX = VectorAdd(VTempX, VTempY);
				VTempZ = VectorAdd(VTempZ, VTempW);
				VTempX = VectorAdd(VTempX, VTempZ);
				// Store
				VectorStoreAligned(VTempX, &MeshClipVertices[i]);
				//����βü�����ϵ������w���ü�
				uint8 VertexFlags = ProcessXFormVertex(MeshClipVertices[i], W_CLIP);
				MeshClipVertexFlags[i] = VertexFlags;
			}
		}
	
		const uint16* MeshIndices = Mesh.IndicesSP.data();
		int32 NumTris = Mesh.IndicesSP.size()/3;
		int32 NumDataTris = OutData.ScreenTriangles.size();

		// ����������
		for (int32 i = 0; i < NumTris; ++i)
		{
			uint16 I0 = MeshIndices[i*3 + 0];
			uint16 I1 = MeshIndices[i*3 + 1];
			uint16 I2 = MeshIndices[i*3 + 2];

			uint8 F0 = MeshClipVertexFlags[I0];
			uint8 F1 = MeshClipVertexFlags[I1];
			uint8 F2 = MeshClipVertexFlags[I2];

			if ((F0 & F1) & F2)
			{
				// �����������һ����������Ͳõ� ����Ϊ�ڵ����� ���ڵ�����һ��
				continue;
			}

			FVector4 V[3] = 
			{
				MeshClipVertices[I0],
				MeshClipVertices[I1],
				MeshClipVertices[I2]
			};

			uint8 TriFlags = F0 | F1 | F2;

			//��ƽ��Ĳü�
			if (TriFlags & EScreenVertexFlags::ClippedNear)
			{
				static const int32 Edges[3][2] = {{0,1}, {1,2}, {2,0}};
				FVector4 ClippedPos[4];
				int32 NumPos = 0;
			
				for(int32 EdgeIdx = 0; EdgeIdx < 3; EdgeIdx++)
				{
					int32 i0 = Edges[EdgeIdx][0];
					int32 i1 = Edges[EdgeIdx][1];

					bool dot0 = V[i0].W < W_CLIP;
					bool dot1 = V[i1].W < W_CLIP;
								
					if (!dot0)
					{
						ClippedPos[NumPos] = V[i0];
						NumPos++;
					}
				
					if (dot0 != dot1)
					{
						float t = (W_CLIP - V[i0].W) / (V[i0].W - V[i1].W);
						ClippedPos[NumPos] = V[i0] + t*(V[i0] - V[i1]);
						NumPos++;
					}
				}
			
				// triangulate clipped vertices
				for (int32 j = 2; j < NumPos; j++)
				{
					FScreenTriangle Tri;
					float Depths[3];
					bool bShouldDiscard = false;

					bShouldDiscard|= ClippedVertexToScreen(ClippedPos[0],	Tri.V[0], Depths[0]);
					bShouldDiscard|= ClippedVertexToScreen(ClippedPos[j-1],	Tri.V[1], Depths[1]);
					bShouldDiscard|= ClippedVertexToScreen(ClippedPos[j],	Tri.V[2], Depths[2]);
								
					if (!bShouldDiscard && TestFrontface(Tri))
					{
						// Min tri depth for occluder (further from screen)
						float TriDepth = FMath::Min3(Depths[0], Depths[1], Depths[2]);
						AddTriangle(Tri, TriDepth, Mesh.PrimId, 1, OutData);
					}
				}
			}
			else
			{
				FScreenTriangle Tri;
				float Depths[3];
				bool bShouldDiscard = false;	//ò��ûʲô����
						
				for (int32 j = 0; j < 3 && !bShouldDiscard; ++j)
				{
					bShouldDiscard|= ClippedVertexToScreen(V[j], Tri.V[j], Depths[j]);
				}
			
				if (!bShouldDiscard && TestFrontface(Tri))
				{
					// Min tri depth for occluder (further from screen)
					float TriDepth = FMath::Min3(Depths[0], Depths[1], Depths[2]);
					AddTriangle(Tri, TriDepth, Mesh.PrimId, /*MeshFlags*/ 1, OutData);
				}
			}
		} // for each triangle
	}// for each mesh
}

static void ProcessOcclusionFrame(const FOcclusionSceneData& InSceneData, FOcclusionFrameResults& OutResults)
{
	FOcclusionFrameData FrameData;
	//�ڵ��������� ���ռ���ʱ�����  ���ڵ�������һ��boxһ��������
	int32 NumExpectedTriangles = InSceneData.NumOccluderTriangles + InSceneData.OccludeeBoxPrimId.size(); 
	FrameData.ReserveBuffers(NumExpectedTriangles);

	{
		ProcessOccluderGeom(InSceneData, FrameData);
	}

	{
		ProcessOccludeeGeom(InSceneData, FrameData, OutResults.VisibilityMap);
	}

	int32 NumRasterizedOccluderTris = 0;
	int32 NumRasterizedOccludeeTris = 0;
	{
		SCOPE_CYCLE_COUNTER(STAT_SoftwareOcclusionRasterize);

		const uint8* MeshFlags = FrameData.ScreenTrianglesFlags.data();
		const FPrimitiveComponentId* PrimitiveIds = FrameData.ScreenTrianglesPrimID.data();
		const FScreenTriangle* Tris = FrameData.ScreenTriangles.data();
						
		for (int32 BinIdx = 0; BinIdx < BIN_NUM; ++BinIdx)
		{
			std::sort(FrameData.SortedTriangles[BinIdx].begin(), FrameData.SortedTriangles[BinIdx].end(),
				[](FSortedIndexDepth A, FSortedIndexDepth B)
				{return A.Depth > B.Depth;  });
	
			const FSortedIndexDepth* SortedTriIndices = FrameData.SortedTriangles[BinIdx].data();
			const int32 NumTris = FrameData.SortedTriangles[BinIdx].size();
			const int32 BinMinX = BinIdx*BIN_WIDTH;
			FFramebufferBin& Bin = OutResults.Bins[BinIdx];
			// TODO: add a way to check when bin is already fully rasterized, so we can skip this work

			for (int32 TriIdx = 0; TriIdx < NumTris; ++TriIdx)
			{
				int32 TriID = SortedTriIndices[TriIdx].Index;
				uint8 Flags = MeshFlags[TriID];
				FPrimitiveComponentId PrimitiveId = PrimitiveIds[TriID];
				const FScreenTriangle& Tri = Tris[TriID];

				if (Flags != 0)
				{
					// rasterize occluder
					RasterizeOccluderTri(Tri, Bin.Data, BinMinX);
					NumRasterizedOccluderTris++;
				}
				else
				{
					// rasterize occludee
					bool& VisBit = OutResults.VisibilityMap[PrimitiveId];
					bool bVisible = RasterizeOccludeeQuad(Tri, Bin.Data, BinMinX);
					VisBit|= bVisible;
					NumRasterizedOccludeeTris++;
				}
			}
		}
	}
}

FSceneSoftwareOcclusion::FSceneSoftwareOcclusion()
{
	Processing = std::make_unique<FOcclusionFrameResults>();
	memset(&(Processing.get()->Bins),0, BIN_NUM * FRAMEBUFFER_HEIGHT *sizeof(uint64));
}

FSceneSoftwareOcclusion::~FSceneSoftwareOcclusion()
{
}


const static float OCCLUDER_DISTANCE_WEIGHT = 10000.f;
static float ComputePotentialOccluderWeight(float ScreenSize, float DistanceSquared)
{
	return ScreenSize + OCCLUDER_DISTANCE_WEIGHT/DistanceSquared;
}

//FGraphEventRef -> void
static void SubmitScene(const FScene* Scene, FViewInfo& View, FOcclusionFrameResults* Results)
{
	int32 NumCollectedOccluders = 0;
	int32 NumCollectedOccludees = 0;
			

	const FMatrix ViewProjMat = View.ViewMatrices.ViewMatrix * View.ViewMatrices.ProjectionMatrix;
	const FVector ViewOrigin = View.ViewMatrices.ViewOrigin;
	const float MaxDistanceSquared = FMath::Square(GSOMaxDistanceForOccluder);
	
	// Allocate occlusion scene
	std::unique_ptr<FOcclusionSceneData> SceneData = std::make_unique<FOcclusionSceneData>();
	SceneData->ViewProj = ViewProjMat;

	const int32 NumReserveOccludee = 1024;
	SceneData->OccludeeBoxPrimId.reserve(NumReserveOccludee);
	SceneData->OccludeeBoxMinMax.reserve(NumReserveOccludee*2);
	SceneData->OccluderData.reserve(GSOMaxOccluderNum);
			
	// Collect scene geometry: occluders, occludees
	{
		SCOPE_CYCLE_COUNTER(STAT_SoftwareOcclusionGather);
				
		FSWOccluderElementsCollector Collector(*SceneData);
		
		TArray<FPotentialOccluderPrimitive> PotentialOccluders;
		PotentialOccluders.reserve(GSOMaxOccluderNum);
		
		for (int i = 0;i < View.PrimitiveVisibilityMap.size();i++)
		{
			uint32 PrimitiveIndex = View.PrimitiveVisibilityMap[i];
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];
			const FBoxSphereBounds& Bounds = Scene->PrimitiveOcclusionBounds[PrimitiveIndex];
			const uint8 OcclusionFlags = Scene->PrimitiveOcclusionFlags[PrimitiveIndex];
			const FPrimitiveComponentId PrimitiveComponentId = PrimitiveSceneInfo->PrimitiveComponentId;
			FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;
			
			const bool bHasHugeBounds = Bounds.SphereRadius > HALF_WORLD_MAX/2.0f; // big objects like skybox
			float DistanceSquared = 0.f;
			float ScreenSize = 0.f;
			
			// Find out whether primitive can/should be occluder or occludee
			bool bCanBeOccluder = !bHasHugeBounds && Proxy->ShouldUseAsOccluder();
			if (bCanBeOccluder)
			{
				// Size/distance requirements
				DistanceSquared = FMath::Max(OCCLUDER_DISTANCE_WEIGHT, (Bounds.Origin - ViewOrigin).SizeSquared() - FMath::Square(Bounds.SphereRadius));
				if (DistanceSquared < MaxDistanceSquared)
				{
					ScreenSize = ComputeBoundsScreenSize(Bounds.Origin, Bounds.SphereRadius, View);
				}

				bCanBeOccluder = GSOMinScreenRadiusForOccluder < ScreenSize;
			}
			
			if (bCanBeOccluder)
			{
				PotentialOccluders.push_back(FPotentialOccluderPrimitive());
				FPotentialOccluderPrimitive& PotentialOccluder = PotentialOccluders.back();

				PotentialOccluder.PrimitiveSceneInfo = PrimitiveSceneInfo;
				PotentialOccluder.Weight = ComputePotentialOccluderWeight(ScreenSize, DistanceSquared);
			}
			
			bool bCanBeOccludee = !bHasHugeBounds && (OcclusionFlags & EOcclusionFlags::CanBeOccluded) != 0;			
			if (bCanBeOccludee)
			{
				// Collect occludee bbox
				CollectOccludeeGeom(Bounds, PrimitiveComponentId, *SceneData);
				NumCollectedOccludees++;
			}
		}

		std::sort(PotentialOccluders.begin(), PotentialOccluders.end(), [&](const FPotentialOccluderPrimitive& A, const FPotentialOccluderPrimitive& B) {
			return A.Weight > B.Weight;
		});

		// Add sorted occluders to scene up to GSOMaxOccluderNum
		for (const FPotentialOccluderPrimitive& PotentialOccluder : PotentialOccluders)
		{
			const FPrimitiveComponentId PrimitiveComponentId = PotentialOccluder.PrimitiveSceneInfo->PrimitiveComponentId;
			FPrimitiveSceneProxy* Proxy = PotentialOccluder.PrimitiveSceneInfo->Proxy;

			if (Proxy->ShouldUseAsOccluder())
			{
				Collector.SetPrimitiveID(PrimitiveComponentId);
				// Collect occluder geometry
				NumCollectedOccluders+= Proxy->CollectOccluderElements(Collector);
			}

			if (NumCollectedOccluders >= GSOMaxOccluderNum)
			{
				break;
			}
		}
	}

	INC_DWORD_STAT_BY(STAT_SoftwareOccluders, NumCollectedOccluders);
	INC_DWORD_STAT_BY(STAT_SoftwareOccludees, NumCollectedOccludees);
	

	// reserve space for occludees vis flags 
	//ZB performance
	Results->VisibilityMap.clear();
	
	// Submit occlusion task
	FOcclusionSceneData* SceneDataParam = SceneData.release();

	//ZB todo  ���߳�
	ProcessOcclusionFrame(*SceneDataParam, *Results);	
}

int32 FSceneSoftwareOcclusion::Process(const FScene* Scene, FViewInfo& View)
{
	SubmitScene(Scene, View, Processing.get());
	return 0;
}

inline bool BinRowTestBit(uint64 Mask, int32 Bit)
{
	return (Mask & (1ull << Bit)) != 0;
}

