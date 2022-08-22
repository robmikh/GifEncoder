#include "ColorHelpers.hlsli"

cbuffer ColorsInfo : register(b0)
{
    uint ColorsCount;
}; 

Texture1D<float4> labColorBuffer : register(t0);
RWTexture2D<float> distancesBuffer : register(u0);

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 position = DTid.xy;

    if (position.x < ColorsCount && position.y < ColorsCount)
    {
        uint row = position.y;
        uint distanceFrom = position.x;

        float3 thisColor = labColorBuffer[row];
        float3 otherColor = labColorBuffer[distanceFrom];

        float distance = computeDistance(thisColor, otherColor);
        distancesBuffer[position] = distance;
    }
}