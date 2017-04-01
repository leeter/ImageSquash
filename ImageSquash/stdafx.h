// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
//#define NOGDICAPMASKS   
#define NOVIRTUALKEYCODE
//#define NOWINMESSAGES   
#define NOWINSTYLES     
#define NOSYSMETRICS    
#define NOMENUS         
#define NOICONS         
#define NOKEYSTATES     
#define NOSYSCOMMANDS   
#define NORASTEROPS     
#define NOSHOWWINDOW    
#define OEMRESOURCE     
#define NOATOM          
#define NOCLIPBOARD     
#define NOCOLOR         
#define NOCTLMGR        
#define NODRAWTEXT      
//#define NOGDI           
#define NOKERNEL        
//#define NOUSER          
//#define NONLS           
#define NOMB            
#define NOMEMMGR        
#define NOMETAFILE      
#define NOMINMAX        
//#define NOMSG           
#define NOOPENFILE      
#define NOSCROLL        
#define NOSERVICE       
#define NOSOUND         
//#define NOTEXTMETRIC    
#define NOWH            
#define NOWINOFFSETS    
#define NOCOMM          
#define NOKANJI         
#define NOHELP          
#define NOPROFILER
#define NOIME
#define NODEFERWINDOWPOS
#define NOMCX
#define STRICT
#define USE_STRICT_CONST
#define STRSAFE_USE_SECURE_CRT 1
#define NO_SHLWAPI_STRFCNS 1
#define NO_SHLWAPI_REG 1
#define NO_SHLWAPI_STREAM 1
#define NO_SHLWAPI_HTTP 1
#define NO_SHLWAPI_ISOS 1
#define NO_SHLWAPI_GDI 1
#define BOOST_FILESYSTEM_NO_DEPRECATED
//#define BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION
//#define BOOST_PROGRAM_OPTIONS_NO_LIB
//#define BOOST_ALL_NO_LIB
#include "targetver.h"
#include <Windows.h>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <locale>
#include <Icm.h>
#include <wincodec.h>
#include <new>
#include <vector>
#include <utility>
#include <memory>
#include <string>
#include <cstdint>
#include <codecvt>
#include <comdef.h>
#include <cmath>
#include <wrl.h>
#include <ppl.h>
//#include <strsafe.h>
#include "banned.h"

//template <class T> __forceinline static void SafeRelease(T **ppT)
//{
//    if (*ppT)
//    {
//        (*ppT)->Release();
//        *ppT = nullptr;
//    }
//}
// TODO: reference additional headers your program requires here
