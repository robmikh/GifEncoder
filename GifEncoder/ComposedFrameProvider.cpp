#include "pch.h"
#include "IComposedFrameProvider.h"
#include "RaniComposedFrameProvider.h"
#include "GifComposedFrameProvider.h"

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

std::future<std::unique_ptr<IComposedFrameProvider>> LoadComposedFrameProviderFromFileAsync(
    winrt::Windows::Storage::StorageFile const& file)
{
    auto extension = std::wstring(file.FileType());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t c) { return std::towlower(c); });

    std::unique_ptr<IComposedFrameProvider> result;
    if (extension == L".rani")
    {
        auto document = co_await winrt::XmlDocument::LoadFromFileAsync(file);
        auto project = LoadRaniProjectFromXmlDocument(document);
        result = std::make_unique<RaniComposedFrameProvider>(std::move(project));
    }
    else if (extension == L".gif")
    {
        auto inMemoryStream = winrt::InMemoryRandomAccessStream();
        {
            auto stream = co_await file.OpenAsync(winrt::FileAccessMode::Read);
            winrt::RandomAccessStream::CopyAsync(stream, inMemoryStream);
        }

        auto wicFactory = winrt::create_instance<IWICImagingFactory2>(CLSID_WICImagingFactory2, CLSCTX_INPROC_SERVER);
        result = std::make_unique<GifComposedFrameProvider>(inMemoryStream, wicFactory);
    }
    else
    {
        throw winrt::hresult_invalid_argument(L"File type unsupported.");
    }

    co_return result;
}