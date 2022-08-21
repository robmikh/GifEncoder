Texture2D frameTexture : register(t0);
SamplerState frameTextureSampler : register(s0);

Texture3D<uint> lutTexture : register(t1);

cbuffer FrameInfo : register(b0)
{
    int TransparentColorIndex;
}

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

uint main(PS_INPUT input) : SV_TARGET
{
    float4 unormColor = frameTexture.Sample(frameTextureSampler, input.texCoord);
    uint paletteIndex = 0;
    if (unormColor.w < 0.1f)
    {
        uint3 color = { (uint)(unormColor.x * 255.0f), (uint)(unormColor.y * 255.0f), (uint)(unormColor.z * 255.0f) };
        paletteIndex = lutTexture[color.xyz];
    }
    else
    {
        paletteIndex = TransparentColorIndex;
    }
    return paletteIndex;
}
