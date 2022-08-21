#pragma once

struct ComposedFrame
{
    winrt::com_ptr<ID3D11Texture2D> Texture;
    winrt::Windows::Foundation::TimeSpan Delay;
};

struct IComposedFrameProvider
{
    virtual uint32_t Width() = 0;
    virtual uint32_t Height() = 0;
    virtual std::vector<ComposedFrame> GetFrames(
        winrt::com_ptr<ID3D11Device> const& d3dDevice,
        winrt::com_ptr<ID2D1DeviceContext> const& d2dContext) = 0;
};

std::future<std::unique_ptr<IComposedFrameProvider>> LoadComposedFrameProviderFromFileAsync(
    winrt::Windows::Storage::StorageFile const& file);