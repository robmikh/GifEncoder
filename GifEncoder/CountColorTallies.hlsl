Texture3D<uint> colorTallyTexture : register(t0);

struct ColorCount
{
	uint Count;
};

RWStructuredBuffer<ColorCount> colorCountBuffer : register(u0);

[numthreads(8, 8, 8)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint3 currentColor = DTid.xyz;

	uint count = colorTallyTexture[currentColor];
	if (count > 0)
	{
		uint value = 0;
		InterlockedAdd(colorCountBuffer[0].Count, 1, value);
	}
}