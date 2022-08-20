#include "pch.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::desktop;
}

int __stdcall wmain()
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // 

    return 0;
}
