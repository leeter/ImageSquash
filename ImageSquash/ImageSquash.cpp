// ImageSquash.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Output.h"
#include "resource.h"

#define E_WORKITEMQUEUEFAIL MAKE_HRESULT(SEVERITY_ERROR, 9, 1);
struct{
	WCHAR inPath[MAX_PATH];
	WCHAR outPath[MAX_PATH];
	double dpi;
	HRESULT hr;
	LPCWSTR profilePath;
} typedef TransformInfo;

static STDMETHODIMP DownSampleAndConvertImage(TransformInfo * info);
static WICPixelFormatGUID _stdcall GetOutputPixelFormat(UINT colorCount, BOOL hasAlpha, BOOL isBlackAndWhite, BOOL isGreyScale, BOOL & hasPalette);
static STDMETHODIMP HasAlpha(IWICBitmapSource * source, IWICImagingFactory * factory, BOOL & hasAlpha);
static STDMETHODIMP CreateColorContextArray(IWICImagingFactory * factory, IWICColorContext *** toCreate, UINT count);
static DWORD _stdcall WriteOutLastError();
static DWORD _stdcall WriteStdError(LPCWSTR error, size_t length);
static void CALLBACK DownSampleThread(
	__inout      PTP_CALLBACK_INSTANCE Instance,
	__inout_opt  PVOID Context,
	__inout      PTP_WORK Work
	);
static STDMETHODIMP QueueFileForDownSample(WIN32_FIND_DATA &file, LPCWSTR inPath, LPCWSTR outPath, LPCWSTR profilePath, double dpi, std::vector<PTP_WORK> & worklist);
static BOOL STDMETHODCALLTYPE IsPixelFormatRGBWithAlpha(WICPixelFormatGUID pixelFormat);

/// <summary>program entry point</summary>
int _tmain(int argc, _TCHAR* argv[])
{
	WCHAR profilePath[MAX_PATH];

	HRESULT hr = S_OK;
	double dpi = 72.0;
	size_t actuallength = 0;	
	std::vector<PTP_WORK> worklist;
	HANDLE consoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE consoleIn = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE standardError = GetStdHandle(STD_ERROR_HANDLE);

	if(consoleOut == NULL)
	{
		return -1;
	}

	HMODULE module = GetModuleHandle(NULL);
	if (module == NULL)
	{
		WriteOutLastError();
		return -1;
	}

	DWORD written = 0;
	WCHAR buffer[256];
	int read = LoadString(module, Logo, buffer, 256);
	if(!WriteConsole(consoleOut, buffer, read, &written, NULL))
	{
		WriteOutLastError();
		return -1;
	}

	for(int i = argc - 1; i; --i)
	{
		hr = StringCchLength(argv[i], 256, &actuallength);
		if(!SUCCEEDED(hr) || actuallength > MAX_PATH)
		{
			return hr;
		}
		if(CompareStringOrdinal(argv[i], actuallength, L"-?", 2, TRUE) == CSTR_EQUAL ||
			CompareStringOrdinal(argv[i], actuallength, L"/?", 2, TRUE) == CSTR_EQUAL)
		{
			
			read = LoadString(module, HelpString, buffer , 256);			
			WriteConsole(consoleOut, buffer, read, &written, NULL);
			return 0;
		}
	}

	if(argc < 2)
	{
		read = LoadString(module, Error, &buffer[0], 256);
		WriteConsole(consoleOut, &buffer[0], read, &written, NULL);
		return -1;
	}

	

	/******************************************************************************
	* Get the DPI if it's set at the command line
	*/
	for(int i = 1; i < argc; ++i)
	{
		if(CompareStringOrdinal(argv[i], actuallength, L"-dpi", 4, TRUE) == CSTR_EQUAL)
		{
			++i;
			if (i > argc)
			{
				return E_INVALIDARG;		
			}
			if (argv[i] != NULL)
			{
				wchar_t * stopped = NULL;
				double result = 0.0;
				result = wcstod(argv[i], &stopped);
				if (result != HUGE_VAL || result > 0)
				{
					dpi = result;
				}
			}		
		}
	}

	/**************************************************************************
	 * Get the color directory once
	 */
	if (SUCCEEDED(hr))
	{
		DWORD bufferSize = 0;
		BOOL result = GetColorDirectory(NULL, NULL, &bufferSize);
		// unfortunately unless we want to handle the pathing semantics we're
		// with 8.3 paths
		if (bufferSize == 0 || bufferSize > MAX_PATH)
		{
			WriteOutLastError();
			hr = E_FAIL;
		}
		GetColorDirectory(NULL, profilePath , &bufferSize);
	}

	WIN32_FIND_DATA file = {0};
	WIN32_FIND_DATA outFile = {0};
	HANDLE searchHandle = FindFirstFile(argv[1], &file);
	HANDLE outSearchHandle = FindFirstFileEx(argv[2], FindExInfoBasic, &outFile, FindExSearchNameMatch, NULL, NULL);
	if(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && outFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		WCHAR inPathBase[MAX_PATH];
		WCHAR outPathBase[MAX_PATH];
		PathRemoveFileSpec(argv[1]);
		PathRemoveFileSpec(argv[2]);
		GetFullPathName(argv[1], MAX_PATH, inPathBase, NULL);
		GetFullPathName(argv[2], MAX_PATH, outPathBase, NULL);
		while(FindNextFile(searchHandle, &file))
		{
			WCHAR inPathBuffer[MAX_PATH];
			WCHAR outPathBuffer[MAX_PATH];

			PathCombine(inPathBuffer, inPathBase, file.cFileName);
			PathCombine(outPathBuffer, outPathBase, file.cFileName);
			
			hr = QueueFileForDownSample(file, inPathBuffer, outPathBuffer, profilePath, dpi, worklist);
			if(!SUCCEEDED(hr))
			{
				break;
			}
		}
	}
	else
	{
		hr = QueueFileForDownSample(file, argv[1], argv[2], profilePath, dpi, worklist);
	}

	for (size_t waitCount = worklist.size(); waitCount; --waitCount)
	{
		size_t index = waitCount - 1;
		WaitForThreadpoolWorkCallbacks(worklist[index], FALSE);
		CloseThreadpoolWork(worklist[index]);
	}
	FindClose(searchHandle);
	FindClose(outSearchHandle);

	return hr;
}

