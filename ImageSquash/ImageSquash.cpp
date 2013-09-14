// ImageSquash.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Output.h"
#include "TransformInfo.h"
#include "resource.h"
#include "CoInitializeWrapper.h"
#include <boost\scope_exit.hpp>

namespace po = ::boost::program_options;
namespace fs = ::boost::filesystem;
namespace wrl = ::Microsoft::WRL;
#define E_WORKITEMQUEUEFAIL MAKE_HRESULT(SEVERITY_ERROR, 9, 1);

namespace{

/// <summary> Creates a <see cref="IWICColorContext" /> array of pointers </summary>
static std::vector<wrl::ComPtr<IWICColorContext>> CreateColorContextArray(wrl::ComPtr<IWICImagingFactory> const &factory, const UINT count)
{
	std::vector<wrl::ComPtr<IWICColorContext>> toCreate(count);

	for(UINT i = 0; i < count; ++i)
	{
		_com_util::CheckError(factory->CreateColorContext(&(toCreate)[i]));
	}
	return toCreate;
}

/// <summary>Down samples and converts an image to the pixel format with the least possible
/// space usage</summary>
/// <remarks>Yes I know this looks like one long chain of spaghetti code, the problem is that MS did
/// a reasonable job designing WIC and I can't really figure out a way to condense it further without 
/// making it a lot less efficient</remarks>
static HRESULT DownSampleAndConvertImage(const TransformInfo&  info)
{
	// stuff that has to be cleaned up
	wrl::ComPtr<IWICImagingFactory> pFactory;
	wrl::ComPtr<IWICBitmapDecoder> pDecoder; 
	wrl::ComPtr<IWICStream> pStream;
	wrl::ComPtr<IPropertyBag2> pPropBag;
	wrl::ComPtr<IWICBitmapFrameDecode> pIDecoderFrame;
	wrl::ComPtr<IWICBitmapScaler> pScaler;
	wrl::ComPtr<IWICColorTransform>  colorTransform;
	wrl::ComPtr<IWICColorContext> outputContext;
	// annoying but I haven't found a way to get around this problem
	// unfortunately both a vector and an array of CComPtr create problems
	//std::unique_ptr<IWICColorContext*[]> inputContexts = nullptr;
	std::vector<wrl::ComPtr<IWICColorContext>> inputContexts;


	//stuff that doesn't
	wrl::ComPtr<IWICBitmapSource> toOutput;
	
	double originalDpiX = 0.0, originalDpiY = 0.0;
	UINT sizeX = 0, sizeY = 0, colorCount = 0, finalCount = 0 ,actualContexts = 0;
	BOOL hasAlpha = FALSE, isGreyScale = FALSE, isBlackAndWhite = FALSE, hasPalette = FALSE, isCMYK = FALSE;
	WICPixelFormatGUID inputFormat = { 0 };

	HRESULT hr = ::CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pFactory));
	if(FAILED(hr))
		return hr;

	_com_util::CheckError(pFactory->CreateStream(&pStream));

	_com_util::CheckError(pStream->InitializeFromFilename(info.InputPath().c_str(), GENERIC_READ));

	// this will fail if the file isn't an image we can handle
	hr = pFactory->CreateDecoderFromStream(pStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &pDecoder);
	if(FAILED(hr))
		return hr;

	_com_util::CheckError(pDecoder->GetFrame(0, &pIDecoderFrame));
	

	_com_util::CheckError(pIDecoderFrame->GetPixelFormat(&inputFormat));

	isCMYK = InlineIsEqualGUID(GUID_WICPixelFormat32bppCMYK, inputFormat) || 
			InlineIsEqualGUID(GUID_WICPixelFormat40bppCMYKAlpha, inputFormat) ||
			InlineIsEqualGUID(GUID_WICPixelFormat64bppCMYK, inputFormat) ||
			InlineIsEqualGUID(GUID_WICPixelFormat80bppCMYKAlpha, inputFormat);

	_com_util::CheckError(pIDecoderFrame->GetSize(&sizeX, &sizeY));

	// ------------------------- Color -------------------------------------------

	toOutput = pIDecoderFrame;
	_com_util::CheckError(pIDecoderFrame->GetColorContexts(0, nullptr, &actualContexts));

	if(actualContexts || isCMYK)
	{
		// if we don't have any contexts but we're CMYK we need to have a profile anyway
		UINT contextsToCreate = !actualContexts && isCMYK ? 1 : actualContexts;
		inputContexts = CreateColorContextArray(pFactory, contextsToCreate);
	}	

	if (actualContexts > 0)
	{		
		_com_util::CheckError(pIDecoderFrame->GetColorContexts(actualContexts, inputContexts[0].GetAddressOf(), &finalCount));
	}

	if (!actualContexts && isCMYK)
	{
		fs::path finalPath(info.ProfilePath());
		finalPath /= L"RSWOP.icm";
		_com_util::CheckError(inputContexts[0]->InitializeFromFilename(finalPath.wstring().c_str()));
	}

	if (finalCount)
	{
		_com_util::CheckError(pFactory->CreateColorTransformer(&colorTransform));
	}

	_com_util::CheckError(pFactory->CreateColorContext(&outputContext));

	{
		fs::path finalPath(info.ProfilePath());
		finalPath /= L"sRGB Color Space Profile.icm";
		_com_util::CheckError(outputContext->InitializeFromFilename(finalPath.c_str()));
	}

	if (!inputContexts.empty())
	{
		for(UINT i = 0 ;i < actualContexts; ++i)
		{
			WICColorContextType type = WICColorContextUninitialized;
			inputContexts[i]->GetType(&type);

			hr = colorTransform->Initialize(toOutput.Get(), inputContexts[i].Get(), outputContext.Get(), GUID_WICPixelFormat32bppBGRA);
			if(SUCCEEDED(hr))
			{
				break;
			}
		}
	}

	if (!inputContexts.empty())
	{
		toOutput = colorTransform;
	}
	//--------------------- SCALING -----------------------------------------------

	_com_util::CheckError(toOutput->GetResolution(&originalDpiX, &originalDpiY));

	if (originalDpiX > info.Dpi() || originalDpiY > info.Dpi())
	{
		_com_util::CheckError(pFactory->CreateBitmapScaler(&pScaler));
	}	

	UINT newSizeX = sizeX, newSizeY = sizeY;
	if (pScaler)
	{
		newSizeX = static_cast<UINT>(std::ceil((static_cast<double>(sizeX) * info.Dpi()) / originalDpiX));
		newSizeY = static_cast<UINT>(std::ceil((static_cast<double>(sizeY) * info.Dpi()) / originalDpiY));
		_com_util::CheckError(pScaler->Initialize(toOutput.Get(), newSizeX, newSizeY, WICBitmapInterpolationModeFant));
		toOutput = pScaler;
	}

	// Change output to scaled
	{
		std::unique_ptr<ImageSquash::Output::outputImage> output = 
			ImageSquash::Output::outputImage::CreateOutputImage(
			newSizeX,
			newSizeY,
			info.type(),
			pFactory,
			info.Dpi());
		output->write(toOutput.Get(), info.OutputPath());
	}

	return hr;
}
static void WriteOutLastError()
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
	BOOST_SCOPE_EXIT_ALL(&){ ::LocalFree(outBuffer); };
	std::wcerr<< outBuffer <<std::endl;
}

}

