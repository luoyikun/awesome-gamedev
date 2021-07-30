// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include"Library/UEAdapter.h"
#include"Library/Math/Matrix.h"
#include"Library/Math/Vector.h"
#include"Library/Math/BoxSphereBounds.h"

#include <memory>

/*=============================================================================
SceneSoftwareOcclusion.h
=============================================================================*/

//=====================================ȫ�ֱ���========================================
//��������������޳�skybox �ڵ���İ�Χ�а༶̫���� ����һ��skybox  ���������Ϊ�ڵ���
#define HALF_WORLD_MAX				(2097152.0 * 0.5)		/* �����С�İ뾶 */
//�ڵ���İ�Χ��ռ����Ļ����Сֵ С����������Ϊ�ڵ��� 
static float GSOMinScreenRadiusForOccluder = 0.075f;
//�ڵ�����Զ���� ����������벻����Ϊ�ڵ���
static float GSOMaxDistanceForOccluder = 20000.f;
//�ڵ���������� �����������������Ϊ�ڵ��� �������һ��Ȩ������
static int32 GSOMaxOccluderNum = 150;
//�Ƿ�ʹ��SIMD�Ż�
static int32 GSOSIMD = 1;


// ƽ�ƾ���ת�� ����vector�ĵ��任����
class FTranslationMatrix
	: public FMatrix
{
public:
	FTranslationMatrix(const FVector& Delta)
		: FMatrix(
			FPlane(1.0f,	0.0f,	0.0f,	0.0f),
			FPlane(0.0f,	1.0f,	0.0f,	0.0f),
			FPlane(0.0f,	0.0f,	1.0f,	0.0f),
			FPlane(Delta.X,	Delta.Y,Delta.Z,1.0f)
		)
	{ }
	/** Matrix factory. Return an FMatrix so we don't have type conversion issues in expressions. */
	static FMatrix Make(FVector const& Delta)
	{
		return FTranslationMatrix(Delta);
	}
};
// ͶӰ���� ����FOV ��� minZ �õ�ͶӰ����
class FPerspectiveMatrix
	: public FMatrix
{
public:
#define Z_PRECISION	0.0f
	FPerspectiveMatrix(float HalfFOV, float Width, float Height, float MinZ):
	FMatrix(
		FPlane(1.0f / FMath::Tan(HalfFOV),	0.0f,									0.0f,							0.0f),
		FPlane(0.0f,						Width / FMath::Tan(HalfFOV) / Height,	0.0f,							0.0f),
		FPlane(0.0f,						0.0f,									(1.0f - Z_PRECISION),			1.0f),
		FPlane(0.0f,						0.0f,									-MinZ * (1.0f - Z_PRECISION),	0.0f)
		)
	{}
};
// �����洢�ڵ���Ķ��� �� ��������
typedef TArray<FVector> FOccluderVertexArray;
typedef TArray<uint16> FOccluderIndexArray;
//ZB todo perf �����ֵ���� ��Ҫ�ĳ�ָ���������
typedef FOccluderVertexArray FOccluderVertexArraySP;
typedef FOccluderIndexArray FOccluderIndexArraySP;

// �ڵ����
namespace EOcclusionFlags
{
	enum Type
	{
		/** No flags. */
		None = 0x0,
		/** Indicates the primitive can be occluded. */
		CanBeOccluded = 0x1,
	};
}
// ��Ļ���㱻�ü��ı��
namespace EScreenVertexFlags
{
	const uint8 None = 0;
	const uint8 ClippedLeft		= 1 << 0;	// Vertex is clipped by left plane
	const uint8 ClippedRight	= 1 << 1;	// Vertex is clipped by right plane
	const uint8 ClippedTop		= 1 << 2;	// Vertex is clipped by top plane
	const uint8 ClippedBottom	= 1 << 3;	// Vertex is clipped by bottom plane
	const uint8 ClippedNear		= 1 << 4;   // Vertex is clipped by near plane
	const uint8 Discard			= 1 << 5;	// Polygon using this vertex should be discarded
}

// ���ID
class FPrimitiveComponentId
{
public:

	FPrimitiveComponentId() : PrimIDValue(0)
	{}

	inline bool IsValid() const
	{
		return PrimIDValue > 0;
	}

