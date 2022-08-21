#pragma once

class ColorQuantizer
{
public:
	ColorQuantizer(
		winrt::com_ptr<ID3D11Device> const& d3dDevice,
		winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
		uint32_t width,
		uint32_t height);

	void Quantize(
		winrt::com_ptr<ID3D11Texture2D> const& texture,
		winrt::com_ptr<ID3D11ShaderResourceView> const& lutSrv,
		int transparentColorIndex,
		std::vector<uint8_t>& bytes);

private:
	winrt::com_ptr<ID3D11Texture2D> m_inputTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> m_inputSrv;
	winrt::com_ptr<ID3D11SamplerState> m_inputSampler;
	winrt::com_ptr<ID3D11Texture2D> m_outputTexture;
	winrt::com_ptr<ID3D11RenderTargetView> m_outputRtv;
	winrt::com_ptr<ID3D11Buffer> m_vertexBuffer;
	winrt::com_ptr<ID3D11Buffer> m_indexBuffer;
	winrt::com_ptr<ID3D11Texture2D> m_stagingTexture;
	winrt::com_ptr<ID3D11Buffer> m_frameInfoBuffer;
	winrt::com_ptr<ID3D11Buffer> m_frameInfoStagingBuffer;
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
	winrt::com_ptr<ID3D11VertexShader> m_vertexShader;
	winrt::com_ptr<ID3D11PixelShader> m_pixelShader;
};
