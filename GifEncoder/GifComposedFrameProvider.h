#pragma once
#include "IComposedFrameProvider.h"

struct GifComposedFrameProvider : IComposedFrameProvider
{
	GifComposedFrameProvider(
		winrt::Windows::Storage::Streams::IRandomAccessStream const& stream, 
		winrt::com_ptr<IWICImagingFactory2> const& wicFactory);
	~GifComposedFrameProvider() {}

	uint32_t Width() override { return m_width; }
	uint32_t Height() override { return m_height; }
	std::vector<ComposedFrame> GetFrames(
		winrt::com_ptr<ID3D11Device> const& d3dDevice,
		winrt::com_ptr<ID2D1DeviceContext> const& d2dContext) override;

private:
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_frameCount = 0;
	winrt::Windows::Storage::Streams::IRandomAccessStream m_stream{ nullptr };
	winrt::com_ptr<IStream> m_abiStream;
	winrt::com_ptr<IWICImagingFactory2> m_wicFactory;
	winrt::com_ptr<IWICBitmapDecoder> m_wicDecoder;
};