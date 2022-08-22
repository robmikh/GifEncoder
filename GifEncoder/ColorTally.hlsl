cbuffer TextureInfo : register(b0)
{
    uint Width;
    uint Height;
};

Texture2D<unorm float4> inputTexture : register(t0);
RWTexture3D<uint> outputTexture : register(u0);

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 position = DTid.xy;

    if (position.x < Width && position.y < Height)
    {
        float4 pixel = inputTexture[position];
        uint3 color = { (uint)(pixel.x * 255.0f), (uint)(pixel.y * 255.0f), (uint)(pixel.z * 255.0f) };

        uint value = 0;
        InterlockedAdd(outputTexture[color.xyz], 1, value);
    }
}