// for some reason we can't put this in an anonymous namespace it breaks complication
// but the static keywork works
static std::wistream& operator>>(std::wistream& in, ImageType & image)
{
    std::wstring token;
    in >> token;
    if (token == L"png")
		image = PNG;
    else if (token == L"jpg")
		image = JPG;
	else 
		throw po::validation_error(po::validation_error::invalid_option_value);
    return in;
}


/// <summary>program entry point</summary>
int wmain(int argc, wchar_t* argv[])
{
	std::cout.imbue(std::locale(""));
	std::wcout.imbue(std::locale(""));
	HRESULT hr = S_OK;
	double dpi = 72.0;
	size_t actuallength = 0;
	ImageType outputType = PNG;
	std::vector<TransformInfo> worklist;
	
	fs::path inputPath;
	fs::path outputPath;
	std::wstring profilePath;

	{
		//DWORD written = 0;
		WCHAR buffer[256] = {0};
		int read = ::LoadString(nullptr, Logo, buffer, 256);
		std::wcout << buffer << std::endl;
	}
	{

		po::options_description desc("Options");
		desc.add_options()
			("help,?", "View full help")
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
		::GetColorDirectory(nullptr, temp , &bufferSize);
		profilePath = temp;
	}

	if(fs::is_directory(inputPath) && fs::is_directory(outputPath))// file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && outFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		fs::path inPathBase = fs::canonical(inputPath.remove_filename());
		fs::path outPathBase = fs::canonical(outputPath.remove_filename());

		fs::directory_iterator endItr;
		for(fs::directory_iterator inputItr(inputPath); inputItr != endItr; inputItr++)
		{
			fs::path fileName = inputItr->path().filename();
			fs::path inPathBuffer(inPathBase);
			inPathBuffer /= fileName;

			fs::path outPathBuffer(outPathBase);
			outPathBuffer /= fileName;
#if __cplusplus > 199711L
			worklist.emplace_back(outputType, inPathBuffer.wstring(), outPathBuffer.wstring(), profilePath, dpi);
#else
			TransformInfo info(outputType, inPathBuffer.wstring(), outPathBuffer.wstring(), profilePath, dpi);
			worklist.push_back(info);
#endif
		}
	}
	else
	{
#if __cplusplus > 199711L
		worklist.emplace_back(outputType, inputPath.wstring(), outputPath.wstring(), profilePath, dpi);
#else
		TransformInfo info(outputType, inputPath.wstring(), outputPath.wstring(), profilePath, dpi);
		worklist.push_back(info);
#endif
	}

	Concurrency::parallel_for_each(worklist.begin(), worklist.end(),
		[](const TransformInfo & info){
			CoInitializeWrapper<COINIT_MULTITHREADED> wrapper;
			if (SUCCEEDED(wrapper))
			{
				try{
				DownSampleAndConvertImage(info);
				}catch(_com_error& ex){

					std::wcerr.setf(std::ios::showbase);
					std::wcerr<< 
						L"Unable to convert file: \n" <<
						info.InputPath() <<
						L"\nHRESULT: " <<
						std::hex << ex.Error() <<
						L"\nMessage:\n"<< ex.ErrorMessage() << std::endl;
					std::wcerr.unsetf(std::ios::showbase);
				}
			}
	});

	return hr;
}