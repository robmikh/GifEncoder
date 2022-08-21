#pragma once

struct RaniLayer
{
    std::wstring Name;
    bool Visible = true;
    float Opacity = 1.0f;

    winrt::Windows::Storage::Streams::IBuffer PngData{ nullptr };
};

struct RaniFrame
{
    std::vector<RaniLayer> Layers;
};

struct RaniProject
{
    int Width = 0;
    int Height = 0;
    winrt::Windows::Foundation::TimeSpan FrameTime = std::chrono::milliseconds(130);
    winrt::Windows::UI::Color BackgroundColor = { 255, 255, 255, 255 };

    std::vector<RaniFrame> Frames;
};

inline std::unique_ptr<RaniProject> LoadRaniProjectFromXmlDocument(
    winrt::Windows::Data::Xml::Dom::XmlDocument const& document)
{
    // The project node should be in the top level
    winrt::Windows::Data::Xml::Dom::IXmlNode projectNode{ nullptr };
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

            project->BackgroundColor.A = static_cast<uint8_t>(alpha);
            project->BackgroundColor.R = static_cast<uint8_t>(red);
            project->BackgroundColor.G = static_cast<uint8_t>(green);
            project->BackgroundColor.B = static_cast<uint8_t>(blue);
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
                                            layer.Opacity = std::stof(value);
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

                                            layer.PngData = winrt::Windows::Security::Cryptography::CryptographicBuffer::DecodeFromBase64String(base64String);
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

    return project;
}

inline std::vector<winrt::com_ptr<ID3D11Texture2D>> ComposeFrames(
    std::unique_ptr<RaniProject> const& project,
    winrt::com_ptr<ID3D11Device> const& d3dDevice,
    winrt::com_ptr<ID2D1DeviceContext> const& d2dContext)
{
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
        auto renderTarget = robmikh::common::uwp::CreateBitmapFromTexture(renderTargetTexture, d2dContext);
        d2dContext->SetTarget(renderTarget.get());

        auto backgroundColor = project->BackgroundColor;
        auto clearColor = D2D1_COLOR_F{ static_cast<float>(backgroundColor.R) / 255.0f, static_cast<float>(backgroundColor.G) / 255.0f, static_cast<float>(backgroundColor.B) / 255.0f, static_cast<float>(backgroundColor.A) / 255.0f };
        d2dContext->BeginDraw();
        d2dContext->Clear(&clearColor);
        for (auto&& layer : frame.Layers)
        {
            if (layer.Visible)
            {
                auto pngDataStream = winrt::Windows::Storage::Streams::InMemoryRandomAccessStream();
                pngDataStream.WriteAsync(layer.PngData).get();

                auto layerTexture = robmikh::common::uwp::LoadTextureFromStreamAsync(pngDataStream, d3dDevice).get();
                auto layerBitmap = robmikh::common::uwp::CreateBitmapFromTexture(layerTexture, d2dContext);

                auto opacity = layer.Opacity;
                d2dContext->DrawBitmap(layerBitmap.get(), nullptr, opacity, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr, nullptr);
            }
        }
        winrt::check_hresult(d2dContext->EndDraw());
        d2dContext->SetTarget(nullptr);

        frames.push_back(renderTargetTexture);
    }
    return frames;
}
