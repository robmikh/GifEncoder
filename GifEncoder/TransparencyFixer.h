#pragma once

class TransparencyFixer
{
public:
	TransparencyFixer(
		winrt::com_ptr<ID3D11Device> const& d3dDevice,
		winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
		uint32_t width,
		uint32_t height);

	std::vector<uint8_t> ProcessInput(winrt::com_ptr<ID3D11Texture2D> const& inputTexture, int transparentColorIndex, std::vector<uint8_t>& indexPixels);

private:
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
	winrt::com_ptr<ID3D11Texture2D> m_inputTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> m_inputSrv;
	winrt::com_ptr<ID3D11Texture2D> m_outputTexture;
	winrt::com_ptr<ID3D11UnorderedAccessView> m_outputUav;
	winrt::com_ptr<ID3D11Buffer> m_frameInfoBuffer;
	winrt::com_ptr<ID3D11Buffer> m_frameInfoStagingBuffer;
	winrt::com_ptr<ID3D11ComputeShader> m_shader;
	winrt::com_ptr<ID3D11Texture2D> m_stagingTexture;
};