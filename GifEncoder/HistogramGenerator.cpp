#include "pch.h"
#include "HistogramGenerator.h"

namespace shaders
{
	namespace tally
	{
#include "ColorTallyShader.h"
	}
	namespace count
	{
#include "CountColorTalliesShader.h"
	}
	namespace accumulate
	{
#include "AccumulateColorTalliesShader.h"
	}
}

namespace winrt
{
	using namespace Windows::UI;
}

struct TextureInfo
{
	uint32_t Width;
	uint32_t Height;
};

struct ShaderColorCount
{
	uint32_t Count;
};

struct ShaderColorTally
{
	uint32_t R;
	uint32_t G;
	uint32_t B;
	uint32_t Count;
};

float CLEARCOLOR[] = { 0.0f, 0.0f, 0.0f, 0.0f }; // RGBA

HistogramGenerator::HistogramGenerator(
	winrt::com_ptr<ID3D11Device> const& d3dDevice, 
	uint32_t width, 
	uint32_t height)
{
	m_d3dDevice = d3dDevice;
	d3dDevice->GetImmediateContext(m_d3dContext.put());
	m_width = width;
	m_height = height;

	// Create color tally texture
	{
		D3D11_TEXTURE3D_DESC desc = {};
		desc.Width = 256;
		desc.Height = 256;
		desc.Depth = 256;
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		winrt::check_hresult(d3dDevice->CreateTexture3D(&desc, nullptr, m_colorTallyTexture.put()));
	}
	winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(m_colorTallyTexture.get(), nullptr, m_colorTallyUav.put()));
	winrt::check_hresult(d3dDevice->CreateShaderResourceView(m_colorTallyTexture.get(), nullptr, m_colorTallySrv.put()));

	// Create input texture
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

	// Create texture info buffers
	{
		auto paddedSize = ComputePaddedBufferSize(sizeof(TextureInfo));
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = paddedSize;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		TextureInfo initialInfo = {};
		initialInfo.Width = width;
		initialInfo.Height = height;
		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = reinterpret_cast<void*>(&initialInfo);
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, &initData, m_textureInfoBuffer.put()));
	}

	// Create color count buffers
	{
		auto paddedSize = ComputePaddedBufferSize(sizeof(ShaderColorCount));
		paddedSize = sizeof(ShaderColorCount);
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = paddedSize;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = paddedSize;
		ShaderColorCount initialInfo = {};
		initialInfo.Count = 0;
		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = reinterpret_cast<void*>(&initialInfo);
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, &initData, m_colorCountBuffer.put()));

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDiff = {};
		uavDiff.Format = DXGI_FORMAT_UNKNOWN;
		uavDiff.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDiff.Buffer.NumElements = 1;
		winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(m_colorCountBuffer.get(), &uavDiff, m_colorCountUav.put()));

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, nullptr, m_colorCountStagingBuffer.put()));
	}

	// We have to create the accumulate buffers on the fly

	// Load shaders
	winrt::check_hresult(d3dDevice->CreateComputeShader(shaders::tally::g_main, ARRAYSIZE(shaders::tally::g_main), nullptr, m_colorTallyShader.put()));
	winrt::check_hresult(d3dDevice->CreateComputeShader(shaders::count::g_main, ARRAYSIZE(shaders::count::g_main), nullptr, m_countShader.put()));
	winrt::check_hresult(d3dDevice->CreateComputeShader(shaders::accumulate::g_main, ARRAYSIZE(shaders::accumulate::g_main), nullptr, m_accmumulateShader.put()));
}

