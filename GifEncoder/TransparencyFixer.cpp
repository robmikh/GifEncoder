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

uint32_t ComputePaddedBufferSize(size_t size) 
{
	auto paddedSize = size;
	if (paddedSize < 16)
	{
		paddedSize = 16;
	}
	else
	{
		auto remainder = paddedSize % 16;
		paddedSize = paddedSize + remainder;
	}
	return static_cast<uint32_t>(paddedSize);
}

template <typename T>
T ReadFromBuffer(
	winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
	winrt::com_ptr<ID3D11Buffer> const& stagingBuffer)
{
	D3D11_BUFFER_DESC desc = {};
	stagingBuffer->GetDesc(&desc);

	assert(sizeof(T) <= desc.ByteWidth);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	d3dContext->Map(stagingBuffer.get(), 0, D3D11_MAP_READ, 0, &mapped);

	T result = {};
	result = *reinterpret_cast<T*>(mapped.pData);

	d3dContext->Unmap(stagingBuffer.get(), 0);

	return result;
}

TransparencyFixer::TransparencyFixer(
	winrt::com_ptr<ID3D11Device> const& d3dDevice, 
	winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
	uint32_t width,
	uint32_t height)
{
	m_d3dContext = d3dContext;

	// Create current and previous textures
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
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_currentTexture.put()));
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_previousTexture.put()));
	}
	winrt::check_hresult(d3dDevice->CreateShaderResourceView(m_currentTexture.get(), nullptr, m_currentSrv.put()));
	winrt::check_hresult(d3dDevice->CreateShaderResourceView(m_previousTexture.get(), nullptr, m_previousSrv.put()));

	// Create output and staging textures
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

	// Create frame info buffers
	{
		auto paddedSize = ComputePaddedBufferSize(sizeof(FrameInfo));
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

	// Create diff info buffers
	{
		auto paddedSize = ComputePaddedBufferSize(sizeof(DiffInfo));
		paddedSize = sizeof(DiffInfo);
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = paddedSize;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = paddedSize;
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, nullptr, m_diffInfoBuffer.put()));

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDiff = {};
		uavDiff.Format = DXGI_FORMAT_UNKNOWN;
		uavDiff.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDiff.Buffer.NumElements = 1;
		winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(m_diffInfoBuffer.get(), &uavDiff, m_diffInfoUav.put()));

		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		DiffInfo initialInfo = {};
		initialInfo.NumDifferingPixels = 0;
		initialInfo.left = width;
		initialInfo.top = height;
		initialInfo.right = 0;
		initialInfo.bottom = 0;
		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = reinterpret_cast<void*>(&initialInfo);
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, &initData, m_diffInfoDefaultBuffer.put()));

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, nullptr, m_diffInfoStagingBuffer.put()));
	}

	winrt::check_hresult(d3dDevice->CreateComputeShader(g_main, ARRAYSIZE(g_main), nullptr, m_shader.put()));

}

void TransparencyFixer::InitPrevious(winrt::com_ptr<ID3D11Texture2D> const& previousTexture)
{
	m_d3dContext->CopyResource(m_previousTexture.get(), previousTexture.get());
}

DiffInfo TransparencyFixer::ProcessInput(winrt::com_ptr<ID3D11Texture2D> const& texture, int transparentColorIndex, std::vector<uint8_t>& indexPixels)
{
	D3D11_TEXTURE2D_DESC desc = {};
	texture->GetDesc(&desc);
	WINRT_VERIFY(indexPixels.size() == desc.Width * desc.Height);

	// Update our frame info buffer
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(m_frameInfoStagingBuffer.get(), 0, D3D11_MAP_WRITE, 0, &mapped));
		FrameInfo info = {};
		info.TransparentColorIndex = transparentColorIndex;
		info.Width = desc.Width;
		info.Height = desc.Height;
		memcpy_s(mapped.pData, sizeof(FrameInfo), reinterpret_cast<void*>(&info), sizeof(FrameInfo));
		m_d3dContext->Unmap(m_frameInfoStagingBuffer.get(), 0);
	}
	m_d3dContext->CopyResource(m_frameInfoBuffer.get(), m_frameInfoStagingBuffer.get());

	// Reset our diff info buffer
	m_d3dContext->CopyResource(m_diffInfoBuffer.get(), m_diffInfoDefaultBuffer.get());

	// Update our output texture
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
	m_d3dContext->CopyResource(m_outputTexture.get(), m_stagingTexture.get());

	// Update our current texture
	m_d3dContext->CopyResource(m_currentTexture.get(), texture.get());

	// Setup our pipeline
	m_d3dContext->CSSetShader(m_shader.get(), nullptr, 0);
	std::vector<ID3D11ShaderResourceView*> srvs = { m_currentSrv.get(), m_previousSrv.get()};
	m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	std::vector<ID3D11Buffer*> constants = { m_frameInfoBuffer.get() };
	m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	std::vector<ID3D11UnorderedAccessView*> uavs = { m_outputUav.get(), m_diffInfoUav.get()};
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

	// Run the compute shader
	m_d3dContext->Dispatch((desc.Width / 8) + 1, (desc.Height / 8) + 1, 1);

	// Copy our output to the staging texture and then to the provided buffer
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

	// Copy the diff info
	m_d3dContext->CopyResource(m_diffInfoStagingBuffer.get(), m_diffInfoBuffer.get());
	auto diffInfo = ReadFromBuffer<DiffInfo>(m_d3dContext, m_diffInfoStagingBuffer);

	// Copy current to previous
	m_d3dContext->CopyResource(m_previousTexture.get(), m_currentTexture.get());

	// Unbind pipeline
	m_d3dContext->CSSetShader(nullptr, nullptr, 0);
	srvs = { nullptr, nullptr };
	m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	constants = { nullptr };
	m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	uavs = { nullptr, nullptr };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

	return diffInfo;
}
