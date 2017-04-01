// ImageSquash.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Output.h"
#include "TransformInfo.h"
#include "resource.h"
#include "CoInitializeWrapper.h"


namespace po = ::boost::program_options;
namespace fs = ::boost::filesystem;
namespace wrl = ::Microsoft::WRL;

namespace{

/// <summary> Creates a <see cref="IWICColorContext" /> array of pointers </summary>
static std::vector<wrl::ComPtr<IWICColorContext>> CreateColorContextArray(wrl::ComPtr<IWICImagingFactory> const &factory, const UINT count)
{
	std::vector<wrl::ComPtr<IWICColorContext>> toCreate(count);

	for(UINT i = 0; i < count; ++i)
	{
		_com_util::CheckError(factory->CreateColorContext(toCreate[i].GetAddressOf()));
	}
	return toCreate;
}

static std::vector<IWICColorContext*> GetPointerVector(const  std::vector<wrl::ComPtr<IWICColorContext>> & toGet)
{
	std::vector<IWICColorContext*> toReturn(toGet.size());
	for(int i = 0; i < toGet.size(); ++i){
		toReturn[i] = toGet[i].Get();
	}
	return toReturn;
}

/// <summary>Down samples and converts an image to the pixel format with the least possible
/// space usage</summary>
/// <remarks>Yes I know this looks like one long chain of spaghetti code, the problem is that MS did
/// a reasonable job designing WIC and I can't really figure out a way to condense it further without 
/// making it a lot less efficient</remarks>
static HRESULT DownSampleAndConvertImage(const TransformInfo&  info)
{
	UINT sizeX = 0, sizeY = 0, finalCount = 0 ,actualContexts = 0;
	WICPixelFormatGUID inputFormat = { };
	wrl::ComPtr<IWICImagingFactory> pFactory;
	HRESULT hr = ::CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(pFactory.GetAddressOf()));
	if(FAILED(hr))
		return hr;

	wrl::ComPtr<IWICStream> pStream;
	_com_util::CheckError(pFactory->CreateStream(pStream.GetAddressOf()));

	_com_util::CheckError(pStream->InitializeFromFilename(info.InputPath().c_str(), GENERIC_READ));

	wrl::ComPtr<IWICBitmapDecoder> pDecoder;
	// this will fail if the file isn't an image we can handle
	hr = pFactory->CreateDecoderFromStream(pStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, pDecoder.GetAddressOf());
	if(FAILED(hr))
		return hr;
	wrl::ComPtr<IWICBitmapFrameDecode> pIDecoderFrame;
	_com_util::CheckError(pDecoder->GetFrame(0, pIDecoderFrame.GetAddressOf()));
	

	_com_util::CheckError(pIDecoderFrame->GetPixelFormat(std::addressof(inputFormat)));

	const auto isCMYK = GUID_WICPixelFormat32bppCMYK == inputFormat
			 || GUID_WICPixelFormat40bppCMYKAlpha == inputFormat
			 || GUID_WICPixelFormat64bppCMYK == inputFormat
			 || GUID_WICPixelFormat80bppCMYKAlpha == inputFormat;

	_com_util::CheckError(
		pIDecoderFrame->GetSize(
			std::addressof(sizeX),
			std::addressof(sizeY)));

	// ------------------------- Color -------------------------------------------

	wrl::ComPtr<IWICBitmapSource> toOutput = pIDecoderFrame;
	_com_util::CheckError(pIDecoderFrame->GetColorContexts(0, nullptr, std::addressof(actualContexts)));
	std::vector<wrl::ComPtr<IWICColorContext>> inputContexts;
	if(actualContexts || isCMYK)
	{
		// if we don't have any contexts but we're CMYK we need to have a profile anyway
		UINT contextsToCreate = !actualContexts && isCMYK ? 1 : actualContexts;
		inputContexts = CreateColorContextArray(pFactory, contextsToCreate);
	}	

	if (actualContexts > 0)
	{		
		std::vector<IWICColorContext*> addressVector = GetPointerVector(inputContexts);
		_com_util::CheckError(
			pIDecoderFrame->GetColorContexts(
				actualContexts,
				addressVector.data(),
				std::addressof(finalCount)));
	}

	if (!actualContexts && isCMYK)
	{
		fs::path finalPath(info.ProfilePath());
		finalPath /= L"RSWOP.icm";
		_com_util::CheckError(inputContexts[0]->InitializeFromFilename(finalPath.wstring().c_str()));
	}

	wrl::ComPtr<IWICColorTransform>  colorTransform;
	if (finalCount)
	{
		_com_util::CheckError(pFactory->CreateColorTransformer(colorTransform.GetAddressOf()));
	}

	wrl::ComPtr<IWICColorContext> outputContext;
	_com_util::CheckError(pFactory->CreateColorContext(outputContext.GetAddressOf()));

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
			inputContexts[i]->GetType(std::addressof(type));