/// <summary>Queues and image to be downsampled</summary>
static STDMETHODIMP QueueFileForDownSample(WIN32_FIND_DATA &file, LPCWSTR inPath, LPCWSTR outPath, LPCWSTR profilePath, double dpi, std::vector<PTP_WORK> & worklist)
{
	HRESULT hr = S_OK;
	if(!(file.dwFileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_VIRTUAL | FILE_ATTRIBUTE_TEMPORARY)))
	{
		PTP_WORK work = NULL;
		TransformInfo * info = new TransformInfo();
		if(info == NULL)
		{
			hr = E_OUTOFMEMORY;
		}
		StringCchCopy(info->inPath, MAX_PATH, inPath);
		StringCchCopy(info->outPath, MAX_PATH, outPath);
		info->dpi = dpi;
		info->hr = S_OK;
		info->profilePath = profilePath;

		work = CreateThreadpoolWork(DownSampleThread, info, NULL);
		// unable to create the work item
		if(work == NULL)
		{
			WriteOutLastError();
			return E_WORKITEMQUEUEFAIL;
		}
		worklist.push_back(work);
		SubmitThreadpoolWork(work);
	}
	return hr;
}

static void CALLBACK DownSampleThread(
	__inout      PTP_CALLBACK_INSTANCE Instance,
	__inout_opt  PVOID Context,
	__inout      PTP_WORK Work
	)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (SUCCEEDED(hr))
	{
		TransformInfo * info = reinterpret_cast<TransformInfo*>(Context);
		DownSampleAndConvertImage(info);
	}

	if (SUCCEEDED(hr))
	{
		CoUninitialize();
	}
}

