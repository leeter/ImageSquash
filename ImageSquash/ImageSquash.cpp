// ImageSquash.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
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
	int read = LoadString(module, Logo, &buffer[0], 256);
	hr = StringCchLength(buffer, 256, &actuallength);
	if(!WriteConsole(consoleOut, buffer, actuallength, &written, NULL))
	{
		WriteOutLastError();
		return -1;
	}

	if(argc < 2)
	{
		read = LoadString(module, Error, &buffer[0], 256);
		WriteConsole(consoleOut, &buffer[0], read, &written, NULL);
	}

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
	for(int i = 1; i < argc; ++i)
	{
		hr = StringCchLength(argv[i], 256, &actuallength);
		if(SUCCEEDED(hr))
		{
			if(CompareStringOrdinal(argv[i], actuallength, L"-b", 2, TRUE) == 0)
			{
				++i;
				if (i < argc)
				{
					if (argv[i] != NULL)
					{
						wchar_t * stopped = NULL;
						double result = 0.0;
						result = wcstod(argv[i + 1], &stopped);
						if (result != HUGE_VAL || result > 0)
						{
							dpi = result;
						}
					}					
				}
			}
		}
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
			PathRenameExtension(outPathBuffer, L".png");
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
	IWICStream *pOutStream = NULL;
	IPropertyBag2 * pPropBag = NULL;
	IWICBitmapFrameEncode * pOutputFrame = NULL;
	IWICBitmapFrameDecode *pIDecoderFrame  = NULL;
	IWICBitmapEncoder *pEncoder = NULL;
	IWICFormatConverter *pConverter = NULL;
	IWICBitmapScaler * pScaler = NULL;
	IWICPalette * pPalette = NULL;
	IWICColorContext ** inputContexts = NULL;
	IWICColorTransform * colorTransform = NULL;
	IWICColorContext ** outputContexts = NULL;

	//stuff that doesn't
	IWICBitmapSource * toOutput = NULL;
	WICPixelFormatGUID inputFormat = { 0 };
	WICPixelFormatGUID outputFormat = GUID_WICPixelFormat32bppBGRA, selectedOutputFormat = GUID_WICPixelFormat32bppBGRA;
	double originalDpiX = 0.0, originalDpiY = 0.0;
	UINT sizeX = 0, sizeY = 0, colorCount = 0, finalCount = 0;;
	BOOL hasAlpha = FALSE, isGreyScale = FALSE, isBlackAndWhite = FALSE, hasPalette = FALSE, isCMYK = FALSE;

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
		hr = pFactory->CreateStream(&pOutStream);
	}

	if (SUCCEEDED(hr))
	{
		hr = pStream->InitializeFromFilename(info->inPath, GENERIC_READ);
	}	

	// Create the output frame
	if (SUCCEEDED(hr))
	{
		hr = pOutStream->InitializeFromFilename(info->outPath, GENERIC_READ | GENERIC_WRITE);
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
	UINT actualContexts = 0;
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


	if (SUCCEEDED(hr))
	{
		if (!IsEqualGUID(inputFormat, GUID_WICPixelFormatBlackWhite))
		{
			hr = pFactory->CreatePalette(&pPalette);

			if (SUCCEEDED(hr))
			{
				hr = HasAlpha(toOutput, pFactory, hasAlpha);
			}

			if (SUCCEEDED(hr))
			{
				hr = pPalette->InitializeFromBitmap(toOutput, 256U, hasAlpha);
			}			

			if (SUCCEEDED(hr))
			{
				hr = pPalette->GetColorCount(&colorCount);
			}

			if (SUCCEEDED(hr))
			{
				hr = pPalette->IsBlackWhite(&isBlackAndWhite);
			}

			if (SUCCEEDED(hr))
			{
				hr = pPalette->IsGrayscale(&isGreyScale);
			}

			if (SUCCEEDED(hr))
			{
				outputFormat = selectedOutputFormat = GetOutputPixelFormat(
					colorCount,
					hasAlpha,
					isBlackAndWhite,
					isGreyScale,
					hasPalette
					);

				if (!hasPalette)
				{
					SafeRelease(&pPalette);
				}
			}
		}
		else
		{
			outputFormat = selectedOutputFormat = inputFormat;
		}
	}
	// if we need to convert the pixel format... we should do so
	if (SUCCEEDED(hr) && !IsEqualGUID(inputFormat, outputFormat))
	{		
		hr = pFactory->CreateFormatConverter(&pConverter);
	}

	if (SUCCEEDED(hr) && pConverter)
	{
		pConverter->Initialize(
			toOutput,
			outputFormat,
			WICBitmapDitherTypeNone,
			pPalette,
			0.0F,
			WICBitmapPaletteTypeCustom
			);
	}

	if (SUCCEEDED(hr) && pConverter)
	{
		toOutput = pConverter;
	}

	if (SUCCEEDED(hr))
	{
		hr = pFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &pEncoder);	
	}

	if (SUCCEEDED(hr))
	{
		hr = pEncoder->Initialize(pOutStream, WICBitmapEncoderNoCache);
	}
	if (SUCCEEDED(hr))
	{
		hr = pEncoder->CreateNewFrame(&pOutputFrame, &pPropBag);
	}

	// this doesn't actually do anything at the moment, but we should keep it around as a sample of
	// how to do it in the future
	if (SUCCEEDED(hr))
	{        
		PROPBAG2 option = { 0 };
		option.pstrName = L"InterlaceOption";
		VARIANT varValue;    
		VariantInit(&varValue);
		varValue.vt = VT_BOOL;
		varValue.boolVal = 0;      
		hr = pPropBag->Write(1, &option, &varValue); 
		if (SUCCEEDED(hr))
		{
			hr = pOutputFrame->Initialize(pPropBag);
		}
	}	

	if (SUCCEEDED(hr))
	{
		hr = pOutputFrame->SetResolution(info->dpi, info->dpi);
	}

	if (SUCCEEDED(hr))
	{
		hr = pOutputFrame->SetSize(newSizeX, newSizeY);
	}

	if (SUCCEEDED(hr))
	{
		hr = pOutputFrame->SetPixelFormat(&outputFormat);
	}

	if (SUCCEEDED(hr))
	{
		hr = IsEqualGUID(outputFormat, selectedOutputFormat) ? S_OK : E_FAIL;
	}
	// disabled as this was adding 4k to the image
	/*if (SUCCEEDED(hr))
	{
	hr = pOutputFrame->SetColorContexts(1, outputContexts);
	}*/

	if (SUCCEEDED(hr))
	{
		hr = pOutputFrame->WriteSource(toOutput, NULL);
	}	

	if (SUCCEEDED(hr))
	{
		hr = pOutputFrame->Commit();
	}

	if (SUCCEEDED(hr))
	{
		hr = pEncoder->Commit();
	}
	// cleanup factory
	SafeRelease(&pFactory);
	SafeRelease(&pDecoder);
	SafeRelease(&pEncoder);
	SafeRelease(&pPropBag);
	SafeRelease(&pOutputFrame);
	SafeRelease(&pStream);
	SafeRelease(&pOutStream);
	SafeRelease(&pIDecoderFrame);
	SafeRelease(&pConverter);
	SafeRelease(&pScaler);
	SafeRelease(&pPalette);
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