			hr = colorTransform->Initialize(toOutput.Get(), inputContexts[i].Get(), outputContext.Get(), GUID_WICPixelFormat32bppBGRA);
			if(SUCCEEDED(hr))
			{
				break;
			}
		}
		toOutput = colorTransform;
	}
	//--------------------- SCALING -----------------------------------------------
	double originalDpiX = 0.0, originalDpiY = 0.0;
	_com_util::CheckError(
		toOutput->GetResolution(
			std::addressof(originalDpiX),
			std::addressof(originalDpiY)));

	wrl::ComPtr<IWICBitmapScaler> pScaler;
	if (originalDpiX > info.Dpi() || originalDpiY > info.Dpi())
	{
		_com_util::CheckError(pFactory->CreateBitmapScaler(pScaler.GetAddressOf()));
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
	::FormatMessageW(
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		::GetLastError(),
		0,
		reinterpret_cast<LPWSTR>(std::addressof(outBuffer)),
		BUFSIZ,
		nullptr
		);
	std::unique_ptr<std::remove_pointer_t<LPWSTR>, decltype(&LocalFree)> outBufferPtr(outBuffer, LocalFree);
	std::wcerr<< outBuffer <<std::endl;
}

std::wstring getColorProfileDirectory()
{
	
	DWORD bufferSize = 0;
	::GetColorDirectoryW(nullptr, nullptr, std::addressof(bufferSize));
	// unfortunately unless we want to handle the pathing semantics we're
	// with 8.3 paths
	if (bufferSize == 0 || bufferSize > (MAX_PATH * sizeof(WCHAR)))
	{
		WriteOutLastError();
		throw std::exception("Buffer size mismatch for GetColorDirectory");
	}
	WCHAR temp[MAX_PATH] = {};
	::GetColorDirectoryW(nullptr, temp , std::addressof(bufferSize));
	return temp;
}

std::string unicodeToMultibyte(const std::wstring & ws)
{
	std::wstring_convert<std::codecvt<wchar_t, char, std::mbstate_t>> convert;
	
	return convert.to_bytes(ws);
	
}

std::string narrowString(const std::wstring& ws)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.to_bytes(ws);
}

std::wstring getStringResourceW(UINT resourceID)
{
	// read only ptr
	wchar_t* buffer = nullptr;
	int resultLength = ::LoadStringW(nullptr, resourceID, reinterpret_cast<wchar_t*>(std::addressof(buffer)), 0);
	return std::wstring(buffer, resultLength);
}

std::string getStringResourceA(UINT resourceID)
{
	return unicodeToMultibyte(getStringResourceW(resourceID));
}
}

// for some reason we can't put this in an anonymous namespace it breaks complication
// but the static keywork works
static std::wistream& operator>>(std::wistream& in, ImageType & image)
{
	std::wstring token;
	in >> token;
	if (token == L"png")
		image = ImageType::PNG;
	else if (token == L"jpg")
		image = ImageType::JPG;
	else 
		throw po::validation_error(po::validation_error::invalid_option_value);
	return in;
}


/// <summary>program entry point</summary>
int wmain(int argc, wchar_t* argv[])
{
	std::locale defaultLocale("");
	std::cout.imbue(defaultLocale);
	std::wcout.imbue(defaultLocale);
	HRESULT hr = S_OK;
	double dpi = 72.0;
	ImageType outputType = ImageType::PNG;
	std::vector<TransformInfo> worklist;
	
	fs::path inputPath;
	fs::path outputPath;

	std::wcout << getStringResourceW(RS_Logo) << std::endl;
	{

		po::options_description desc("Options");
		desc.add_options()
			("help,?", "View full help")
			("format,f", po::wvalue<ImageType>()->default_value(ImageType::PNG, "png"), getStringResourceA(IDS_OUTPUTFORMAT).c_str()) //"output format")
			("outPath,o", po::wvalue<std::wstring>()->required(), getStringResourceA(IDS_OUTPUTPATH).c_str())
			("input,i", po::wvalue<std::wstring>()->required(), getStringResourceA(IDS_INPUTPATH).c_str())
			("dpi", po::wvalue<double>(std::addressof(dpi))->default_value(72.0, "72.0"), getStringResourceA(IDS_OUPUTDPI).c_str());

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
	auto profilePath = getColorProfileDirectory();

	if(fs::is_directory(inputPath) && fs::is_directory(outputPath))
	{
		fs::path inPathBase = fs::canonical(inputPath.remove_filename());
		fs::path outPathBase = fs::canonical(outputPath.remove_filename());

		fs::directory_iterator endItr;
		
		for(fs::directory_iterator inputItr(inputPath); inputItr != endItr; ++inputItr)
		{
			fs::path fileName = inputItr->path().filename();
			fs::path inPathBuffer(inPathBase);
			inPathBuffer /= fileName;

			fs::path outPathBuffer(outPathBase);
			outPathBuffer /= fileName;
			worklist.emplace_back(outputType, inPathBuffer.wstring(), outPathBuffer.wstring(), profilePath, dpi);
		}
	}
	else
	{
		worklist.emplace_back(outputType, inputPath.wstring(), outputPath.wstring(), profilePath, dpi);
	}

	Concurrency::parallel_for_each(worklist.begin(), worklist.end(),
		[](const TransformInfo & info){
			const CoInitializeWrapper<COINIT_MULTITHREADED> wrapper;
			if (SUCCEEDED(wrapper))
			{
				try{
					DownSampleAndConvertImage(info);
				}catch(const _com_error& ex){

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