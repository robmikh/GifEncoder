cbuffer FrameInfo : register(b0)
{
    uint Width;
    uint Height;
    int TransparentColorIndex;
}; 

Texture2D<unorm float4> inputTexture : register(t0);
RWTexture2D<uint> outputTexture : register(u0);

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 position = DTid.xy;

    if (position.x < Width && position.y < Height)
    {
        float4 pixel = inputTexture[position];

        if (pixel.w <= 0.1f)
        {
            outputTexture[position] = TransparentColorIndex;
        }
    }
}