static WICPixelFormatGUID _stdcall GetOutputPixelFormat(UINT colorCount, BOOL hasAlpha, BOOL isBlackAndWhite, BOOL isGreyScale, BOOL & hasPalette)
{
	if (isBlackAndWhite)
	{
		return GUID_WICPixelFormatBlackWhite;
	}
	else if (!hasAlpha)
	{
		if (colorCount < 3U)
		{
			hasPalette = TRUE;
			return GUID_WICPixelFormat1bppIndexed;
		}
		if (colorCount < 5U)
		{
			hasPalette = TRUE;
			return GUID_WICPixelFormat2bppIndexed;					
		}
		else if (colorCount < 17U)
		{
			hasPalette = TRUE;
			return GUID_WICPixelFormat4bppIndexed;					
		}
		// while it would be convenient to check to see if they have less that 257 colors
		// unfortunately the way the palette system works this would result in all images
		// without alpha being caught in this logical trap. So until I find a better way
		// to deal with it... it will have to have 255 colors or less to end up in the 8bpp
		// range.
		else if (colorCount < 256U)
		{
			hasPalette = TRUE;
			return GUID_WICPixelFormat8bppIndexed;
		}
		else
		{
			return GUID_WICPixelFormat24bppBGR;
		}
	}
	return GUID_WICPixelFormat32bppBGRA;
}

static HRESULT _stdcall HasAlpha(IWICBitmapSource * source, IWICImagingFactory * factory, BOOL & hasAlpha)
{
	// stuff that has to be cleaned up
	IWICFormatConverter * converter = NULL;
	IWICBitmap * bitmap = NULL;
	IWICBitmapLock * lock = NULL;

	// stuff that doesn't
	WICInProcPointer buffer = NULL;
	UINT * bitmapPixels = NULL;
	IWICBitmapSource * finalSource = NULL;	
	WICPixelFormatGUID inputFormat = { 0 };	
	UINT sizeX = 0, sizeY = 0, bufferSize = 0;

	HRESULT hr = source->GetPixelFormat(&inputFormat);	
	hasAlpha = false;
	if (SUCCEEDED(hr))
	{
		if (IsEqualGUID(inputFormat, GUID_WICPixelFormat32bppBGRA) ||
			IsEqualGUID(inputFormat, GUID_WICPixelFormat32bppPBGRA) ||
			IsEqualGUID(inputFormat, GUID_WICPixelFormat32bppRGBA) ||
			IsEqualGUID(inputFormat, GUID_WICPixelFormat32bppPRGBA))
		{
			if (IsEqualGUID(inputFormat, GUID_WICPixelFormat32bppBGRA))
			{
				finalSource = source;
			}
			else
			{
				hr = factory->CreateFormatConverter(&converter);
				if (SUCCEEDED(hr))
				{
					hr = converter->Initialize(
						source,
						GUID_WICPixelFormat32bppBGRA,
						WICBitmapDitherTypeNone,
						NULL,
						0.0,
						WICBitmapPaletteTypeCustom
						);

				}

				if (SUCCEEDED(hr))
				{
					finalSource = converter;
				}				
			}

			if (SUCCEEDED(hr))
			{
				hr = factory->CreateBitmapFromSource(finalSource, WICBitmapCacheOnDemand, &bitmap);
			}

			if (SUCCEEDED(hr))
			{
				hr = bitmap->GetSize(&sizeX, &sizeY);
			}

			if (SUCCEEDED(hr))
			{
				WICRect lockRectangle = { 0, 0, sizeX, sizeY};
				hr = bitmap->Lock(&lockRectangle, WICBitmapLockRead, &lock);
			}

			if (SUCCEEDED(hr))
			{
				hr = lock->GetDataPointer(&bufferSize, &buffer);				
			}

			if(SUCCEEDED(hr))
			{
				//bitmapPixels = (UINT*)&buffer[0];
				size_t i = bufferSize;
				do
				{
					i -= 4;
					if(buffer[i] != 0xFFU)
					{
						hasAlpha = TRUE;
						break;
					}
				}while(i);
			}

			SafeRelease(&converter);
			SafeRelease(&lock);
			SafeRelease(&bitmap);
		}
	}

	return hr;
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