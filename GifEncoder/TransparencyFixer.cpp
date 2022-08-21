#include "pch.h"
#include "TransparencyFixer.h"
#include "FixTransparencyShader.h"

namespace util
{
	using namespace robmikh::common::uwp;
}

struct FrameInfo
{
	uint32_t Width;
	uint32_t Height;
	int TransparentColorIndex;
};

TransparencyFixer::TransparencyFixer(
	winrt::com_ptr<ID3D11Device> const& d3dDevice, 
	winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
	uint32_t width,
	uint32_t height)
{
	m_d3dContext = d3dContext;

	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_inputTexture.put()));
	}
	winrt::check_hresult(d3dDevice->CreateShaderResourceView(m_inputTexture.get(), nullptr, m_inputSrv.put()));

	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8_UINT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_outputTexture.put()));

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.MiscFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_stagingTexture.put()));

	}
	winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(m_outputTexture.get(), nullptr, m_outputUav.put()));

	{
		auto paddedSize = sizeof(FrameInfo);
		if (paddedSize < 16)
		{
			paddedSize = 16;
		}
		else
		{
			auto remainder = paddedSize % 16;
			paddedSize = paddedSize + remainder;
		}
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = paddedSize;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, nullptr, m_frameInfoBuffer.put()));

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, nullptr, m_frameInfoStagingBuffer.put()));
	}

	winrt::check_hresult(d3dDevice->CreateComputeShader(g_main, ARRAYSIZE(g_main), nullptr, m_shader.put()));

}

std::vector<uint8_t> TransparencyFixer::ProcessInput(winrt::com_ptr<ID3D11Texture2D> const& texture, int transparentColorIndex, std::vector<uint8_t>& indexPixels)
{
	D3D11_TEXTURE2D_DESC desc = {};
	texture->GetDesc(&desc);
	WINRT_VERIFY(indexPixels.size() == desc.Width * desc.Height);

	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(m_frameInfoStagingBuffer.get(), 0, D3D11_MAP_WRITE, 0, &mapped));
		FrameInfo info = {};
		info.TransparentColorIndex = transparentColorIndex;
		memcpy_s(mapped.pData, sizeof(FrameInfo), reinterpret_cast<void*>(&info), sizeof(FrameInfo));
		m_d3dContext->Unmap(m_frameInfoStagingBuffer.get(), 0);
	}
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(m_stagingTexture.get(), 0, D3D11_MAP_WRITE, 0, &mapped));
		auto stride = desc.Width;
		auto dest = reinterpret_cast<byte*>(mapped.pData);
		auto source = indexPixels.data();
		for (auto i = 0; i < (int)desc.Height; i++)
		{
			memcpy(dest, source, stride);

			dest += mapped.RowPitch;
			source += stride;
		}
		m_d3dContext->Unmap(m_stagingTexture.get(), 0);
	}
	m_d3dContext->CopyResource(m_frameInfoBuffer.get(), m_frameInfoStagingBuffer.get());
	m_d3dContext->CopyResource(m_inputTexture.get(), texture.get());
	m_d3dContext->CopyResource(m_outputTexture.get(), m_stagingTexture.get());

	m_d3dContext->CSSetShader(m_shader.get(), nullptr, 0);
	std::vector<ID3D11ShaderResourceView*> srvs = { m_inputSrv.get() };
	m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	std::vector<ID3D11Buffer*> constants = { m_frameInfoBuffer.get() };
	m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	std::vector<ID3D11UnorderedAccessView*> uavs = { m_outputUav.get() };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);
	m_d3dContext->Dispatch(256 / 8, 256 / 8, 1);

	m_d3dContext->CopyResource(m_stagingTexture.get(), m_outputTexture.get());
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(m_stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));

		auto stride = desc.Width;
		auto source = reinterpret_cast<byte*>(mapped.pData);
		auto dest = indexPixels.data();
		for (auto i = 0; i < (int)desc.Height; i++)
		{
			memcpy(dest, source, stride);

			source += mapped.RowPitch;
			dest += stride;
		}
		m_d3dContext->Unmap(m_stagingTexture.get(), 0);
	}

	m_d3dContext->CSSetShader(nullptr, nullptr, 0);
	srvs = { nullptr };
	m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	constants = { nullptr };
	m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	uavs = { nullptr };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

	return util::CopyBytesFromTexture(m_outputTexture);
}
