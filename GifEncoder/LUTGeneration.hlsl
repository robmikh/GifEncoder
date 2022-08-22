#include "ColorHelpers.hlsli"

cbuffer PaletteInfo : register(b0)
{
    uint PaletteLength;
};

Texture1D<uint4> paletteTexture : register(t0);
RWTexture3D<uint> outputTexture : register(u0);

uint3 getPaletteColor(uint index)
{
    uint4 color = paletteTexture[index];
    return color.xyz;
}

[numthreads(8, 8, 8)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Determine the palette size
    uint paletteColors = PaletteLength;

    // Extract color from the current texel position
    // TODO: This should be zyx, as we're moving between BGR and RGB...
    //uint3 currentColor = DTid.zyx;
    uint3 currentColor = DTid.xyz;
    // Convert extracted color to CIELAB space
    float3 currentColorLab = rgb2lab(currentColor);
    // Compute the min difference between the extracted color and each color in the palette
    float minDistance = -1.0f; // TODO: Float max?
    uint closestColorIndex = 0;
    for (uint i = 0; i < paletteColors; i++)
    {
        // Get the color from the palette
        uint3 paletteColor = getPaletteColor(i);
        // Convert the palette color to CIELAB space
        float3 paletteColorLab = rgb2lab(paletteColor);
        // Compute distance
        float distance = computeDistance(currentColorLab, paletteColorLab);
        if (minDistance < 0.0f || distance < minDistance)
        {
            minDistance = distance;
            closestColorIndex = i;
        }
        if (distance == 0.0f)
        {
            break;
        }
    }

    outputTexture[DTid.xyz] = closestColorIndex;
}