#pragma once
#include <Unknwn.h>
#include <winrt/base.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d2d1_3.h>

namespace robmikh::common::uwp
{
    inline winrt::com_ptr<ID2D1Bitmap1> CreateBitmapFromTexture(
        winrt::com_ptr<ID3D11Texture2D> const& texture,
        winrt::com_ptr<ID2D1DeviceContext> const& d2dContext)
    {
        auto dxgiSurface = texture.as<IDXGISurface>();
        winrt::com_ptr<ID2D1Bitmap1> bitmap;
        winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.get(), nullptr, bitmap.put()));
        return bitmap;
    }
}
