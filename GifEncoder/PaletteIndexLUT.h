#pragma once

class PaletteIndexLUT
{
public:
	PaletteIndexLUT(
		winrt::com_ptr<ID3D11Device> const& d3dDevice,
		winrt::com_ptr<ID3D11DeviceContext> const& d3dContext);

	void Generate(std::vector<winrt::Windows::UI::Color> const& palette);
	winrt::com_ptr<ID3D11ShaderResourceView> const& Srv() { return m_lutSrv; }

private:
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
	winrt::com_ptr<ID3D11Texture3D> m_lutTexture;
	winrt::com_ptr<ID3D11Texture1D> m_paletteTexture;
	winrt::com_ptr<ID3D11Texture1D> m_paletteStagingTexture;
	winrt::com_ptr<ID3D11Buffer> m_paletteInfoBuffer;
	winrt::com_ptr<ID3D11Buffer> m_paletteInfoStagingBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> m_lutUav;
	winrt::com_ptr<ID3D11ShaderResourceView> m_lutSrv;
	winrt::com_ptr<ID3D11ShaderResourceView> m_paletteSrv;
	winrt::com_ptr<ID3D11ComputeShader> m_shader;
};