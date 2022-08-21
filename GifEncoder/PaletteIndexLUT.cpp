#include "pch.h"
#include "PaletteIndexLUT.h"
#include "LUTGenerationShader.h"

struct PaletteInfo
{
	uint32_t PaletteLength;
	int TransparnetColorIndex;
};

PaletteIndexLUT::PaletteIndexLUT(
	winrt::com_ptr<ID3D11Device> const& d3dDevice, 
	winrt::com_ptr<ID3D11DeviceContext> const& d3dContext)
{
	m_d3dContext = d3dContext;

	// Create our lut texture
	D3D11_TEXTURE3D_DESC lutDesc = {};
	lutDesc.Width = 256;
	lutDesc.Height = 256;
	lutDesc.Depth = 256;
	lutDesc.MipLevels = 1;
	lutDesc.Format = DXGI_FORMAT_R8_UINT;
	lutDesc.Usage = D3D11_USAGE_DEFAULT;
	lutDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	winrt::check_hresult(d3dDevice->CreateTexture3D(&lutDesc, nullptr, m_lutTexture.put()));
	winrt::check_hresult(d3dDevice->CreateShaderResourceView(m_lutTexture.get(), nullptr, m_lutSrv.put()));
	winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(m_lutTexture.get(), nullptr, m_lutUav.put()));
	
	// Create our palette texture
	D3D11_TEXTURE1D_DESC paletteDesc = {};
	paletteDesc.Width = 256;
	paletteDesc.MipLevels = 1;
	paletteDesc.ArraySize = 1;
	paletteDesc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
	paletteDesc.Usage = D3D11_USAGE_DEFAULT;
	paletteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	winrt::check_hresult(d3dDevice->CreateTexture1D(&paletteDesc, nullptr, m_paletteTexture.put()));
	winrt::check_hresult(d3dDevice->CreateShaderResourceView(m_paletteTexture.get(), nullptr, m_paletteSrv.put()));
	paletteDesc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
	paletteDesc.Usage = D3D11_USAGE_STAGING;
	paletteDesc.BindFlags = 0;
	paletteDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	winrt::check_hresult(d3dDevice->CreateTexture1D(&paletteDesc, nullptr, m_paletteStagingTexture.put()));

	// Create our palette info texture
	auto paletteInfoBufferPaddedSize = sizeof(PaletteInfo);
	if (paletteInfoBufferPaddedSize < 16)
	{
		paletteInfoBufferPaddedSize = 16;
	}
	else
	{
		auto remainder = paletteInfoBufferPaddedSize % 16;
		paletteInfoBufferPaddedSize = paletteInfoBufferPaddedSize + remainder;
	}
	D3D11_BUFFER_DESC paletteInfoDesc = {};
	paletteInfoDesc.ByteWidth = paletteInfoBufferPaddedSize;
	paletteInfoDesc.Usage = D3D11_USAGE_DEFAULT;
	paletteInfoDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	winrt::check_hresult(d3dDevice->CreateBuffer(&paletteInfoDesc, nullptr, m_paletteInfoBuffer.put()));

	paletteInfoDesc.Usage = D3D11_USAGE_STAGING;
	paletteInfoDesc.BindFlags = 0;
	paletteInfoDesc.MiscFlags = 0;
	paletteInfoDesc.StructureByteStride = 0;
	paletteInfoDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	winrt::check_hresult(d3dDevice->CreateBuffer(&paletteInfoDesc, nullptr, m_paletteInfoStagingBuffer.put()));

	winrt::check_hresult(d3dDevice->CreateComputeShader(g_main, ARRAYSIZE(g_main), nullptr, m_shader.put()));
}

void PaletteIndexLUT::Generate(std::vector<uint8_t> const& palette, int transparentColorIndex)
{
	WINRT_VERIFY(palette.size() <= 256 * 4);

	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(m_paletteStagingTexture.get(), 0, D3D11_MAP_WRITE, 0, &mapped));
		memset(mapped.pData, 0, 256);
		memcpy_s(mapped.pData, palette.size(), palette.data(), palette.size());
		m_d3dContext->Unmap(m_paletteStagingTexture.get(), 0);
	}
	m_d3dContext->CopyResource(m_paletteTexture.get(), m_paletteStagingTexture.get());

	{
		D3D11_BUFFER_DESC paletteInfoDesc = {};
		m_paletteInfoStagingBuffer->GetDesc(&paletteInfoDesc);
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(m_paletteInfoStagingBuffer.get(), 0, D3D11_MAP_WRITE, 0, &mapped));
		PaletteInfo info = {};
		info.PaletteLength = static_cast<uint32_t>(palette.size());
		info.TransparnetColorIndex = transparentColorIndex;
		memcpy_s(mapped.pData, sizeof(PaletteInfo), reinterpret_cast<void*>(&info), sizeof(PaletteInfo));
		m_d3dContext->Unmap(m_paletteInfoStagingBuffer.get(), 0);
	}
	m_d3dContext->CopyResource(m_paletteInfoBuffer.get(), m_paletteInfoStagingBuffer.get());

	m_d3dContext->CSSetShader(m_shader.get(), nullptr, 0);
	std::vector<ID3D11ShaderResourceView*> srvs = { m_paletteSrv.get() };
	m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	std::vector<ID3D11Buffer*> constants = { m_paletteInfoBuffer.get() };
	m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	std::vector<ID3D11UnorderedAccessView*> uavs = { m_lutUav.get() };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);
	m_d3dContext->Dispatch(256 / 8, 256 / 8, 256 / 8);

	m_d3dContext->CSSetShader(nullptr, nullptr, 0);
	srvs = { nullptr };
	m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	constants = { nullptr };
	m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	uavs = { nullptr };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);
}
