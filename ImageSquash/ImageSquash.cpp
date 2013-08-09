// ImageSquash.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Output.h"
#include "TransformInfo.h"
#include "resource.h"

namespace po = ::boost::program_options;
namespace fs = ::boost::filesystem;
#define E_WORKITEMQUEUEFAIL MAKE_HRESULT(SEVERITY_ERROR, 9, 1);

static HRESULT DownSampleAndConvertImage(const TransformInfo&  info);
static STDMETHODIMP CreateColorContextArray(IWICImagingFactory * factory, IWICColorContext *** toCreate, const UINT count);
static DWORD WriteOutLastError();
static DWORD WriteStdError(LPCWSTR error, size_t length);
static HRESULT inline QueueFileForDownSample(const WIN32_FIND_DATA &file, const std::wstring& inPath, const std::wstring& outPath, const std::wstring& profilePath, const double dpi, const ImageType type, std::vector<TransformInfo> & worklist);

std::wistream& operator>>(std::wistream& in, ImageType & image)
{
    std::wstring token;
    in >> token;
    if (token == L"png")
		image = PNG;
    else if (token == L"jpg")
		image = JPG;
//    else throw boost::program_options::validation_error("Invalid unit");
    return in;
}

/// <summary>program entry point</summary>
int _tmain(int argc, _TCHAR* argv[])
{
	std::cout.imbue(std::locale(""));
	std::wcout.imbue(std::locale(""));
	HRESULT hr = S_OK;
	double dpi = 72.0;
	size_t actuallength = 0;
	ImageType outputType = PNG;
	std::vector<TransformInfo> worklist;
	//HANDLE consoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
	//HANDLE consoleIn = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE standardError = GetStdHandle(STD_ERROR_HANDLE);
	fs::wpath inputPath;
	fs::wpath outputPath;
	std::wstring profilePath;	

	HMODULE module = ::GetModuleHandle(nullptr);
	if (module == NULL)
	{
		WriteOutLastError();
		return -1;
	}

	//DWORD written = 0;
	WCHAR buffer[256] = {0};
	int read = LoadString(module, Logo, buffer, 256);
	std::wcout << buffer << std::endl;

	{

	po::options_description desc("Options");
	desc.add_options()
		("help", "View full help")
		("format,f", po::wvalue<ImageType>()->default_value(PNG, "png"), "output format")
		("outPath,o", po::wvalue<std::wstring>()->required(), "output path")
		("input,i", po::wvalue<std::wstring>()->required(), "input path")
		("dpi", po::wvalue<double>(&dpi)->default_value(72.0, "72.0"), "output dpi");

	po::variables_map vm;
	po::wcommand_line_parser parser(argc, argv);
	parser.options(desc);
	po::store(parser.run(), vm);

	if(vm.count("help"))
	{
		std::cout << desc << std::endl;
		return -1;
	}
	/*if(argc < 2)
	{
		read = ::LoadString(module, Error, &buffer[0], 256);
		::WriteConsole(consoleOut, &buffer[0], read, &written, nullptr);
		return -1;
	}*/

	if(vm.count("input"))
	{
		inputPath = vm["input"].as<std::wstring>();
	}

	if(vm.count("outPath"))
	{
		outputPath = vm["outPath"].as<std::wstring>();
	}

	if(vm.count("format"))
	{
		outputType = vm["format"].as<ImageType>();
	}
	}
	/**************************************************************************
	 * Get the color directory once
	 */
	if (SUCCEEDED(hr))
	{
		WCHAR temp[MAX_PATH] = {0};
		DWORD bufferSize = 0;
		BOOL result = ::GetColorDirectory(nullptr, nullptr, &bufferSize);
		// unfortunately unless we want to handle the pathing semantics we're
		// with 8.3 paths
		if (bufferSize == 0 || bufferSize > sizeof(temp))
		{
			WriteOutLastError();
			hr = E_FAIL;
		}
		::GetColorDirectory(NULL, temp , &bufferSize);
		profilePath = std::wstring(temp);
	}

	{
	// all of this is is unnecessary in C++11 as we would have the <filesystem> header
	WIN32_FIND_DATA file = {0};
	WIN32_FIND_DATA outFile = {0};
	HANDLE searchHandle = ::FindFirstFile(inputPath.c_str(), &file);
	HANDLE outSearchHandle = ::FindFirstFileEx(outputPath.c_str(), FindExInfoBasic, &outFile, FindExSearchNameMatch, nullptr, NULL);
	if(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && outFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		fs::wpath inPathBase = fs::canonical(inputPath.remove_filename());
		fs::wpath outPathBase = fs::canonical(outputPath.remove_filename());

		while(::FindNextFile(searchHandle, &file))
		{
			fs::wpath inPathBuffer(inPathBase);
			inPathBuffer /= file.cFileName;

			fs::wpath outPathBuffer(outPathBase);
			outPathBuffer /= file.cFileName;
			
			hr = QueueFileForDownSample(file, inPathBuffer.wstring(), outPathBuffer.wstring(), profilePath, dpi, outputType, worklist);
			if(!SUCCEEDED(hr))
			{
				break;
			}
		}
	}
	else
	{
		hr = QueueFileForDownSample(file, inputPath.wstring(), outputPath.wstring(), profilePath, dpi, outputType, worklist);
	}

	::FindClose(searchHandle);
	::FindClose(outSearchHandle);
	}

	Concurrency::parallel_for_each(worklist.begin(), worklist.end(),
		[](const TransformInfo & info){
			HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			if (SUCCEEDED(hr))
			{
				DownSampleAndConvertImage(info);
			}

			if (SUCCEEDED(hr))
			{
				::CoUninitialize();
			}
	});

	return hr;
}

