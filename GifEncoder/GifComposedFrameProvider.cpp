#include "pch.h"
#include "GifComposedFrameProvider.h"

namespace winrt
{
	using namespace Windows::Storage::Streams;
}

namespace util
{
	using namespace robmikh::common::uwp;
}

GifComposedFrameProvider::GifComposedFrameProvider(
	winrt::IRandomAccessStream const& stream, 
	winrt::com_ptr<IWICImagingFactory2> const& wicFactory)
{
	m_stream = stream;
	m_abiStream = util::CreateStreamFromRandomAccessStream(stream);

	// Create WIC Decoder
	m_wicFactory = wicFactory;
	winrt::check_hresult(m_wicFactory->CreateDecoder(GUID_ContainerFormatGif, nullptr, m_wicDecoder.put()));
	winrt::check_hresult(m_wicDecoder->Initialize(m_abiStream.get(), WICDecodeMetadataCacheOnLoad));

	// Read properties
	winrt::com_ptr<IWICMetadataQueryReader> metadataQueryReader;
	winrt::check_hresult(m_wicDecoder->GetMetadataQueryReader(
		metadataQueryReader.put()));

	m_wicDecoder->GetFrameCount(&m_frameCount);
	m_width = util::GetMetadataByName<uint16_t>(
		metadataQueryReader,
		L"/logscrdesc/Width");
	m_height = util::GetMetadataByName<uint16_t>(
		metadataQueryReader,
		L"/logscrdesc/Height");
}

std::vector<ComposedFrame> GifComposedFrameProvider::GetFrames(
	winrt::com_ptr<ID3D11Device> const& d3dDevice, 
	winrt::com_ptr<ID2D1DeviceContext> const& d2dContext)
{
	std::vector<ComposedFrame> frames;
	frames.reserve(m_frameCount);

	winrt::com_ptr<ID3D11DeviceContext> d3dContext;
	d3dDevice->GetImmediateContext(d3dContext.put());

	// Create our render target
	winrt::com_ptr<ID3D11Texture2D> renderTargetTexture;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_width;
		desc.Height = m_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.SampleDesc.Count = 1;
		winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, renderTargetTexture.put()));
	}
	auto renderTarget = util::CreateBitmapFromTexture(renderTargetTexture, d2dContext);
	d2dContext->SetTarget(renderTarget.get());

	for (uint32_t i = 0; i < m_frameCount; i++)
	{
		winrt::com_ptr<IWICBitmapFrameDecode> wicFrame;
		winrt::check_hresult(m_wicDecoder->GetFrame(i, wicFrame.put()));

		// Read properties
		winrt::com_ptr<IWICMetadataQueryReader> metadataQueryReader;
		winrt::check_hresult(wicFrame->GetMetadataQueryReader(
			metadataQueryReader.put()));

		auto delay = util::GetMetadataByName<uint16_t>(
			metadataQueryReader,
			L"/grctlext/Delay");
		// The delay comes in 10 ms units
		auto milliseconds = std::chrono::milliseconds(static_cast<uint64_t>(delay) * 10);

		auto left = util::GetMetadataByNameOrDefault<uint16_t>(metadataQueryReader, L"/imgdesc/Left", 0);
		auto top = util::GetMetadataByNameOrDefault<uint16_t>(metadataQueryReader, L"/imgdesc/Top", 0);

		winrt::com_ptr<IWICFormatConverter> wicConverter;
		winrt::check_hresult(m_wicFactory->CreateFormatConverter(wicConverter.put()));
		winrt::check_hresult(wicConverter->Initialize(
			wicFrame.get(),
			GUID_WICPixelFormat32bppBGRA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0,
			WICBitmapPaletteTypeCustom));

		uint32_t width = 0;
		uint32_t height = 0;
		winrt::check_hresult(wicConverter->GetSize(&width, &height));

		auto bytesPerPixel = 4;
		auto stride = width * bytesPerPixel;
		std::vector<uint8_t> bytes(stride * height, 0);
		winrt::check_hresult(wicConverter->CopyPixels(nullptr, stride, static_cast<uint32_t>(bytes.size()), bytes.data()));

		// Create our frame texture
		winrt::com_ptr<ID3D11Texture2D> frameTexture;
		{
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.SampleDesc.Count = 1;
			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = bytes.data();
			initData.SysMemPitch = stride;
			winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, &initData, frameTexture.put()));
		}
		auto frameBitmap = util::CreateBitmapFromTexture(frameTexture, d2dContext);

		// Create our final texture
		winrt::com_ptr<ID3D11Texture2D> finalTexture;
		{
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = m_width;
			desc.Height = m_height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.SampleDesc.Count = 1;
			winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, finalTexture.put()));
		}

		d2dContext->BeginDraw();
		D2D1_POINT_2F offset = { static_cast<float>(left), static_cast<float>(top) };
		d2dContext->DrawImage(frameBitmap.get(), offset);
		winrt::check_hresult(d2dContext->EndDraw());

		d3dContext->CopyResource(finalTexture.get(), renderTargetTexture.get());

		frames.push_back(ComposedFrame{ finalTexture, milliseconds });
	}
	return frames;
}
