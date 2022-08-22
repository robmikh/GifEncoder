#pragma once
#include "IComposedFrameProvider.h"

struct ColorCount
{
	winrt::Windows::UI::Color Color;
	uint32_t Count;
};

class HistogramGenerator
{
public:
	HistogramGenerator(winrt::com_ptr<ID3D11Device> const& d3dDevice, uint32_t width, uint32_t height);

	std::tuple<std::vector<ColorCount>, winrt::com_ptr<ID3D11Buffer>> Generate(std::vector<ComposedFrame> const& frames);

private:
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	winrt::com_ptr<ID3D11Device> m_d3dDevice;
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;

	winrt::com_ptr<ID3D11Texture3D> m_colorTallyTexture;
	winrt::com_ptr<ID3D11UnorderedAccessView> m_colorTallyUav;
	winrt::com_ptr<ID3D11ShaderResourceView> m_colorTallySrv;

	// Color tally resources
	winrt::com_ptr<ID3D11Texture2D> m_inputTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> m_inputSrv;
	winrt::com_ptr<ID3D11Buffer> m_textureInfoBuffer;
	winrt::com_ptr<ID3D11ComputeShader> m_colorTallyShader;

	// Count resoruces
	winrt::com_ptr<ID3D11Buffer> m_colorCountBuffer;
	winrt::com_ptr<ID3D11Buffer> m_colorCountStagingBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> m_colorCountUav;
	winrt::com_ptr<ID3D11ComputeShader> m_countShader;

	// Accumulate resources
	winrt::com_ptr<ID3D11Buffer> m_accmumulateBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> m_accmumulateUav;
	winrt::com_ptr<ID3D11ComputeShader> m_accmumulateShader;
};