/// <summary>Queues and image to be downsampled</summary>
static HRESULT inline QueueFileForDownSample(const WIN32_FIND_DATA &file, const std::wstring& inPath, const std::wstring& outPath, const std::wstring& profilePath, const double dpi, const ImageType type, std::vector<TransformInfo> & worklist)
{
	HRESULT hr = S_OK;
	if(!(file.dwFileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_VIRTUAL | FILE_ATTRIBUTE_TEMPORARY)))
	{
		TransformInfo info(type, inPath, outPath, profilePath, dpi);

		worklist.push_back(info);
	}
	return hr;
}

/// <summary>Down samples and converts an image to the pixel format with the least possible
/// space usage</summary>
/// <remarks>Yes I know this looks like one long chain of spaghetti code, the problem is that MS did
/// a reasonable job designing WIC and I can't really figure out a way to condense it further without 
/// making it a lot less efficient</remarks>
static HRESULT DownSampleAndConvertImage(const TransformInfo&  info)
{
	// stuff that has to be cleaned up
	CComPtr<IWICImagingFactory> pFactory = nullptr;
	CComPtr<IWICBitmapDecoder> pDecoder = nullptr; 
	CComPtr<IWICStream> pStream = nullptr;
	CComPtr<IPropertyBag2> pPropBag = nullptr;
	CComPtr<IWICBitmapFrameDecode> pIDecoderFrame  = nullptr;
	CComPtr<IWICBitmapScaler> pScaler = nullptr;
	CComPtr<IWICColorTransform>  colorTransform = nullptr;
	// annoying but I haven't found a way to get around this problem
	// unfortunately both a vector and an array of CComPtr create problems
	IWICColorContext ** inputContexts = nullptr;	
	IWICColorContext ** outputContexts = nullptr;

	//stuff that doesn't
	IWICBitmapSource * toOutput = nullptr;
	
	double originalDpiX = 0.0, originalDpiY = 0.0;
	UINT sizeX = 0, sizeY = 0, colorCount = 0, finalCount = 0 ,actualContexts = 0;
	BOOL hasAlpha = FALSE, isGreyScale = FALSE, isBlackAndWhite = FALSE, hasPalette = FALSE, isCMYK = FALSE;
	WICPixelFormatGUID inputFormat = { 0 };

	HRESULT hr = ::CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		reinterpret_cast<LPVOID*>(&pFactory)
		);

	if (SUCCEEDED(hr))
	{
		hr = pFactory->CreateStream(&pStream);
	}

	if (SUCCEEDED(hr))
	{
		hr = pStream->InitializeFromFilename(info.InputPath().c_str(), GENERIC_READ);
	}	

	if (SUCCEEDED(hr))
	{
		hr = pFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnLoad, &pDecoder);
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
		hr = pIDecoderFrame->GetColorContexts(0, nullptr, &actualContexts);
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
		fs::wpath finalPath(info.ProfilePath());
		finalPath /= L"RSWOP.icm";
		hr = inputContexts[0]->InitializeFromFilename(finalPath.wstring().c_str());
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
		fs::wpath finalPath(info.ProfilePath());
		finalPath /= L"sRGB Color Space Profile.icm";
		hr = outputContexts[0]->InitializeFromFilename(finalPath.wstring().c_str());
	}

	if (SUCCEEDED(hr) && inputContexts != nullptr)
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

	if (SUCCEEDED(hr) && inputContexts != nullptr)
	{
		toOutput = colorTransform;
	}
	//--------------------- SCALING -----------------------------------------------
	if (SUCCEEDED(hr))
	{
		hr = toOutput->GetResolution(&originalDpiX, &originalDpiY);
	}

	if (SUCCEEDED(hr) && (originalDpiX > info.Dpi() || originalDpiY > info.Dpi()))
	{
		hr = pFactory->CreateBitmapScaler(&pScaler);
	}	

	UINT newSizeX = sizeX, newSizeY = sizeY;
	if (SUCCEEDED(hr) && pScaler)
	{
		newSizeX = (UINT)ceil(((double)sizeX * info.Dpi()) / originalDpiX);
		newSizeY = (UINT)ceil(((double)sizeY * info.Dpi()) / originalDpiY);
		hr = pScaler->Initialize(toOutput, newSizeX, newSizeY, WICBitmapInterpolationModeFant);
	}

	// Change output to scaled
	if (SUCCEEDED(hr) && pScaler)
	{
		toOutput = pScaler;
	}

	if(SUCCEEDED(hr))
	{
		ImageSquash::Output::OutputInfo outputInfo = { pFactory, toOutput, info.Dpi(), newSizeX, newSizeY};
		hr = OutputImage(outputInfo, info.OutputPath(), GUID_ContainerFormatPng);
	}
	
	// cleanup factory
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

	return hr;
}
static DWORD WriteOutLastError()
{
	LPWSTR outBuffer = nullptr;
	size_t read = ::FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		nullptr,
		::GetLastError(),
		0,
		outBuffer,
		BUFSIZ,
		nullptr
		);
	DWORD toReturn = WriteStdError(outBuffer, read);
	::LocalFree(outBuffer);
	return toReturn;
}

static DWORD WriteStdError(LPCWSTR error, size_t length)
{
	HANDLE stdError = ::GetStdHandle(STD_ERROR_HANDLE);
	if (stdError != INVALID_HANDLE_VALUE)
	{
		size_t actualLength = 0;
		::StringCchLength(error, length, &actualLength);
		DWORD written = 0;
		::WriteConsole(stdError, error, (DWORD)actualLength, &written, nullptr);
		return written;
	}
	return 0;
}





static STDMETHODIMP CreateColorContextArray(IWICImagingFactory * factory, IWICColorContext *** toCreate, const UINT count)
{
	HRESULT hr = S_OK;
	*toCreate = new (std::nothrow) IWICColorContext*[count];
	if (*toCreate == nullptr)
	{
		hr = E_OUTOFMEMORY;
		return hr;
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

