#pragma once
#include <type_traits>
#include <winrt/base.h>

namespace is::impl {
    template <typename T, typename F, typename...Args>
    int32_t capture_to(T** result, F function, Args&& ...args)
    {
        return function(args..., result);
    }

    template <typename T, typename O, typename M, typename...Args, std::enable_if_t<std::is_class_v<O> || std::is_union_v<O>, int> = 0>
    int32_t capture_to(T** result, O * object, M method, Args&& ...args)
    {
        return (object->*method)(args..., result);
    }

    template <typename T, typename O, typename M, typename...Args>
    int32_t capture_to(T** result, winrt::com_ptr<O> const& object, M method, Args&& ...args)
    {
        return (object.get()->*(method))(args..., result);
    }
}

namespace is {
    template <typename T, typename...Args>
    winrt::impl::com_ref<T> capture(Args&& ...args)
    {
        T* result{};
        winrt::check_hresult(impl::capture_to<T>(&result, std::forward<Args>(args)...));
        return { result, winrt::take_ownership_from_abi };
    }

}