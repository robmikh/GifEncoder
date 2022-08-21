#include "pch.h"

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

struct RaniLayer
{
    std::wstring Name;
    bool Visible = true;
    float Opacity = 1.0f;

    winrt::IBuffer PngData{nullptr};
};

struct RaniFrame
{
    std::vector<RaniLayer> Layers;
};

struct RaniProject
{
    int Width = 0;
    int Height = 0;
    winrt::TimeSpan FrameTime = std::chrono::milliseconds(130);
    winrt::Color BackgroundColor = { 255, 255, 255, 255 };

    std::vector<RaniFrame> Frames;
};

std::future<std::unique_ptr<RaniProject>> LoadRaniProjectFromStorageFileAsync(
    winrt::StorageFile file);

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

    // Read rani file
    path = std::filesystem::current_path();
    path /= "untitled.rani";
    auto inputFile = util::GetStorageFileFromPathAsync(path.wstring()).get();
    auto project = LoadRaniProjectFromStorageFileAsync(inputFile).get();

    // Create a texture for each composed layer
    std::vector<winrt::com_ptr<ID3D11Texture2D>> frames;
    for (auto&& frame : project->Frames)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = project->Width;
        desc.Height = project->Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.SampleDesc.Count = 1;
        winrt::com_ptr<ID3D11Texture2D> renderTargetTexture;
        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, renderTargetTexture.put()));
        auto renderTarget = CreateBitmapFromTexture(renderTargetTexture, d2dContext);
        d2dContext->SetTarget(renderTarget.get());

        auto backgroundColor = project->BackgroundColor;
        auto clearColor = D2D1_COLOR_F{ static_cast<float>(backgroundColor.R) / 255.0f, static_cast<float>(backgroundColor.G) / 255.0f, static_cast<float>(backgroundColor.B) / 255.0f, static_cast<float>(backgroundColor.A) / 255.0f };
        
        // TEMP DEBUG
        project->BackgroundColor.A = 0;
        project->BackgroundColor.R = 255;
        project->BackgroundColor.G = 255;
        project->BackgroundColor.B = 255;

        d2dContext->BeginDraw();
        d2dContext->Clear(&clearColor);
        for (auto&& layer : frame.Layers)
        {
            if (layer.Visible)
            {
                auto pngDataStream = winrt::InMemoryRandomAccessStream();
                pngDataStream.WriteAsync(layer.PngData).get();

                auto layerTexture = util::LoadTextureFromStreamAsync(pngDataStream, d3dDevice).get();
                auto layerBitmap = CreateBitmapFromTexture(layerTexture, d2dContext);

                auto opacity = layer.Opacity;
                d2dContext->DrawBitmap(layerBitmap.get(), nullptr, opacity, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr, nullptr);
            }
        }
        winrt::check_hresult(d2dContext->EndDraw());
        d2dContext->SetTarget(nullptr);

        frames.push_back(renderTargetTexture);
    }

    // Compute the frame delay
    auto millisconds = std::chrono::duration_cast<std::chrono::milliseconds>(project->FrameTime);
    // Use 10ms units
    auto frameDelay = millisconds.count() / 10;

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
        auto bytesPerPixel = util::GetBytesPerPixel(desc.Format);
        winrt::com_ptr<IWICBitmap> wicBitmap;
        winrt::check_hresult(wicFactory->CreateBitmapFromMemory(
            desc.Width,
            desc.Height,
            GUID_WICPixelFormat32bppBGRA,
            bytesPerPixel * desc.Width,
            bytes.size(),
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

        // TEMP DEBUG
        //for (auto&& color : colors)
        //{
        //    wprintf(L"0x%08x\n", color);
        //}
        //wprintf(L"\n");

        // Convert our frame using the palette
        winrt::check_hresult(wicConverter->Initialize(
            wicBitmap.get(),
            GUID_WICPixelFormat8bppIndexed,
            WICBitmapDitherTypeNone, // ???
            wicPalette.get(),
            0.0,
            WICBitmapPaletteTypeFixedWebPalette));

        std::vector<uint8_t> readbackBytes(desc.Width * desc.Height, 0);
        winrt::check_hresult(wicConverter->CopyPixels(nullptr, desc.Width, readbackBytes.size(), readbackBytes.data()));
        

        // Attmept to fix the alpha
        for (auto i = 0; i < desc.Width * desc.Height; i++)
        {
            auto alpha = bytes[(i * 4) + 3];
            if (alpha == 0)
            {
                readbackBytes[i] = transparentColorIndex;
            }
        }

        {
            std::stringstream stringStream;
            stringStream << "debug_indexed_" << desc.Width << "x" << desc.Height << ".bin";
            std::ofstream file(stringStream.str(), std::ios::out | std::ios::binary);
            for (auto&& index : readbackBytes)
            {
                std::vector<uint8_t> bgraBytes(4, 0);
                bgraBytes[0] = index;
                bgraBytes[1] = index;
                bgraBytes[2] = index;
                bgraBytes[3] = 255;
                file.write(reinterpret_cast<const char*>(bgraBytes.data()), bgraBytes.size());
            }
        }

        // Create a new bitmap with the fixed bytes
        winrt::com_ptr<IWICBitmap> wicBitmapFixed;
        winrt::check_hresult(wicFactory->CreateBitmapFromMemory(
            desc.Width,
            desc.Height,
            GUID_WICPixelFormat8bppIndexed,
            desc.Width,
            readbackBytes.size(),
            readbackBytes.data(),
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
                //transparencyIndex.bVal = 0;
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

            {
                std::stringstream stringStream;
                stringStream << "debug_" << desc.Width << "x" << desc.Height << ".bin";
                std::ofstream file(stringStream.str(), std::ios::out | std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
        }

        // Write out bitmap and commit the frame
        winrt::check_hresult(wicFrame->WriteSource(wicBitmapFixed.get(), nullptr));
        winrt::check_hresult(wicFrame->Commit());

        frameIndex++;
    }
    winrt::check_hresult(wicEncoder->Commit());

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

std::future<std::unique_ptr<RaniProject>> LoadRaniProjectFromStorageFileAsync(
    winrt::StorageFile file)
{
    auto document = co_await winrt::XmlDocument::LoadFromFileAsync(file);
    // The project node should be in the top level
    winrt::IXmlNode projectNode{ nullptr };
    for (auto&& child : document.ChildNodes())
    {
        if (child.NodeName() == L"AnimatorProject")
        {
            projectNode = child;
            break;
        }
    }
    if (projectNode == nullptr)
    {
        throw winrt::hresult_error(E_FAIL, L"Expected AnimatorProject node as a child of the document.");
    }

    auto project = std::make_unique<RaniProject>();
    auto projectAttributes = projectNode.Attributes();
    for (auto&& projectAttribute : projectAttributes)
    {
        auto attributeName = projectAttribute.NodeName();
        auto attributeValue = projectAttribute.NodeValue();
        if (attributeName == L"Width")
        {
            auto value = std::wstring(winrt::unbox_value<winrt::hstring>(attributeValue));
            project->Width = std::stoi(value);
        }
        else if (attributeName == L"Height")
        {
            auto value = std::wstring(winrt::unbox_value<winrt::hstring>(attributeValue));
            project->Height = std::stoi(value);
        }
        else if (attributeName == L"FrameTimeInMs")
        {

            auto value = std::wstring(winrt::unbox_value<winrt::hstring>(attributeValue));
            project->FrameTime = std::chrono::milliseconds(std::stoi(value));
        }
        else if (attributeName == L"BackgroundColor")
        {
            auto value = std::wstring(winrt::unbox_value<winrt::hstring>(attributeValue));
            uint64_t rawColor = std::stoul(value, nullptr, 16);
            auto red = (0xFF000000 & rawColor) >> 24;
            auto green = (0x00FF0000 & rawColor) >> 16;
            auto blue = (0x0000FF00 & rawColor) >> 8;
            auto alpha = 0x000000FF & rawColor;

            project->BackgroundColor.A = alpha;
            project->BackgroundColor.R = red;
            project->BackgroundColor.G = green;
            project->BackgroundColor.B = blue;
        }
    }
    if (project->Width == 0 || project->Height == 0)
    {
        throw winrt::hresult_error(E_FAIL, L"A width or height of 0 is invalid.");
    }

    auto projectChildren = projectNode.ChildNodes();
    for (auto&& projectChild : projectChildren)
    {
        auto projectChildName = projectChild.NodeName();
        if (projectChildName == L"Frames")
        {
            for (auto&& frameNode : projectChild.ChildNodes())
            {
                if (frameNode.NodeName() == L"Frame")
                {
                    RaniFrame frame = {};

                    for (auto&& layersNode : frameNode.ChildNodes())
                    {
                        if (layersNode.NodeName() == L"Layers")
                        {
                            for (auto&& layerNode : layersNode.ChildNodes())
                            {
                                if (layerNode.NodeName() == L"Layer")
                                {
                                    RaniLayer layer = {};

                                    auto layerAttributes = layerNode.Attributes();
                                    for (auto&& layerAttribute : layerAttributes)
                                    {
                                        auto attributeName = layerAttribute.NodeName();
                                        auto attributeValue = layerAttribute.NodeValue();
                                        if (attributeName == L"Name")
                                        {
                                            layer.Name = std::wstring(winrt::unbox_value<winrt::hstring>(attributeValue));
                                        }
                                        else if (attributeName == L"Visible")
                                        {
                                            auto value = std::wstring(winrt::unbox_value<winrt::hstring>(attributeValue));
                                            std::transform(value.begin(), value.end(), value.begin(),
                                                [](wchar_t c) { return std::towlower(c); });
                                            layer.Visible = value == L"true";
                                        }
                                        else if (attributeName == L"Opacity")
                                        {
                                            auto value = std::wstring(winrt::unbox_value<winrt::hstring>(attributeValue));
                                            layer.Opacity = std::stoi(value);
                                        }
                                    }

                                    for (auto&& layerChild : layerNode.ChildNodes())
                                    {
                                        if (layerChild.NodeName() == L"PngData")
                                        {
                                            winrt::hstring base64String;
                                            for (auto&& textChild : layerChild.ChildNodes())
                                            {
                                                if (textChild.NodeName() == L"#text")
                                                {
                                                    base64String = layerChild.InnerText();
                                                    break;
                                                }
                                            }
                                            if (base64String.empty())
                                            {
                                                throw winrt::hresult_error(E_FAIL, L"Expected png data.");
                                            }

                                            layer.PngData = winrt::CryptographicBuffer::DecodeFromBase64String(base64String);
                                        }
                                    }

                                    frame.Layers.push_back(layer);
                                }
                            }
                        }
                    }

                    project->Frames.push_back(frame);
                }
            }
        }
    }

    co_return project;
}