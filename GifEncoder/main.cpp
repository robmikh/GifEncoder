#include "pch.h"
#include "TransparencyFixer.h"
#include "RaniFormat.h"
#include "DebugFileWriters.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::UI;
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
    using namespace Windows::Data::Xml;
    using namespace Windows::Data::Xml::Dom;
    using namespace Windows::Security::Cryptography;
}

namespace util
{
    using namespace robmikh::common::uwp;
    using namespace robmikh::common::desktop;
}

std::future<std::unique_ptr<RaniProject>> LoadRaniProjectFromStorageFileAsync(
    winrt::StorageFile file);

winrt::IAsyncAction MainAsync(bool useDebugLayer)
{
    // Create output file
    auto path = std::filesystem::current_path();
    path /= "test.gif";
    auto outputFile = co_await util::CreateStorageFileFromPathAsync(path.wstring());

    // Open the file for write
    auto stream = co_await outputFile.OpenAsync(winrt::FileAccessMode::ReadWrite);
    auto abiStream = util::CreateStreamFromRandomAccessStream(stream);

    // Initialize DirectX
    uint32_t d3dCreateFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (useDebugLayer)
    {
        d3dCreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
    auto d3dDevice = util::CreateD3DDevice(d3dCreateFlags);
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());
    auto debugLevel = D2D1_DEBUG_LEVEL_NONE;
    if (useDebugLayer)
    {
        debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    }
    auto d2dFactory = util::CreateD2DFactory(debugLevel);
    auto d2dDevice = util::CreateD2DDevice(d2dFactory, d3dDevice);
    winrt::com_ptr<ID2D1DeviceContext> d2dContext;
    winrt::check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.put()));

    // Create WIC Encoder
    auto wicFactory = winrt::create_instance<IWICImagingFactory2>(CLSID_WICImagingFactory2, CLSCTX_INPROC_SERVER);
    winrt::com_ptr<IWICBitmapEncoder> wicEncoder;
    winrt::check_hresult(wicFactory->CreateEncoder(GUID_ContainerFormatGif, nullptr, wicEncoder.put()));
    winrt::check_hresult(wicEncoder->Initialize(abiStream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICImageEncoder> wicImageEncoder;
    winrt::check_hresult(wicFactory->CreateImageEncoder(d2dDevice.get(), wicImageEncoder.put()));

    // Write the application block
    // http://www.vurdalakov.net/misc/gif/netscape-looping-application-extension
    {
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
    }

    // Read rani file
    path = std::filesystem::current_path();
    path /= "untitled.rani";
    auto inputFile = co_await util::GetStorageFileFromPathAsync(path.wstring());
    auto project = co_await LoadRaniProjectFromStorageFileAsync(inputFile);

    // Create a texture for each composed layer
    auto frames = ComposeFrames(project, d3dDevice, d2dContext);

    // Compute the frame delay
    auto millisconds = std::chrono::duration_cast<std::chrono::milliseconds>(project->FrameTime);
    // Use 10ms units
    auto frameDelay = millisconds.count() / 10;

    auto transparencyFixer = TransparencyFixer(d3dDevice, d3dContext, project->Width, project->Height);
    std::vector<uint8_t> indexPixelBytes(project->Width * project->Height, 0);

    // Encode each frame
    auto frameIndex = 0;
    for (auto&& frameTexture : frames)
    {
        // Create our converter
        winrt::com_ptr<IWICFormatConverter> wicConverter;
        winrt::check_hresult(wicFactory->CreateFormatConverter(wicConverter.put()));

        // Create a WIC bitmap from our texture
        D3D11_TEXTURE2D_DESC desc = {};
        frameTexture->GetDesc(&desc);
        auto bytes = util::CopyBytesFromTexture(frameTexture);
        auto bytesPerPixel = 4;
        winrt::com_ptr<IWICBitmap> wicBitmap;
        winrt::check_hresult(wicFactory->CreateBitmapFromMemory(
            desc.Width,
            desc.Height,
            GUID_WICPixelFormat32bppBGRA,
            bytesPerPixel * desc.Width,
            static_cast<uint32_t>(bytes.size()),
            bytes.data(),
            wicBitmap.put()));

        // Create a pallette for our bitmap
        winrt::com_ptr<IWICPalette> wicPalette;
        winrt::check_hresult(wicFactory->CreatePalette(wicPalette.put()));
        winrt::check_hresult(wicPalette->InitializeFromBitmap(wicBitmap.get(), 256, true));

        // We need to find which color is our transparent one
        uint32_t numColors = 0;
        winrt::check_hresult(wicPalette->GetColorCount(&numColors));
        std::vector<WICColor> colors(numColors, 0);
        winrt::check_hresult(wicPalette->GetColors(numColors, colors.data(), &numColors));
        auto transparentColorIndex = -1;
        for (auto i = 0; i < colors.size(); i++)
        {
            if (colors[i] == 0)
            {
                transparentColorIndex = i;
                break;
            }
        }

        // Convert our frame using the palette
        winrt::check_hresult(wicConverter->Initialize(
            wicBitmap.get(),
            GUID_WICPixelFormat8bppIndexed,
            WICBitmapDitherTypeNone, // ???
            wicPalette.get(),
            0.0,
            WICBitmapPaletteTypeFixedWebPalette));
        winrt::check_hresult(wicConverter->CopyPixels(nullptr, desc.Width, static_cast<uint32_t>(indexPixelBytes.size()), indexPixelBytes.data()));

        if (transparentColorIndex >= 0 && frameIndex > 0)
        {
            transparencyFixer.ProcessInput(frameTexture, transparentColorIndex, indexPixelBytes);
        }
        else
        {
            transparencyFixer.InitPrevious(frameTexture);
        }

        // TEMP DEBUG
        //{
        //    auto debugFileName = ImageViewerFileNameFromSize("debug_indexed", desc.Width, desc.Height);
        //    WriteIndexedPixelBytesToFileAsBgra8(debugFileName, indexPixelBytes);
        //}

        // Create a new bitmap with the fixed bytes
        winrt::com_ptr<IWICBitmap> wicBitmapFixed;
        winrt::check_hresult(wicFactory->CreateBitmapFromMemory(
            desc.Width,
            desc.Height,
            GUID_WICPixelFormat8bppIndexed,
            desc.Width,
            static_cast<uint32_t>(indexPixelBytes.size()),
            indexPixelBytes.data(),
            wicBitmapFixed.put()));
        winrt::check_hresult(wicBitmapFixed->SetPalette(wicPalette.get()));

        // Setup our WIC frame
        winrt::com_ptr<IWICBitmapFrameEncode> wicFrame;
        winrt::check_hresult(wicEncoder->CreateNewFrame(wicFrame.put(), nullptr));
        winrt::check_hresult(wicFrame->Initialize(nullptr));

        // Write frame metadata
        winrt::com_ptr<IWICMetadataQueryWriter> metadata;
        winrt::check_hresult(wicFrame->GetMetadataQueryWriter(metadata.put()));
        // Delay
        {
            PROPVARIANT delayValue = {};
            delayValue.vt = VT_UI2;
            delayValue.uiVal = static_cast<unsigned short>(frameDelay);
            winrt::check_hresult(metadata->SetMetadataByName(L"/grctlext/Delay", &delayValue));
        }
        // Transparency
        if (transparentColorIndex >= 0 && frameIndex > 0)
        {
            {
                PROPVARIANT transparencyValue = {};
                transparencyValue.vt = VT_BOOL;
                transparencyValue.boolVal = TRUE;
                winrt::check_hresult(metadata->SetMetadataByName(L"/grctlext/TransparencyFlag", &transparencyValue));
            }

            {
                PROPVARIANT transparencyIndex = {};
                transparencyIndex.vt = VT_UI1;
                transparencyIndex.bVal = static_cast<uint8_t>(transparentColorIndex);
                winrt::check_hresult(metadata->SetMetadataByName(L"/grctlext/TransparentColorIndex", &transparencyIndex));
            }
        }

        if (frameIndex > 0)
        {
            {
                PROPVARIANT disposalValue = {};
                disposalValue.vt = VT_UI1;
                disposalValue.bVal = 1;
                winrt::check_hresult(metadata->SetMetadataByName(L"/grctlext/Disposal", &disposalValue));
            }

            // TEMP DEBUG
            //{
            //    auto debugFileName = ImageViewerFileNameFromSize("debug", desc.Width, desc.Height);
            //    WriteBgra8PixelsToFile(debugFileName, bytes);
            //}
        }

        // Write out bitmap and commit the frame
        winrt::check_hresult(wicFrame->WriteSource(wicBitmapFixed.get(), nullptr));
        winrt::check_hresult(wicFrame->Commit());

        frameIndex++;
    }
    winrt::check_hresult(wicEncoder->Commit());
}

int __stdcall wmain()
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    bool useDebugLayer = true;

    MainAsync(useDebugLayer).get();

    return 0;
}

std::future<std::unique_ptr<RaniProject>> LoadRaniProjectFromStorageFileAsync(
    winrt::StorageFile file)
{
    auto document = co_await winrt::XmlDocument::LoadFromFileAsync(file);
    co_return LoadRaniProjectFromXmlDocument(document);
}
