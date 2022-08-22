#include "ColorHelpers.hlsli"

cbuffer ColorsInfo : register(b0)
{
    uint ColorsCount;
};

struct ColorTally
{
	uint3 Color;
	uint Count;
};

StructuredBuffer<ColorTally> colorTallies : register(t0);
RWTexture1D<float4> labColorBuffer : register(u0);

[numthreads(32, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	uint index = DTid.x;

	if (index < ColorsCount)
	{
		uint3 rgbColor = colorTallies[index].Color;
		float3 labColor = rgb2lab(rgbColor);
		labColorBuffer[index] = float4(labColor.x, labColor.y, labColor.z, 1.0f);
	}
}