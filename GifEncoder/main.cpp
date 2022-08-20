#include "pch.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::UI;
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
}

namespace util
{
    using namespace robmikh::common::uwp;
    using namespace robmikh::common::desktop;
}

winrt::com_ptr<ID2D1Bitmap1> CreateBitmapFromTexture(
    winrt::com_ptr<ID3D11Texture2D> const& texture,
    winrt::com_ptr<ID2D1DeviceContext> const& d2dContext);

int __stdcall wmain()
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Create output file
    auto path = std::filesystem::current_path();
    path /= "test.gif";
    auto outputFile = util::CreateStorageFileFromPathAsync(path.wstring()).get();

    // Open the file for write
    auto stream = outputFile.OpenAsync(winrt::FileAccessMode::ReadWrite).get();
    auto abiStream = util::CreateStreamFromRandomAccessStream(stream);

    // Initialize DirectX
    auto d3dDevice = util::CreateD3DDevice();
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());
    auto d2dFactory = util::CreateD2DFactory();
    auto d2dDevice = util::CreateD2DDevice(d2dFactory, d3dDevice);

    // Create WIC Encoder
    auto wicFactory = winrt::create_instance<IWICImagingFactory2>(CLSID_WICImagingFactory2, CLSCTX_INPROC_SERVER);
    winrt::com_ptr<IWICBitmapEncoder> wicEncoder;
    winrt::check_hresult(wicFactory->CreateEncoder(GUID_ContainerFormatGif, nullptr, wicEncoder.put()));
    winrt::check_hresult(wicEncoder->Initialize(abiStream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICImageEncoder> wicImageEncoder;
    winrt::check_hresult(wicFactory->CreateImageEncoder(d2dDevice.get(), wicImageEncoder.put()));

    // Write the application block
    // http://www.vurdalakov.net/misc/gif/netscape-looping-application-extension
    winrt::com_ptr<IWICMetadataQueryWriter> metadata;
    winrt::check_hresult(wicEncoder->GetMetadataQueryWriter(metadata.put()));
    {
        PROPVARIANT value = {};
        value.vt = VT_UI1 | VT_VECTOR;
        value.caub.cElems = 11;
        std::string text("NETSCAPE2.0");
        std::vector<uint8_t> chars(text.begin(), text.end());
        WINRT_VERIFY(chars.size() == 11);
        value.caub.pElems = chars.data();
        winrt::check_hresult(metadata->SetMetadataByName(L"/appext/application", &value));
    }
    {
        PROPVARIANT value = {};
        value.vt = VT_UI1 | VT_VECTOR;
        value.caub.cElems = 5;
        // The first value is the size of the block, which is the fixed value 3.
        // The second value is the looping extension, which is the fixed value 1.
        // The third and fourth values comprise an unsigned 2-byte integer (little endian).
        //     The value of 0 means to loop infinitely.
        // The final value is the block terminator, which is the fixed value 0.
        std::vector<uint8_t> data({ 3, 1, 0, 0, 0 });
        value.caub.pElems = data.data();
        winrt::check_hresult(metadata->SetMetadataByName(L"/appext/data", &value));
    }

    // Generate our frames


    return 0;
}

winrt::com_ptr<ID2D1Bitmap1> CreateBitmapFromTexture(
    winrt::com_ptr<ID3D11Texture2D> const& texture,
    winrt::com_ptr<ID2D1DeviceContext> const& d2dContext)
{
    auto dxgiSurface = texture.as<IDXGISurface>();
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.get(), nullptr, bitmap.put()));
    return bitmap;
}
