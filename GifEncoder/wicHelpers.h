#pragma once
#include <Unknwn.h>
#include <winrt/base.h>
#include <wincodec.h>
#include <wil/resource.h>
#include <string>

namespace robmikh::common::uwp
{
    template <typename T>
    T GetValueFromPropVariant(PROPVARIANT const& propValue);

    template <typename T>
    std::optional<T> TryGetMetadataByName(
        winrt::com_ptr<IWICMetadataQueryReader> const& metadataQueryReader,
        std::wstring const& propertyName)
    {
        wil::unique_prop_variant propValue;
        HRESULT hr = metadataQueryReader->GetMetadataByName(
            propertyName.c_str(),
            propValue.addressof());
        std::optional<T> opt = std::nullopt;
        if (SUCCEEDED(hr))
        {
            auto value = GetValueFromPropVariant<T>(propValue);
            opt = std::optional(value);
        }
        else if (hr == WINCODEC_ERR_PROPERTYNOTFOUND)
        {
            // Skip the property
        }
        else
        {
            winrt::check_hresult(hr);
        }
        return opt;
    }

    template <typename T>
    T GetMetadataByNameOrDefault(
        winrt::com_ptr<IWICMetadataQueryReader> const& metadataQueryReader,
        std::wstring const& propertyName,
        T const& defaultValue)
    {
        std::optional<T> opt = TryGetMetadataByName<T>(metadataQueryReader, propertyName);
        if (opt.has_value())
        {
            return opt.value();
        }
        else
        {
            return defaultValue;
        }
    }

    template <typename T>
    T GetMetadataByName(
        winrt::com_ptr<IWICMetadataQueryReader> const& metadataQueryReader,
        std::wstring const& propertyName)
    {
        wil::unique_prop_variant propValue;
        winrt::check_hresult(metadataQueryReader->GetMetadataByName(
            propertyName.c_str(),
            &propValue));
        return GetValueFromPropVariant<T>(propValue);
    }

    template <>
    inline uint16_t GetValueFromPropVariant<uint16_t>(
        PROPVARIANT const& propValue)
    {
        if (propValue.vt != VT_UI2)
        {
            throw winrt::hresult_error(E_UNEXPECTED, L"Unexpected property value type.");
        }
        return propValue.uiVal;
    }
}
