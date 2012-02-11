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
#define NOGDI
#define NOCOLOR
#define NODRAWTEXT
#define NOICONS
#define NOWINMESSAGES
#define NOMB
#define NOMETAFILE
#define NOWINABLE
#define NOCLIPBOARD
#define MMNOMMSYSTEM
#define MMNOSOUND
#define MMNODRV
#define STRICT
#include "targetver.h"
#include <Windows.h>
#include <tchar.h>
#include <wincodec.h>
#include <math.h>
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
