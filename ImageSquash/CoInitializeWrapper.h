#pragma once 
#include "stdafx.h"
// RAII wrapper for managing the lifetime of the COM library.
template<COINIT T>
class CoInitializeWrapper
{
    const HRESULT _hr;
public:
    CoInitializeWrapper(void)
		:_hr(::CoInitializeEx(nullptr, T))
    {
    }
    ~CoInitializeWrapper()
    {
        if (SUCCEEDED(_hr))
        {
            CoUninitialize();
        }
    }
    operator HRESULT() const
    {
        return _hr;
    }
private:
	CoInitializeWrapper(const CoInitializeWrapper&) = delete;
	CoInitializeWrapper(const CoInitializeWrapper&&) = delete;
	const CoInitializeWrapper& operator = (const CoInitializeWrapper&) = delete;
};