/// <summary>Down samples and converts an image to the pixel format with the least possible
/// space usage</summary>
/// <remarks>Yes I know this looks like one long chain of spaghetti code, the problem is that MS did
/// a reasonable job designing WIC and I can't really figure out a way to condense it further without 
/// making it a lot less efficient</remarks>
static STDMETHODIMP DownSampleAndConvertImage(TransformInfo * info)
{
	// stuff that has to be cleaned up
	IWICImagingFactory *pFactory = NULL;
	IWICBitmapDecoder *pDecoder = NULL; 
	IWICStream *pStream = NULL;
	IPropertyBag2 * pPropBag = NULL;
	IWICBitmapFrameDecode *pIDecoderFrame  = NULL;
	IWICBitmapScaler * pScaler = NULL;
	IWICColorContext ** inputContexts = NULL;
	IWICColorTransform * colorTransform = NULL;
	IWICColorContext ** outputContexts = NULL;

	//stuff that doesn't
	IWICBitmapSource * toOutput = NULL;
	
	double originalDpiX = 0.0, originalDpiY = 0.0;
	UINT sizeX = 0, sizeY = 0, colorCount = 0, finalCount = 0 ,actualContexts = 0;
	BOOL hasAlpha = FALSE, isGreyScale = FALSE, isBlackAndWhite = FALSE, hasPalette = FALSE, isCMYK = FALSE;
	WICPixelFormatGUID inputFormat = { 0 };

	HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		(LPVOID*)&pFactory
		);

	if (SUCCEEDED(hr))
	{
		hr = pFactory->CreateStream(&pStream);
	}

	if (SUCCEEDED(hr))
	{
		hr = pStream->InitializeFromFilename(info->inPath, GENERIC_READ);
	}	

	if (SUCCEEDED(hr))
	{
		hr = pFactory->CreateDecoderFromStream(pStream, NULL, WICDecodeMetadataCacheOnLoad, &pDecoder);
	}


	if (SUCCEEDED(hr))
	{
		hr = pDecoder->GetFrame(0, &pIDecoderFrame);
	}

	if (SUCCEEDED(hr))
	{
		hr = pIDecoderFrame->GetPixelFormat(&inputFormat);
	}

	if (SUCCEEDED(hr))
	{
		isCMYK = InlineIsEqualGUID(GUID_WICPixelFormat32bppCMYK, inputFormat) || 
			InlineIsEqualGUID(GUID_WICPixelFormat40bppCMYKAlpha, inputFormat) ||
			InlineIsEqualGUID(GUID_WICPixelFormat64bppCMYK, inputFormat) ||
			InlineIsEqualGUID(GUID_WICPixelFormat80bppCMYKAlpha, inputFormat);
	}

	if (SUCCEEDED(hr))
	{
		hr = pIDecoderFrame->GetSize(&sizeX, &sizeY);
	}
	// ------------------------- Color -------------------------------------------

	if (SUCCEEDED(hr))
	{		
		toOutput = pIDecoderFrame;
		hr = pIDecoderFrame->GetColorContexts(0, NULL, &actualContexts);
	}

	if(SUCCEEDED(hr) && (actualContexts || isCMYK))
	{
		// if we don't have any contexts but we're CMYK we need to have a profile anyway
		UINT contextsToCreate = !actualContexts && isCMYK ? 1 : actualContexts;
		hr = CreateColorContextArray(pFactory, &inputContexts, contextsToCreate);
	}	

	if (SUCCEEDED(hr) && actualContexts > 0)
	{		
		hr= pIDecoderFrame->GetColorContexts(actualContexts, inputContexts, &finalCount);
	}



	if (SUCCEEDED(hr) && !actualContexts && isCMYK)
	{		
		WCHAR finalPath[MAX_PATH];
		PathCombine(finalPath, info->profilePath, L"RSWOP.icm");
		hr = inputContexts[0]->InitializeFromFilename(finalPath);
	}

	if (SUCCEEDED(hr) && finalCount)
	{
		hr = pFactory->CreateColorTransformer(&colorTransform);
	}

	if (SUCCEEDED(hr))
	{
		hr = CreateColorContextArray(pFactory, &outputContexts, 1);
	}

	if(SUCCEEDED(hr))
	{
		WCHAR finalPath[MAX_PATH];
		PathCombine(finalPath, info->profilePath, L"sRGB Color Space Profile.icm");
		hr = outputContexts[0]->InitializeFromFilename(finalPath);
	}

	if (SUCCEEDED(hr) && inputContexts != NULL)
	{
		for(UINT i = 0 ;i < actualContexts; ++i)
		{
			WICColorContextType type = WICColorContextUninitialized;
			inputContexts[i]->GetType(&type);

			hr = colorTransform->Initialize(toOutput, inputContexts[i], outputContexts[0], GUID_WICPixelFormat32bppBGRA);
			if(SUCCEEDED(hr))
			{
				break;
			}
		}
	}

	if (SUCCEEDED(hr) && inputContexts != NULL)
	{
		toOutput = colorTransform;
	}
	//--------------------- SCALING -----------------------------------------------
	if (SUCCEEDED(hr))
	{
		hr = toOutput->GetResolution(&originalDpiX, &originalDpiY);
	}

	if (SUCCEEDED(hr) && (originalDpiX > info->dpi || originalDpiY > info->dpi))
	{
		hr = pFactory->CreateBitmapScaler(&pScaler);
	}	

	UINT newSizeX = sizeX, newSizeY = sizeY;
	if (SUCCEEDED(hr) && pScaler)
	{
		newSizeX = (UINT)ceil(((double)sizeX * info->dpi) / originalDpiX);
		newSizeY = (UINT)ceil(((double)sizeY * info->dpi) / originalDpiY);
		hr = pScaler->Initialize(toOutput, newSizeX, newSizeY, WICBitmapInterpolationModeFant);
	}

	// Change output to scaled
	if (SUCCEEDED(hr) && pScaler)
	{
		toOutput = pScaler;
	}

	if(SUCCEEDED(hr))
	{
		hr = OutputImage(pFactory, toOutput, info->outPath, GUID_ContainerFormatPng, info->dpi, newSizeX, newSizeY);
	}
	
	// cleanup factory
	SafeRelease(&pFactory);
	SafeRelease(&pDecoder);
	SafeRelease(&pPropBag);
	SafeRelease(&pStream);
	SafeRelease(&pIDecoderFrame);
	SafeRelease(&pScaler);
	SafeRelease(&colorTransform);
	for(UINT i = 0; i < actualContexts; ++i)
	{
		SafeRelease(&inputContexts[i]);
	}

	if (inputContexts)
	{
		delete [] inputContexts;
	}

	// there should never be more than one output context
	if(outputContexts)
	{
		SafeRelease(&outputContexts[0]);
		delete [] outputContexts;
	}
	if(info)
	{
		delete info;
	}

	return hr;
}
static DWORD _stdcall WriteOutLastError()
{
	LPWSTR outBuffer = NULL;
	size_t read = FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		NULL,
		GetLastError(),
		0,
		outBuffer,
		BUFSIZ,
		NULL
		);
	DWORD toReturn = WriteStdError(outBuffer, read);
	LocalFree(outBuffer);
	return toReturn;
}

static DWORD _stdcall WriteStdError(LPCWSTR error, size_t length)
{
	HANDLE stdError = GetStdHandle(STD_ERROR_HANDLE);
	if (stdError != INVALID_HANDLE_VALUE)
	{
		size_t actualLength = 0;
		StringCchLength(error, length, &actualLength);
		DWORD written = 0;
		WriteConsole(stdError, error, (DWORD)actualLength, &written, NULL);
		return written;
	}
	return 0;
}





static STDMETHODIMP CreateColorContextArray(IWICImagingFactory * factory, IWICColorContext *** toCreate, UINT count)
{
	HRESULT hr = S_OK;
	*toCreate = new IWICColorContext*[count];
	if (*toCreate == NULL)
	{
		hr = E_OUTOFMEMORY;
	}

	if (SUCCEEDED(hr))
	{
		for(UINT i = 0; i < count; ++i)
		{
			if (SUCCEEDED(hr))
			{
				hr = factory->CreateColorContext(&(*toCreate)[i]);
			}
		}
	}
	return hr;
}