	inline bool operator==(FPrimitiveComponentId OtherId) const
	{
		return PrimIDValue == OtherId.PrimIDValue;
	}

	bool operator<(const FPrimitiveComponentId& other)  const
	{
		return PrimIDValue < other.PrimIDValue;
	}

	friend uint32 GetTypeHash(FPrimitiveComponentId Id)
	{
		return Id.PrimIDValue; //ZB Modify
	}

	uint32 PrimIDValue;
};

// ��Ļ����
struct FScreenPosition
{
	int32 X, Y;
};

// ��Ļ������
struct FScreenTriangle
{
	FScreenPosition V[3];
};

// �ڵ�mesh����
struct FOcclusionMeshData
{
	FMatrix					LocalToWorld;
	FOccluderVertexArraySP	VerticesSP;
	FOccluderIndexArraySP	IndicesSP;
	FPrimitiveComponentId	PrimId;
};




// SOC FramBuffer���  ��6 x 64 = 384 �߶ȣ�256
// | 64  |  64 | ... |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
static const int32 BIN_WIDTH = 64;
static const int32 BIN_NUM = 6;
static const int32 FRAMEBUFFER_WIDTH = BIN_WIDTH * BIN_NUM;
static const int32 FRAMEBUFFER_HEIGHT = 256;

//===================Input������ �ڵ���ͱ��ڵ���===============
// �ڵ��� mesh�ռ���
class FOccluderElementsCollector
{
public:
	virtual ~FOccluderElementsCollector() {};
	virtual void AddElements(const FOccluderVertexArraySP& Vertices, const FOccluderIndexArraySP& Indices, const FMatrix& LocalToWorld)
	{}
};
// �ڵ���������
struct FOcclusionSceneData
{
	FMatrix							ViewProj;
	TArray<FVector>					OccludeeBoxMinMax;
	TArray<FPrimitiveComponentId>	OccludeeBoxPrimId;
	TArray<FOcclusionMeshData>		OccluderData;
	int32							NumOccluderTriangles;
};
class FSWOccluderElementsCollector : public FOccluderElementsCollector
{
public:
	FSWOccluderElementsCollector(FOcclusionSceneData& InData)
		: SceneData(InData)
	{
		SceneData.NumOccluderTriangles = 0;
	}
	
	void SetPrimitiveID(FPrimitiveComponentId PrimitiveId)
	{
		CurrentPrimitiveId = PrimitiveId;
	}

	virtual void AddElements(const FOccluderVertexArraySP& Vertices, const FOccluderIndexArraySP& Indices, const FMatrix& LocalToWorld) override
	{
		SceneData.OccluderData.push_back(FOcclusionMeshData());
		FOcclusionMeshData& MeshData = SceneData.OccluderData.back();

		MeshData.PrimId = CurrentPrimitiveId;
		MeshData.LocalToWorld = LocalToWorld;
		MeshData.VerticesSP = Vertices;
		MeshData.IndicesSP = Indices;

		SceneData.NumOccluderTriangles+= Indices.size()/3;
	}

public:
	FOcclusionSceneData& SceneData;
	FPrimitiveComponentId CurrentPrimitiveId;
};

// �������Ļ���
class FPrimitiveSceneProxy
{
public:
	virtual bool ShouldUseAsOccluder()
	{
		return true;
	}
	//���������
	virtual int CollectOccluderElements(FOccluderElementsCollector& Collector)
	{
		return 1;
	}

};

// �ڵ��������
class FOccluderSceneProxy : public FPrimitiveSceneProxy
{
public:
	bool ShouldUseAsOccluder()
	{
		return true;
	}

	int CollectOccluderElements(FOccluderElementsCollector& Collector)
	{
		//���Դ���
		FOccluderVertexArraySP VerticesSP;
		VerticesSP.push_back(FVector(0, 50, 50));
		VerticesSP.push_back(FVector(0, 50, 0));
		VerticesSP.push_back(FVector(0, 0, 0));
		VerticesSP.push_back(FVector(0, 0, 50));

		VerticesSP.push_back(FVector(0, 150, 150));
		VerticesSP.push_back(FVector(0, 150, 100));
		VerticesSP.push_back(FVector(0, 100, 100));
		VerticesSP.push_back(FVector(0, 100, 150));

		FOccluderIndexArraySP IndicesSP;
		IndicesSP.push_back(2);
		IndicesSP.push_back(1);
		IndicesSP.push_back(0);
		
		IndicesSP.push_back(0);
		IndicesSP.push_back(3);
		IndicesSP.push_back(2);

		// IndicesSP.push_back(6);
		// IndicesSP.push_back(5);
		// IndicesSP.push_back(4);
		
		Collector.AddElements(VerticesSP, IndicesSP, FMatrix::GetIdentity());
		return 1;
	}
};

