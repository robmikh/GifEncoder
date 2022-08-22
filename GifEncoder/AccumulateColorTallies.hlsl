Texture3D<uint> colorTallyTexture : register(t0);

struct ColorTally
{
	uint3 Color;
	uint Count;
};

AppendStructuredBuffer<ColorTally> colorTallies : register(u0);

[numthreads(8, 8, 8)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	uint3 currentColor = DTid.xyz;

	uint count = colorTallyTexture[currentColor];
	if (count > 0)
	{
		ColorTally tally = { currentColor, count };
		colorTallies.Append(tally);
	}
}