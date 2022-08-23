#include "pch.h"
#include "TransparencyFixer.h"
#include "IComposedFrameProvider.h"
#include "DebugFileWriters.h"
#include "HistogramGenerator.h"
#include "ColorDistanceGenerator.h"
#include "PaletteIndexLUT.h"
#include "ColorQuantizer.h"

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

struct Options
{
    bool UseDebugLayer;
    std::wstring InputPath;
    std::wstring OutputPath;
};

enum class CliResult
{
    Valid,
    Invalid,
    Help,
};

CliResult ParseOptions(std::vector<std::wstring> const& args, Options& options);
void PrintHelp();

winrt::IAsyncAction MainAsync(bool useDebugLayer, std::wstring inputPath, std::wstring outputPath)
{
    // Read input file
    auto inputFile = co_await util::GetStorageFileFromPathAsync(inputPath);
    auto inputFrameProvider = co_await LoadComposedFrameProviderFromFileAsync(inputFile);
    uint32_t width = inputFrameProvider->Width();
    uint32_t height = inputFrameProvider->Height();

    // Create output file
    auto outputFile = co_await util::CreateStorageFileFromPathAsync(outputPath);

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

    // Create a texture for each composed layer
    auto frames = inputFrameProvider->GetFrames(d3dDevice, d2dContext);

    auto histogramGenerator = HistogramGenerator(d3dDevice, width, height);
    auto [colorDatas, colorsBuffer] = histogramGenerator.Generate(frames);

    // TEMP DEBUG
    {
        std::wstringstream csvStream;
        csvStream << L"Color, Count" << std::endl;
        for (auto&& colorData : colorDatas)
        {
            uint32_t color = 0;
            color |= colorData.Color.A << 24;
            color |= colorData.Color.R << 16;
            color |= colorData.Color.G << 8;
            color |= colorData.Color.B;
            csvStream << L"#" << std::uppercase << std::setfill(L'0') << std::setw(8) << std::hex << color;
            csvStream << L"," << std::dec << colorData.Count << std::endl;
        }
        auto text = csvStream.str();

        auto path = std::filesystem::current_path();
        path /= "histogram.csv";
        auto histogramFile = co_await util::CreateStorageFileFromPathAsync(path.wstring());

        co_await winrt::FileIO::WriteTextAsync(histogramFile, text, winrt::UnicodeEncoding::Utf8);
    }

    auto distances = ColorDistanceGenerator::Generate(d3dDevice, d3dContext, colorsBuffer, colorDatas.size());
    
    // TODO: Properly pick colors
    // Let's just sort and pick the top 255 colors to test global palette encoding.
    std::vector<winrt::Color> globalPaletteColors;
    globalPaletteColors.reserve(255);
    std::vector<size_t> sortedColorIndexes;
    sortedColorIndexes.reserve(colorDatas.size());
    for (auto i = 0; i < colorDatas.size(); i++)
    {
        sortedColorIndexes.push_back(i);
    }
    struct ColorIndexComparitor
    {
        std::vector<ColorCount> const& ColorDatas;

        bool operator()(size_t a, size_t b) const
        {
            return ColorDatas[a].Count > ColorDatas[b].Count;
        }
    };
    std::sort(sortedColorIndexes.begin(), sortedColorIndexes.end(), ColorIndexComparitor{ colorDatas });
    std::vector<size_t> selectedColors;
    selectedColors.reserve(sortedColorIndexes.size());
    for (auto i = 0; i < 255; i++)
    {
        selectedColors.push_back(sortedColorIndexes[i]);
    }
    for (auto i = 0; i < selectedColors.size(); i++)
    {
        auto currentColorIndex = selectedColors[i];

        // Find the closest color that isn't ourselves
        auto minDistance = FLT_MAX;
        auto closestColorSecondaryIndex = -1;
        for (auto j = 0; j < selectedColors.size(); j++)
        {
            auto otherColorIndex = selectedColors[j];
            if (currentColorIndex == otherColorIndex)
            {
                continue;
            }

            auto distance = distances[currentColorIndex][otherColorIndex];
            if (minDistance > distance)
            {
                minDistance = distance;
                closestColorSecondaryIndex = j;
            }
        }

        if (closestColorSecondaryIndex >= 0 && minDistance < 3.0f)
        {
            selectedColors.erase(selectedColors.begin() + closestColorSecondaryIndex);
        }

        
    }
    for (auto&& selectedColor : selectedColors)
    {
        globalPaletteColors.push_back(colorDatas[selectedColor].Color);
    }

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

    auto lut = PaletteIndexLUT(d3dDevice, d3dContext);
    lut.Generate(globalPaletteColors);
    auto quantizer = ColorQuantizer(d3dDevice, d3dContext, width, height);
    auto transparencyFixer = TransparencyFixer(d3dDevice, d3dContext, width, height);
    std::vector<uint8_t> indexPixelBytes(width * height, 0);
    std::vector<uint8_t> tempBuffer;

    // Create our WIC palette
    winrt::com_ptr<IWICPalette> globalWicPalette;
    winrt::check_hresult(wicFactory->CreatePalette(globalWicPalette.put()));
    std::vector<WICColor> wicColors;
    wicColors.reserve(256);
    for (auto&& color : globalPaletteColors)
    {
        uint32_t colorAsNum = 0;
        colorAsNum |= color.A << 24;
        colorAsNum |= color.R << 16;
        colorAsNum |= color.G << 8;
        colorAsNum |= color.B;

        wicColors.push_back(colorAsNum);
    }
    wicColors.push_back(0);
    winrt::check_hresult(globalWicPalette->InitializeCustom(wicColors.data(), static_cast<uint32_t>(wicColors.size())));

    // Encode each frame
    auto frameIndex = 0;
    winrt::TimeSpan unusedDelay = {};
    for (auto&& frame : frames)
    {
        // Create our converter
        winrt::com_ptr<IWICFormatConverter> wicConverter;
        winrt::check_hresult(wicFactory->CreateFormatConverter(wicConverter.put()));

        auto frameTexture = frame.Texture;

        // Create a quantized bitmap from our texture
        D3D11_TEXTURE2D_DESC desc = {};
        frameTexture->GetDesc(&desc);

        auto transparentColorIndex = wicColors.size() - 1;
        quantizer.Quantize(frameTexture, lut.Srv());
        auto indexedTexture = quantizer.OutputTexture();

        std::optional<DiffInfo> diffInfoOpt = std::nullopt;
        if (transparentColorIndex >= 0 && frameIndex > 0)
        {
            auto info = transparencyFixer.ProcessInput(frameTexture, indexedTexture, static_cast<int>(transparentColorIndex), indexPixelBytes);
            if (info.NumDifferingPixels > 0)
            {
                diffInfoOpt = std::optional(std::move(info));
            }
            else
            {
                unusedDelay = unusedDelay + frame.Delay;
                continue;
            }
        }
        else
        {
            transparencyFixer.InitPrevious(frameTexture, indexedTexture, indexPixelBytes);
        }

        // TEMP DEBUG
        //{
        //    auto debugFileName = ImageViewerFileNameFromSize("debug_indexed", desc.Width, desc.Height);
        //    WriteIndexedPixelBytesToFileAsBgra8(debugFileName, indexPixelBytes);
        //}

        // Create a new bitmap with the fixed bytes
        winrt::com_ptr<IWICBitmap> wicBitmapFixed;
        if (diffInfoOpt.has_value())
        {
            auto diffInfo = diffInfoOpt.value();

            uint32_t minValue = 1;
            uint32_t newWidth = std::max(diffInfo.right - diffInfo.left, minValue);
            uint32_t newHeight = std::max(diffInfo.bottom - diffInfo.top, minValue);
            tempBuffer.resize(newWidth * newHeight);

            for (uint32_t i = 0; i < newHeight; i++)
            {
                auto source = indexPixelBytes.data() + (((diffInfo.top + i) * width) + diffInfo.left);
                auto dest = tempBuffer.data() + (i * newWidth);

                memcpy_s(dest, newWidth, source, newWidth);
            }

            winrt::check_hresult(wicFactory->CreateBitmapFromMemory(
                newWidth,
                newHeight,
                GUID_WICPixelFormat8bppIndexed,
                newWidth,
                static_cast<uint32_t>(tempBuffer.size()),
                tempBuffer.data(),
                wicBitmapFixed.put()));
        }
        else
        {
            winrt::check_hresult(wicFactory->CreateBitmapFromMemory(
                desc.Width,
                desc.Height,
                GUID_WICPixelFormat8bppIndexed,
                desc.Width,
                static_cast<uint32_t>(indexPixelBytes.size()),
                indexPixelBytes.data(),
                wicBitmapFixed.put()));
        }
        winrt::check_hresult(wicBitmapFixed->SetPalette(globalWicPalette.get()));

        // Setup our WIC frame
        winrt::com_ptr<IWICBitmapFrameEncode> wicFrame;
        winrt::check_hresult(wicEncoder->CreateNewFrame(wicFrame.put(), nullptr));
        winrt::check_hresult(wicFrame->Initialize(nullptr));

        // Compute the frame delay
        auto delay = frame.Delay + unusedDelay;
        unusedDelay = {};
        auto millisconds = std::chrono::duration_cast<std::chrono::milliseconds>(delay);
        // Use 10ms units
        auto frameDelay = millisconds.count() / 10;

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

        if (diffInfoOpt.has_value())
        {
            auto diffInfo = diffInfoOpt.value();

            {
                PROPVARIANT value = {};
                value.vt = VT_UI2;
                value.uiVal = static_cast<unsigned short>(diffInfo.left);
                winrt::check_hresult(metadata->SetMetadataByName(L"/imgdesc/Left", &value));
            }
            {
                PROPVARIANT value = {};
                value.vt = VT_UI2;
                value.uiVal = static_cast<unsigned short>(diffInfo.top);
                winrt::check_hresult(metadata->SetMetadataByName(L"/imgdesc/Top", &value));
            }
        }

        // Write out bitmap and commit the frame
        winrt::check_hresult(wicFrame->WriteSource(wicBitmapFixed.get(), nullptr));
        winrt::check_hresult(wicFrame->Commit());

        frameIndex++;
    }
    winrt::check_hresult(wicEncoder->Commit());
}

