#include "pch.h"
#include "IComposedFrameProvider.h"
#include "RaniComposedFrameProvider.h"

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
    else
    {
        throw winrt::hresult_invalid_argument(L"File type unsupported.");
    }

    co_return result;
}