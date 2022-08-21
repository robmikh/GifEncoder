#include "pch.h"
#include "ColorQuantizer.h"

namespace shaders
{
	namespace vertex
	{
#include "LUTLookup_VS.h"
	}
	namespace pixel
	{
#include "LUTLookup_PS.h"
	}
}

namespace winrt
{
	using namespace Windows::Foundation::Numerics;
}

struct Vertex
{
	winrt::float3 pos;
	winrt::float2 uv;
};

struct FrameInfo
{
	int TransparentColorIndex;
};

template<typename T>
winrt::com_ptr<ID3D11Buffer> CreateBuffer(winrt::com_ptr<ID3D11Device> d3dDevice, std::vector<T> const& data, uint32_t bindFlags);

ColorQuantizer::ColorQuantizer(
	winrt::com_ptr<ID3D11Device> const& d3dDevice, 
	winrt::com_ptr<ID3D11DeviceContext> const& d3dContext, 
	uint32_t width, 
	uint32_t height)
{
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
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		winrt::check_hresult(d3dDevice->CreateSamplerState(&desc, m_inputSampler.put()));
	}

	// Create a texture for our palettized output
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8_UINT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_outputTexture.put()));

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.MiscFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_stagingTexture.put()));

	}
	winrt::check_hresult(d3dDevice->CreateRenderTargetView(m_outputTexture.get(), nullptr, m_outputRtv.put()));

	// Define quad vertices/indices
	std::vector<Vertex> vertices =
	{
		{ { -1.0,  -1.0, 0.0f }, { 0.0f, 1.0f } },
		{ { -1.0, 1.0, 0.0f }, { 0.0f, 0.0f } },
		{ {1.0, 1.0, 0.0f }, { 1.0f, 0.0f } },
		{ {1.0, 11.0, 0.0f }, { 1.0f, 1.0f } }
	};
	std::vector<uint16_t> indices =
	{
		0, 1, 2, 3, 0, 2
	};
	m_vertexBuffer = CreateBuffer<Vertex>(d3dDevice, vertices, D3D11_BIND_VERTEX_BUFFER);
	m_indexBuffer = CreateBuffer<uint16_t>(d3dDevice, indices, D3D11_BIND_INDEX_BUFFER);

	// Create frame info buffer
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

	// Create LUT lookup shaders
	winrt::check_hresult(d3dDevice->CreateVertexShader(shaders::vertex::g_main, ARRAYSIZE(shaders::vertex::g_main), nullptr, m_vertexShader.put()));
	winrt::check_hresult(d3dDevice->CreatePixelShader(shaders::pixel::g_main, ARRAYSIZE(shaders::pixel::g_main), nullptr, m_pixelShader.put()));

	std::vector<D3D11_INPUT_ELEMENT_DESC> vertexDesc =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	winrt::com_ptr<ID3D11InputLayout> inputLayout;
	winrt::check_hresult(d3dDevice->CreateInputLayout(
		vertexDesc.data(),
		vertexDesc.size(),
		shaders::vertex::g_main,
		ARRAYSIZE(shaders::vertex::g_main),
		inputLayout.put()));

	m_d3dContext = d3dContext;
	std::vector<ID3D11Buffer*> vertexBuffers = { m_vertexBuffer.get() };
	uint32_t vertexStride = sizeof(Vertex);
	uint32_t offset = 0;
	m_d3dContext->IASetVertexBuffers(0, vertexBuffers.size(), vertexBuffers.data(), &vertexStride, &offset);
	m_d3dContext->IASetIndexBuffer(m_indexBuffer.get(), DXGI_FORMAT_R16_UINT, 0);
	m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_d3dContext->IASetInputLayout(inputLayout.get());
}


void ColorQuantizer::Quantize(
	winrt::com_ptr<ID3D11Texture2D> const& texture, 
	winrt::com_ptr<ID3D11ShaderResourceView> const& lutSrv, 
	int transparentColorIndex, 
	std::vector<uint8_t>& bytes)
{
	D3D11_TEXTURE2D_DESC desc = {};
	texture->GetDesc(&desc);

	{
		D3D11_BUFFER_DESC desc = {};
		m_frameInfoStagingBuffer->GetDesc(&desc);
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(m_frameInfoStagingBuffer.get(), 0, D3D11_MAP_WRITE, 0, &mapped));
		FrameInfo info = {};
		info.TransparentColorIndex = transparentColorIndex;
		memcpy_s(mapped.pData, sizeof(FrameInfo), reinterpret_cast<void*>(&info), sizeof(FrameInfo));
		m_d3dContext->Unmap(m_frameInfoStagingBuffer.get(), 0);
	}
	m_d3dContext->CopyResource(m_frameInfoBuffer.get(), m_frameInfoStagingBuffer.get());
	m_d3dContext->CopyResource(m_inputTexture.get(), texture.get());

	m_d3dContext->VSSetShader(m_vertexShader.get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_pixelShader.get(), nullptr, 0);
	std::vector<ID3D11Buffer*> constantBuffers = { m_frameInfoBuffer.get() };
	m_d3dContext->VSSetConstantBuffers(0, constantBuffers.size(), constantBuffers.data());
	std::vector<ID3D11ShaderResourceView*> srvs = { m_inputSrv.get(), lutSrv.get()};
	m_d3dContext->PSSetShaderResources(0, srvs.size(), srvs.data());
	std::vector<ID3D11SamplerState*> samplers = { m_inputSampler.get() };
	m_d3dContext->PSSetSamplers(0, samplers.size(), samplers.data());
	std::vector<ID3D11RenderTargetView*> rtvs = { m_outputRtv.get() };
	m_d3dContext->OMSetRenderTargets(rtvs.size(), rtvs.data(), nullptr);

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(desc.Width);
	viewport.Height = static_cast<float>(desc.Height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_d3dContext->RSSetViewports(1, &viewport);

	m_d3dContext->DrawIndexed(6, 0, 0);

	m_d3dContext->CopyResource(m_stagingTexture.get(), m_outputTexture.get());
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(m_d3dContext->Map(m_stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));

		auto stride = desc.Width;
		WINRT_VERIFY(bytes.size() == stride * desc.Height);
		auto source = reinterpret_cast<byte*>(mapped.pData);
		auto dest = bytes.data();
		for (auto i = 0; i < (int)desc.Height; i++)
		{
			memcpy(dest, source, stride);

			source += mapped.RowPitch;
			dest += stride;
		}
		m_d3dContext->Unmap(m_stagingTexture.get(), 0);
	}

	srvs = { nullptr, nullptr };
	m_d3dContext->PSSetShaderResources(0, srvs.size(), srvs.data());
}

template<typename T>
winrt::com_ptr<ID3D11Buffer> CreateBuffer(winrt::com_ptr<ID3D11Device> d3dDevice, std::vector<T> const& data, uint32_t bindFlags)
{
	D3D11_SUBRESOURCE_DATA bufferData = {};
	bufferData.pSysMem = reinterpret_cast<const void*>(data.data());
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(T) * data.size();
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = bindFlags;
	winrt::com_ptr<ID3D11Buffer> buffer;
	winrt::check_hresult(d3dDevice->CreateBuffer(&desc, &bufferData, buffer.put()));
	return buffer;
}