int __stdcall wmain(int argc, wchar_t* argv[])
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // CLI
    std::vector<std::wstring> args(argv + 1, argv + argc);
    Options options = {};
    auto cliResult = ParseOptions(args, options);
    switch (cliResult)
    {
    case CliResult::Help:
        return 0;
    case CliResult::Invalid:
        return 1;
    default:
        break;
    }

    MainAsync(options.UseDebugLayer, options.InputPath, options.OutputPath).get();

    return 0;
}

CliResult ParseOptions(std::vector<std::wstring> const& args, Options& options)
{
    using namespace robmikh::common::wcli::impl;

    if (GetFlag(args, L"-help") || GetFlag(args, L"/?"))
    {
        PrintHelp();
        return CliResult::Help;
    }
    auto inputPath = GetFlagValue(args, L"-i", L"/i");
    if (inputPath.empty())
    {
        wprintf(L"Invalid input path! Use '-help' for help.\n");
        return CliResult::Invalid;
    }
    auto outputPath = GetFlagValue(args, L"-o", L"/o");
    if (outputPath.empty())
    {
        wprintf(L"Invalid output path! Use '-help' for help.\n");
        return CliResult::Invalid;
    }
    auto useDebugLayer = GetFlag(args, L"-dxDebug", L"/dxDebug");

    options.UseDebugLayer = useDebugLayer;
    options.InputPath = inputPath;
    options.OutputPath = outputPath;
    return CliResult::Valid;
}

void PrintHelp()
{
    wprintf(L"GifEncoder.exe\n");
    wprintf(L"An experimental GIF encoder utility for Windows.\n");
    wprintf(L"\n");
    wprintf(L"Arguments:\n");
    wprintf(L"  -i <input path>          (required) Path to input file (*.rani, *.gif).\n");
    wprintf(L"  -o <output path>         (required) Path to the output image that will be created.\n");
    wprintf(L"\n");
    wprintf(L"Flags:\n");
    wprintf(L"  -dxDebug           (optional) Use the DirectX and DirectML debug layers.\n");
    wprintf(L"\n");
}