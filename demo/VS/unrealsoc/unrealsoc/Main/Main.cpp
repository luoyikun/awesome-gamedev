#define _CRT_SECURE_NO_WARNINGS


#include"Library/Math/Box.h"
#include "SceneSoftwareOcclusion.h"

#include <graphics.h>//download https://easyx.cn/
#include <math.h>
#include <dos.h>
#include <conio.h>
#include<time.h>

//���ļ�����ı���
FSceneSoftwareOcclusion gSoc;

// ���դ��FrameBuffer��СΪ ��64 x 6 384 �ߣ�256
// | 64  |  64 | ... |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |
// |     |     |     |     |     |     |


inline bool BinRowTestBit(uint64 Mask, int32 Bit)
{
    return (Mask & (1ull << Bit)) != 0;
}


// �������դ��mask�Ľ��
void DebugDrawSocView()
{
    // ��ȡresult
    FOcclusionFrameResults* Results = gSoc.Processing.get();

    //������Ļ������ɫ��ɫ
    setbkcolor(0x595959);
    cleardevice();

    //6��Ͱ
    for (int32 i = 0; i < BIN_NUM; ++i)
    {
        //ÿ��Ͱ��ʼ����X Y
        int32 BinStartX = i * BIN_WIDTH;
        int32 BinStartY = 0;

        //ÿ��Ͱ��ÿһ��
        const FFramebufferBin& Bin = Results->Bins[i];
        for (int32 j = 0; j < FRAMEBUFFER_HEIGHT; ++j)
        {
            //���磺RowData�Ķ�������  00111000 ��64bit��Ϊ8bit�� ��ô����Ĵ���������� 00 111 000����������
            uint64 RowData = Bin.Data[j];

            int32 BitY = (FRAMEBUFFER_HEIGHT) - j; // flip image by Y axis
            //pos0�ȳ�ʼ��Ϊ��ʼλ��
            auto Pos0 = FVector(BinStartX, BitY, 0.f);
            //Bit0��ֵ�Ƕ���
            int32 Bit0 = BinRowTestBit(RowData, 0) ? 1 : 0;

            for (int32 k = 1; k < BIN_WIDTH; ++k)
            {
                //��һ��bit��0����1 
                int32 Bit1 = BinRowTestBit(RowData, k) ? 1 : 0;
                //��֮ǰ��bit��һ�� �γ�һ����
                if (Bit0 != Bit1 || (k == (BIN_WIDTH - 1)))
                {
                    //����εĽ���λ��
                    int32 BitX = BinStartX + k;
                    auto Pos1 = FVector(BitX, BitY, 0.f);
                    //����bit0������ɫ 0�ǻ�ɫ 1�ǰ�ɫ
                    if (Bit0 == 0)
                    {
                        setcolor(0x595959);
                    }
                    else
                    {
                        setcolor(WHITE);
                    }
                    //���������
                    line(Pos0.X, Pos0.Y, Pos1.X, Pos1.Y);
                    //��һ����
                    Pos0 = Pos1;
                    Bit0 = Bit1;
                }
            }
        }
        //���ƴ�ֱ�������ɫ���� ����Ͱ
        setcolor(BLUE);
        line(BinStartX, BinStartY, BinStartX, BinStartY + FRAMEBUFFER_HEIGHT);
    }
    //���ƴ�ֱ�������ɫ���� ����Ͱ
    line(BIN_NUM * BIN_WIDTH, 0, BIN_NUM * BIN_WIDTH, FRAMEBUFFER_HEIGHT);
}


int main()
{
    //��ʼ�� ������ͷ �������
    FScene scene;
    auto cube = new FPrimitiveSceneInfo();
    cube->PrimitiveComponentId.PrimIDValue = 0;
    cube->Proxy = new FOccluderSceneProxy();
    scene.Primitives.push_back(cube);

    FBoxSphereBounds cubeSB;
    cubeSB.Origin = FVector(0, 0, 0);
    cubeSB.BoxExtent = FVector(50, 50, 50);
    cubeSB.SphereRadius = 87;

    scene.PrimitiveOcclusionBounds.push_back(cubeSB);
    scene.PrimitiveOcclusionFlags.push_back(EOcclusionFlags::CanBeOccluded);

    FViewInfo viewInfo;
    viewInfo.PrimitiveVisibilityMap.push_back(0);
    static const float FOVRad = 90.0f * PI / 360.0f;
    static const FMatrix ProjectionMatrix = FPerspectiveMatrix(FOVRad, 384, 256, 0.01f);
    viewInfo.ViewMatrices.ProjectionMatrix = FMatrix(
        FPlane(1.0f, 0.0f, 0.0f, 0.0f),
        FPlane(0.0f, 1.5f, 0.0f, 0.0f),
        FPlane(0.0f, 0.0f, 0.0f, 1.0f),
        FPlane(0.0f, 0.0f, 10.0f, 0.0f)
    );

    const auto ViewRotationMatrix = FMatrix(
        FPlane(1.0f, 0.0f, 0.0f, 0.0f),
        FPlane(0.0f, 1.0f, 0.0f, 0.0f),
        FPlane(0.0f, 0.0f, 1.0f, 0.0f),
        FPlane(0.0f, 0.0f, 0.0f, 1.0f)
    );
    auto ViewLocation = FVector(-400, 0, 0);
    FMatrix ViewMatrix = FTranslationMatrix(-ViewLocation);
    viewInfo.ViewMatrices.ViewOrigin = ViewLocation;
    viewInfo.ViewMatrices.ViewMatrix = FMatrix(
        FPlane(0.0f, 0.0f, 1.0f, 0.0f),
        FPlane(1.0f, 0.0f, 0.0f, 0.0f),
        FPlane(0.0f, 1.0f, 0.0f, 0.0f),
        FPlane(0.0f, 0.0f, 400.0f, 1.0f)
    );

    gSoc.Process(&scene, viewInfo);

    initgraph(BIN_NUM * BIN_WIDTH + 1, FRAMEBUFFER_HEIGHT);
    DebugDrawSocView();
    while (!_kbhit())
    {
    }
    _getch();
    closegraph();
}