// ���ڵ������
class FOccludeeSceneProxy : public FPrimitiveSceneProxy
{
public:
	bool ShouldUseAsOccluder()
	{
		return false;
	}
	int CollectOccluderElements(FOccluderElementsCollector& Collector)
	{
		return 0;
	}
};

//���������Ϣ 
class FPrimitiveSceneInfo
{
public:
	FPrimitiveSceneProxy* Proxy;
	FPrimitiveComponentId PrimitiveComponentId;
};
//===================Input������ �ڵ���ͱ��ڵ���===============


struct FPotentialOccluderPrimitive
{
	const FPrimitiveSceneInfo* PrimitiveSceneInfo;
	float Weight;
};



//===================Output������ �ڵ���ͱ��ڵ���===============
//һ��Ͱ����Ļ��ƽ�� ���� ���ڵ���ޚmask buffer
struct FFramebufferBin
{
	uint64 Data[FRAMEBUFFER_HEIGHT];
};

//�޳����
struct FOcclusionFrameResults
{

	FFramebufferBin	Bins[BIN_NUM];
	TMap<FPrimitiveComponentId, bool> VisibilityMap;
};
//===================Output������ �ڵ���ͱ��ڵ���===============

// View ���� 
class FViewMatrices
{
public:
	inline  FViewMatrices()
	{
		ProjectionMatrix.SetIdentity();
		ViewMatrix.SetIdentity();
		ViewOrigin = FVector(0, 0, 0);
	}
	
	FMatrix		ProjectionMatrix;		//ͶӰ����
	FMatrix		ViewMatrix;				//View���� ��������ת����ռ�ľ���
	FVector		ViewOrigin;				//���λ��

};
//View����Ϣ �����Ϣ
class FSceneView
{
public:
	FViewMatrices ViewMatrices;
};

//View����Ϣ �����Ϣ
class FViewInfo : public FSceneView
{
public:
	TArray<int32> PrimitiveVisibilityMap;
};

//������Ϣ 
class FScene
{
public:
	/** Packed array of primitives in the scene. */
	TArray<FPrimitiveSceneInfo*> Primitives;
	/** Packed array of primitive occlusion bounds. */
	TArray<FBoxSphereBounds> PrimitiveOcclusionBounds;
	/** Packed array of primitive occlusion flags. See EOcclusionFlags. */
	TArray<uint8> PrimitiveOcclusionFlags;
};

// �����ε���������� ��������ʹ��
struct FSortedIndexDepth
{
	int32 Index;
	float Depth;
};

// �ڵ�����
struct FOcclusionFrameData
{
	// binned tris
	TArray<FSortedIndexDepth>		SortedTriangles[BIN_NUM];
	
	// tris data	
	TArray<FScreenTriangle>			ScreenTriangles;
	TArray<FPrimitiveComponentId>	ScreenTrianglesPrimID;
	TArray<uint8>					ScreenTrianglesFlags;

	void ReserveBuffers(int32 NumTriangles)
	{
		const int32 NumTrianglesPerBin = NumTriangles/BIN_NUM + 1;
		for (int32 BinIdx = 0; BinIdx < BIN_NUM; ++BinIdx)
		{
			SortedTriangles[BinIdx].reserve(NumTrianglesPerBin);
		}
				
		ScreenTriangles.reserve(NumTriangles);
		ScreenTrianglesPrimID.reserve(NumTriangles);
		ScreenTrianglesFlags.reserve(NumTriangles);
	}
};



//�����ڵ��޳���
class FSceneSoftwareOcclusion
{
public:
	FSceneSoftwareOcclusion();
	~FSceneSoftwareOcclusion();
	int32 Process(const FScene* Scene, FViewInfo& View);
	std::unique_ptr<FOcclusionFrameResults> Processing;
};