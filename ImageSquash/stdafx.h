// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#define NOSERVICE
#define NOMCX
#define NOIME
#define NOTAPE
#define NOSOUND
#define NORPC
#define NODRAWTEXT
#define NOICONS
#define NOMB
#define NOMETAFILE
#define NOWINABLE
#define NOCLIPBOARD
#define NORASTEROPS
#define MMNOMMSYSTEM
#define MMNOSOUND
#define MMNODRV
#define STRICT
#define USE_STRICT_CONST
#define STRSAFE_USE_SECURE_CRT 1
#define NO_SHLWAPI_STRFCNS 1
#define NO_SHLWAPI_REG 1
#define NO_SHLWAPI_STREAM 1
#define NO_SHLWAPI_HTTP 1
#define NO_SHLWAPI_ISOS 1
#define NO_SHLWAPI_GDI 1
//#define BOOST_PROGRAM_OPTIONS_NO_LIB
//#define BOOST_ALL_NO_LIB
#include "targetver.h"
#include <Windows.h>
#pragma warning (push)
#pragma warning (disable: 4005)
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <locale>
#include <Icm.h>
#include <wincodec.h>
#include <array>
#include <new>
#include <vector>
#include <memory>
#include <string>
#include <atlbase.h>
#include <cmath>
#include <ppl.h>
#include <strsafe.h>
#include "banned.h"
#pragma warning (pop)

template <class T> __forceinline static void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}
// TODO: reference additional headers your program requires here
