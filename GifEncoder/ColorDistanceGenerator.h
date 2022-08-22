#pragma once
#include "HistogramGenerator.h"

class ColorDistanceGenerator
{
public:
	static std::vector<std::vector<float>> Generate(
		winrt::com_ptr<ID3D11Device> const& d3dDevice,
		winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
		winrt::com_ptr<ID3D11Buffer> const& accumulatedColorsBuffer,
		size_t numColors);

private:
	ColorDistanceGenerator();
};