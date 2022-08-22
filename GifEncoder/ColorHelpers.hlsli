float computeRgbChannel(uint channel)
{
    float result = ((float)channel) / 255.0f;
    if (result > 0.04045f)
    {
        result = pow((result + 0.055f) / 1.055f, 2.4f);
    }
    else
    {
        result = result / 12.92f;
    }
    return 100.0f * result;
}

float computeXyzChannel(float channel)
{
    if (channel > 0.008856f)
    {
        channel = pow(channel, 1.0f / 3.0f);
    }
    else
    {
        channel = (7.787f * channel) + (16.0f / 116.0f);
    }
    return channel;
}

float3 rgb2lab(uint3 colorRGB)
{
    float r = computeRgbChannel(colorRGB.x);
    float g = computeRgbChannel(colorRGB.y);
    float b = computeRgbChannel(colorRGB.z);

    float x = r * 0.4124f + g * 0.3576f + b * 0.1805f;
    float y = r * 0.2126f + g * 0.7152f + b * 0.0722f;
    float z = r * 0.0193f + g * 0.1192f + b * 0.9505f;

    // Observer= 2°, Illuminant= D65
    x = computeXyzChannel(x / 95.0470f);
    y = computeXyzChannel(y / 100.0f);
    z = computeXyzChannel(z / 108.883f);

    float3 result =
    {
        round((116.0f * y) - 16.0f), // L
        round(500.0f * (x - y)),  // a
        round(200.0f * (y - z)),  // b
    };
    return result;
}

float computeDistance(float3 point1, float3 point2)
{
    float result = pow(point1.x - point2.x, 2.0f) + pow(point1.y - point2.y, 2.0f) + pow(point1.z - point2.z, 2.0f);
    result = sqrt(result);
    return result;
}