#include "pch.h"
#include "ColorDistanceGenerator.h"

namespace shaders
{
	namespace rgbToLab
	{
#include "RgbToLabShader.h"
	}
	namespace colorDistances
	{
#include "ComputeLabColorDistancesShader.h"
	}
}

struct ColorsInfo
{
	uint32_t ColorsCount;
};

std::vector<std::vector<float>> ColorDistanceGenerator::Generate(
	winrt::com_ptr<ID3D11Device> const& d3dDevice,
	winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
	winrt::com_ptr<ID3D11Buffer> const& accumulatedColorsBuffer,
	size_t numColors)
{
	// Create a srv for the accumulated colors buffer
	winrt::com_ptr<ID3D11ShaderResourceView> accumulatedColorsSrv;
	winrt::check_hresult(d3dDevice->CreateShaderResourceView(accumulatedColorsBuffer.get(), nullptr, accumulatedColorsSrv.put()));

	// Create a buffer to hold the lab version of the colors
	winrt::com_ptr<ID3D11Texture1D> labColorsTexture;
	{
		D3D11_TEXTURE1D_DESC desc = {};
		desc.Width = static_cast<uint32_t>(numColors);
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.ArraySize = 1;
		desc.MipLevels = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		winrt::check_hresult(d3dDevice->CreateTexture1D(&desc, nullptr, labColorsTexture.put()));
	}
	winrt::com_ptr<ID3D11UnorderedAccessView> labColorsUav;
	winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(labColorsTexture.get(), nullptr, labColorsUav.put()));
	winrt::com_ptr<ID3D11ShaderResourceView> labColorsSrv;
	winrt::check_hresult(d3dDevice->CreateShaderResourceView(labColorsTexture.get(), nullptr, labColorsSrv.put()));

	// Create our colors info buffer
	winrt::com_ptr<ID3D11Buffer> colorsInfoBuffer;
	{
		auto paddedSize = ComputePaddedBufferSize(sizeof(ColorsInfo));
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = paddedSize;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		ColorsInfo initialInfo = {};
		initialInfo.ColorsCount = static_cast<uint32_t>(numColors);
		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = reinterpret_cast<void*>(&initialInfo);
		winrt::check_hresult(d3dDevice->CreateBuffer(&desc, &initData, colorsInfoBuffer.put()));
	}

	// Load the rgb to lab shader
	winrt::com_ptr<ID3D11ComputeShader> rgbToLabShader;
	winrt::check_hresult(d3dDevice->CreateComputeShader(shaders::rgbToLab::g_main, ARRAYSIZE(shaders::rgbToLab::g_main), nullptr, rgbToLabShader.put()));

	// Run the shader
	d3dContext->CSSetShader(rgbToLabShader.get(), nullptr, 0);
	std::vector<ID3D11ShaderResourceView*> srvs = { accumulatedColorsSrv.get() };
	d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	std::vector<ID3D11Buffer*> constants = { colorsInfoBuffer.get() };
	d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	std::vector<ID3D11UnorderedAccessView*> uavs = { labColorsUav.get() };
	d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

	d3dContext->Dispatch((static_cast<uint32_t>(numColors) / 32) + 1, 1, 1);

	uavs = { nullptr };
	d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);
	srvs = { labColorsSrv.get() };
	d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());

	// Create distances textures
	winrt::com_ptr<ID3D11Texture2D> distancesTexture;
	winrt::com_ptr<ID3D11Texture2D> distancesStagingTexture;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<uint32_t>(numColors);
		desc.Height = static_cast<uint32_t>(numColors);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, distancesTexture.put()));

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, distancesStagingTexture.put()));
	}
	winrt::com_ptr<ID3D11UnorderedAccessView> distancesUav;
	winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(distancesTexture.get(), nullptr, distancesUav.put()));

	// Load the distance shader
	winrt::com_ptr<ID3D11ComputeShader> distancesShader;
	winrt::check_hresult(d3dDevice->CreateComputeShader(shaders::colorDistances::g_main, ARRAYSIZE(shaders::colorDistances::g_main), nullptr, distancesShader.put()));

	// Run the shader
	d3dContext->CSSetShader(distancesShader.get(), nullptr, 0);
	uavs = { distancesUav.get() };
	d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

	d3dContext->Dispatch((static_cast<uint32_t>(numColors) / 32) + 1, 1, 1);

	// Extract the distances
	std::vector<std::vector<float>> result;
	result.reserve(numColors);
	d3dContext->CopyResource(distancesStagingTexture.get(), distancesTexture.get());
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(d3dContext->Map(distancesStagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));
		for (auto i = 0; i < numColors; i++)
		{
			uint8_t* current = reinterpret_cast<uint8_t*>(mapped.pData) + (mapped.RowPitch * i);

			std::vector<float> slice(numColors, 0.0f);

			memcpy_s(slice.data(), slice.size(), reinterpret_cast<float*>(current), slice.size());

			result.push_back(std::move(slice));
		}
		d3dContext->Unmap(distancesStagingTexture.get(), 0);
	}

	// Reset pipeline state
	d3dContext->CSSetShader(nullptr, nullptr, 0);
	srvs = { nullptr };
	d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	constants = { nullptr };
	d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
	uavs = { nullptr };
	d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

	return result;
}