std::vector<ColorCount> HistogramGenerator::Generate(std::vector<ComposedFrame> const& frames)
{
	// Clear our color tally texture
	{
		D3D11_TEXTURE3D_DESC desc = {};
		m_colorTallyTexture->GetDesc(&desc);
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		winrt::com_ptr<ID3D11Texture3D> tempTexture;
		winrt::check_hresult(m_d3dDevice->CreateTexture3D(&desc, nullptr, tempTexture.put()));
		winrt::com_ptr<ID3D11RenderTargetView> tempRtv;
		winrt::check_hresult(m_d3dDevice->CreateRenderTargetView(tempTexture.get(), nullptr, tempRtv.put()));
		m_d3dContext->ClearRenderTargetView(tempRtv.get(), CLEARCOLOR);
		m_d3dContext->CopyResource(m_colorTallyTexture.get(), tempTexture.get());
	}

	// Clear our color count buffer
	{
		D3D11_BUFFER_DESC desc = {};
		m_colorCountBuffer->GetDesc(&desc);
		desc.BindFlags = 0;
		desc.MiscFlags = 0;
		ShaderColorCount initialInfo = {};
		initialInfo.Count = 0;
		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = reinterpret_cast<void*>(&initialInfo);
		winrt::com_ptr<ID3D11Buffer> colorCountClearBuffer;
		winrt::check_hresult(m_d3dDevice->CreateBuffer(&desc, &initData, colorCountClearBuffer.put()));
		m_d3dContext->CopyResource(m_colorCountBuffer.get(), colorCountClearBuffer.get());
	}

	// Run the color tally shader
	m_d3dContext->CSSetShader(m_colorTallyShader.get(), nullptr, 0);
	std::vector<ID3D11ShaderResourceView*> srvs = { m_inputSrv.get() };
	m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	std::vector<ID3D11Buffer*> constants = { m_textureInfoBuffer.get() };
	m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	std::vector<ID3D11UnorderedAccessView*> uavs = { m_colorTallyUav.get() };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

	for (auto&& frame : frames)
	{
		m_d3dContext->CopyResource(m_inputTexture.get(), frame.Texture.get());
		m_d3dContext->Dispatch((m_width / 8) + 1, (m_height / 8) + 1, 1);
	}

	uavs = { nullptr };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

	// Run the count shader
	m_d3dContext->CSSetShader(m_countShader.get(), nullptr, 0);
	srvs = { m_colorTallySrv.get()};
	m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	constants = { nullptr };
	m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	uavs = { m_colorCountUav.get() };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);
	m_d3dContext->Dispatch(256 / 8, 256 / 8, 256 / 8);

	// Get the color count
	m_d3dContext->CopyResource(m_colorCountStagingBuffer.get(), m_colorCountBuffer.get());
	auto shaderColorCount = ReadFromBuffer<ShaderColorCount>(m_d3dContext, m_colorCountStagingBuffer);
	
	// Create accumulate buffers
	winrt::com_ptr<ID3D11Buffer> accumulateBuffer;
	winrt::com_ptr<ID3D11Buffer> accumulateStagingBuffer;
	winrt::com_ptr<ID3D11UnorderedAccessView> accumulateUav;
	auto paddedSize = ComputePaddedBufferSize(sizeof(ShaderColorTally));
	paddedSize = sizeof(ShaderColorTally);
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = paddedSize * shaderColorCount.Count;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = paddedSize;
		winrt::check_hresult(m_d3dDevice->CreateBuffer(&desc, nullptr, accumulateBuffer.put()));

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDiff = {};
		uavDiff.Format = DXGI_FORMAT_UNKNOWN;
		uavDiff.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDiff.Buffer.NumElements = shaderColorCount.Count;
		uavDiff.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
		winrt::check_hresult(m_d3dDevice->CreateUnorderedAccessView(accumulateBuffer.get(), &uavDiff, accumulateUav.put()));

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		winrt::check_hresult(m_d3dDevice->CreateBuffer(&desc, nullptr, accumulateStagingBuffer.put()));
	}

	// Run the accumulate shader
	m_d3dContext->CSSetShader(m_accmumulateShader.get(), nullptr, 0);
	uavs = { accumulateUav.get() };
	m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);
	m_d3dContext->Dispatch(256 / 8, 256 / 8, 256 / 8);
	 
	// Extracted accumulated data
	std::vector<ColorCount> result;
	result.reserve(shaderColorCount.Count);
	m_d3dContext->CopyResource(accumulateStagingBuffer.get(), accumulateBuffer.get());
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(accumulateStagingBuffer.get(), 0, D3D11_MAP_READ, 0, &mapped));
		ShaderColorTally tally = {};
		for (uint32_t i = 0; i < shaderColorCount.Count; i++)
		{
			uint8_t* current = reinterpret_cast<uint8_t*>(mapped.pData) + (paddedSize * i);
			tally = *reinterpret_cast<ShaderColorTally*>(current);
			result.push_back({
				winrt::Color{ 255, static_cast<uint8_t>(tally.R), static_cast<uint8_t>(tally.G), static_cast<uint8_t>(tally.B) },
				tally.Count
				});
		}
		m_d3dContext->Unmap(accumulateStagingBuffer.get(), 0);
	}
	
	// Sort color counts
	std::sort(result.begin(), result.end(), [](ColorCount a, ColorCount b)
		{
			return a.Count < b.Count;
		});

	return result;
}