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
#define STRSAFE_USE_SECURE_CRT 1
#define NO_SHLWAPI_STRFCNS 1
#define NO_SHLWAPI_REG 1
#define NO_SHLWAPI_STREAM 1
#define NO_SHLWAPI_HTTP 1
#define NO_SHLWAPI_ISOS 1
#define NO_SHLWAPI_GDI 1
#include "targetver.h"
#include <Windows.h>
#include <Icm.h>
#include <wincodec.h>
#include <new>
#include <vector>
#include <memory>
#include <string>
#include <atlbase.h>
#include <Shlwapi.h>
#include <cmath>
#include <strsafe.h>
#include "banned.h"

template <class T> __forceinline static void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}
// TODO: reference additional headers your program requires here
