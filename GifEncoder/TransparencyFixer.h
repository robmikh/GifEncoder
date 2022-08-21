#pragma once

struct DiffInfo
{
	uint32_t NumDifferingPixels;
	uint32_t left;
	uint32_t top;
	uint32_t right;
	uint32_t bottom;
};

class TransparencyFixer
{
public:
	TransparencyFixer(
		winrt::com_ptr<ID3D11Device> const& d3dDevice,
		winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
		uint32_t width,
		uint32_t height);

	void InitPrevious(winrt::com_ptr<ID3D11Texture2D> const& previousTexture);
	DiffInfo ProcessInput(winrt::com_ptr<ID3D11Texture2D> const& inputTexture, int transparentColorIndex, std::vector<uint8_t>& indexPixels);

private:
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
	winrt::com_ptr<ID3D11Texture2D> m_currentTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> m_currentSrv;
	winrt::com_ptr<ID3D11Texture2D> m_outputTexture;
	winrt::com_ptr<ID3D11UnorderedAccessView> m_outputUav;
	winrt::com_ptr<ID3D11Buffer> m_frameInfoBuffer;
	winrt::com_ptr<ID3D11Buffer> m_frameInfoStagingBuffer;
	winrt::com_ptr<ID3D11ComputeShader> m_shader;
	winrt::com_ptr<ID3D11Texture2D> m_stagingTexture;
	winrt::com_ptr<ID3D11Texture2D> m_previousTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> m_previousSrv;
	winrt::com_ptr<ID3D11Buffer> m_diffInfoBuffer;
	winrt::com_ptr<ID3D11Buffer> m_diffInfoStagingBuffer;
	winrt::com_ptr<ID3D11Buffer> m_diffInfoDefaultBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> m_diffInfoUav;
};