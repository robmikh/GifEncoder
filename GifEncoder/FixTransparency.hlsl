cbuffer FrameInfo : register(b0)
{
    uint Width;
    uint Height;
    int TransparentColorIndex;
}; 

struct DiffInfo
{
    uint NumDifferingPixels;
    uint left;
    uint top;
    uint right;
    uint bottom;
};

Texture2D<unorm float4> currentTexture : register(t0);
Texture2D<unorm float4> previousTexture : register(t1);
Texture2D<uint> previousIndexTexture : register(t2);
RWTexture2D<uint> outputTexture : register(u0);
RWStructuredBuffer<DiffInfo> diffBuffer : register(u1);

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 position = DTid.xy;

    if (position.x < Width && position.y < Height)
    {
        float4 currentPixel = currentTexture[position];
        float4 previousPixel = previousTexture[position];
        
        uint previousIndex = previousIndexTexture[position];
        uint index = outputTexture[position];

        bool sameIndex = previousIndex == index;
        bool samePixel = (currentPixel.x == previousPixel.x) &&
            (currentPixel.y == previousPixel.y) &&
            (currentPixel.z == previousPixel.z) &&
            (currentPixel.w == previousPixel.w);

        if (!samePixel && !sameIndex)
        {
            uint value = 0;
            InterlockedAdd(diffBuffer[0].NumDifferingPixels, 1, value);

            value = 0;
            InterlockedMin(diffBuffer[0].left, position.x, value);
            InterlockedMin(diffBuffer[0].top, position.y, value);
            InterlockedMax(diffBuffer[0].right, position.x, value);
            InterlockedMax(diffBuffer[0].bottom, position.y, value);
        }
        else
        {
            index = TransparentColorIndex;
        }

        if (currentPixel.w <= 0.1f)
        {
            index = TransparentColorIndex;
        }

        outputTexture[position] = index;
    }
}