#include "stdafx.h"
#include "Output.h"

namespace wrl = ::Microsoft::WRL;
using namespace ImageSquash::Output;

outputImage::outputImage(const UINT sizeX, const UINT sizeY, IWICImagingFactory2* factory, const double dpi)
	:factory(factory), sizeX(sizeX), sizeY(sizeY), dpi(dpi)
{
}

outputImage::~outputImage() noexcept
{}

winrt::com_ptr<IWICStream> outputImage::createStreamForPath(const std::wstring& path)
{
	auto stream = is::capture<IWICStream>(this->factory, &IWICImagingFactory::CreateStream);
	winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));

	return stream;
}
namespace {
class outputJpeg: public outputImage
{
	outputJpeg(const outputJpeg&) = delete;
public:
	using outputImage::outputImage;
	~outputJpeg() noexcept override = default;

	void write(IWICBitmapSource* source, const std::wstring_view outputPath) override
	{
		std::filesystem::path outputPathBuffer(outputPath);
		outputPathBuffer.replace_extension(L".jpg");
		
		const auto outputStream = this->createStreamForPath(outputPathBuffer.wstring());

		winrt::com_ptr<IWICBitmapSource> forOutput;
		forOutput.copy_from(source);

		WICPixelFormatGUID inputFormat = { };
		WICPixelFormatGUID outputFormat = GUID_WICPixelFormat24bppBGR;
		WICPixelFormatGUID selectedOutputFormat = GUID_WICPixelFormat24bppBGR;

		HRESULT hr = forOutput->GetPixelFormat(std::addressof(inputFormat));
		auto palette = is::capture<IWICPalette>(this->Factory(), &IWICImagingFactory::CreatePalette);
		{
			winrt::check_hresult(palette->InitializeFromBitmap(forOutput.get(), 256U, FALSE));
			UINT colorCount = 0;
			winrt::check_hresult(palette->GetColorCount(std::addressof(colorCount)));
			BOOL isGreyScale = FALSE;
			winrt::check_hresult(palette->IsGrayscale(std::addressof(isGreyScale)));

			// Jpeg only supports 8bit greyscale and 24bit BGR we shouldn't
			// waste our time trying to get anything else
			if(isGreyScale && colorCount <  256U)
			{
				selectedOutputFormat = outputFormat = GUID_WICPixelFormat8bppGray;
			}
		}
		// if we need to convert the pixel format... we should do so
		if (!IsEqualGUID(inputFormat, outputFormat))
		{
			auto converter = is::capture<IWICFormatConverter>(this->Factory(), &IWICImagingFactory2::CreateFormatConverter);
		
			winrt::check_hresult(
				converter->Initialize(
					forOutput.get(),
					outputFormat,
					WICBitmapDitherTypeNone,
					palette.get(),
					0.0F,
					WICBitmapPaletteTypeCustom
					));
			forOutput = converter;
		}

		auto encoder = is::capture<IWICBitmapEncoder>(this->Factory(), &IWICImagingFactory2::CreateEncoder, GUID_ContainerFormatJpeg, nullptr);

		winrt::check_hresult(encoder->Initialize(outputStream.get(), WICBitmapEncoderNoCache));
		winrt::com_ptr<IWICBitmapFrameEncode> encoderFrame;
		winrt::com_ptr<IPropertyBag2> propBag;
		winrt::check_hresult(encoder->CreateNewFrame(encoderFrame.put(), propBag.put()));

		// this doesn't actually do anything at the moment, but we should keep it around as a sample of
		// how to do it in the future
		if (SUCCEEDED(hr))
		{        
			PROPBAG2 option = { };
			wchar_t imageQualOpt[] = L"ImageQuality";
			option.pstrName = &imageQualOpt[0];
			wil::unique_variant varValue;
			varValue.fltVal = 0.08f;
			varValue.vt = VT_R4;
			
			winrt::check_hresult(propBag->Write(1, std::addressof(option), std::addressof(varValue)));
			winrt::check_hresult(encoderFrame->Initialize(propBag.get()));
		}

		winrt::check_hresult(encoderFrame->SetResolution(this->Dpi(), this->Dpi()));
		winrt::check_hresult(encoderFrame->SetSize(this->SizeX(), this->SizeY()));
		winrt::check_hresult(encoderFrame->SetPixelFormat(std::addressof(outputFormat)));
		

		if (!IsEqualGUID(outputFormat, selectedOutputFormat))
		{
			winrt::throw_hresult(WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT);
		}
		// disabled as this was adding 4k to the image
		/*if (SUCCEEDED(hr))
		{
		hr = pOutputFrame->SetColorContexts(1, outputContexts);
		}*/

		winrt::check_hresult(encoderFrame->WriteSource(forOutput.get(), nullptr));
		winrt::check_hresult(encoderFrame->Commit());
		winrt::check_hresult(encoder->Commit());
	}
};

class outputPng: public outputImage
{
public:
	using outputImage::outputImage;
	~outputPng() noexcept override = default;

	void write(IWICBitmapSource* source, const std::wstring_view outputPath) override
	{
		std::filesystem::path outputPathBuffer(outputPath);
		outputPathBuffer.replace_extension(L".png");

		auto outputStream = this->createStreamForPath(outputPathBuffer.wstring());


		winrt::com_ptr<IWICBitmapSource> forOutput;
		forOutput.copy_from(source);
		
		WICPixelFormatGUID outputFormat = GUID_WICPixelFormat32bppBGRA;
		WICPixelFormatGUID selectedOutputFormat = GUID_WICPixelFormat32bppBGRA;
		WICPixelFormatGUID inputFormat = {};
		winrt::check_hresult(forOutput->GetPixelFormat(std::addressof(inputFormat)));
		winrt::com_ptr<IWICPalette> palette;
		if (inputFormat != GUID_WICPixelFormatBlackWhite)
		{
			is::capture<IWICPalette>(this->Factory(), &IWICImagingFactory2::CreatePalette);

			const auto hasAlpha = this->HasAlpha(forOutput.get());

			winrt::check_hresult(palette->InitializeFromBitmap(forOutput.get(), 256U, hasAlpha));		
			UINT colorCount = 0;
			winrt::check_hresult(palette->GetColorCount(std::addressof(colorCount)));
			BOOL isBlackAndWhite = false;
			winrt::check_hresult(palette->IsBlackWhite(std::addressof(isBlackAndWhite)));

			//winrt::check_hresult(palette->IsGrayscale(std::addressof(isGreyScale)));
			bool hasPalette = false;
			std::tie(selectedOutputFormat, hasPalette)
				= this->GetOutputPixelFormat(
					colorCount,
					hasAlpha,
					isBlackAndWhite
				);
			outputFormat = selectedOutputFormat;

			if (!hasPalette)
			{
				palette = nullptr;
			}
		}
		else
		{
			outputFormat = selectedOutputFormat = inputFormat;
		}
		
		// if we need to convert the pixel format... we should do so
		if (inputFormat != outputFormat)
		{
			auto converter = is::capture<IWICFormatConverter>(this->Factory(), &IWICImagingFactory::CreateFormatConverter);
			winrt::check_hresult(converter->Initialize(
				forOutput.get(),
				outputFormat,
				WICBitmapDitherTypeNone,
				palette.get(),
				0.0F,
				WICBitmapPaletteTypeCustom
			));
			forOutput = std::move(converter);
		}
		auto encoder = is::capture<IWICBitmapEncoder>(this->Factory(), &IWICImagingFactory::CreateEncoder, GUID_ContainerFormatPng, nullptr);

		winrt::check_hresult(encoder->Initialize(outputStream.get(), WICBitmapEncoderNoCache));
		winrt::com_ptr<IWICBitmapFrameEncode> encoderFrame;
		winrt::com_ptr<IPropertyBag2> propBag;
		winrt::check_hresult(encoder->CreateNewFrame(encoderFrame.put(), propBag.put()));

		// this doesn't actually do anything at the moment, but we should keep it around as a sample of
		// how to do it in the future
		PROPBAG2 option = { };
		wchar_t interlaceOpt[] = L"InterlaceOption";
		option.pstrName = &interlaceOpt[0];
		VARIANT varValue(false);     
		winrt::check_hresult(propBag->Write(1, std::addressof(option), std::addressof(varValue)));
		winrt::check_hresult(encoderFrame->Initialize(propBag.get()));

		winrt::check_hresult(encoderFrame->SetResolution(this->Dpi(), this->Dpi()));

		winrt::check_hresult(encoderFrame->SetSize(this->SizeX(), this->SizeY()));

		winrt::check_hresult(encoderFrame->SetPixelFormat(std::addressof(outputFormat)));

		if (outputFormat != selectedOutputFormat)
		{
			winrt::throw_hresult(E_FAIL);
		}
		// disabled as this was adding 4k to the image
		/*if (SUCCEEDED(hr))
		{
		hr = pOutputFrame->SetColorContexts(1, outputContexts);
		}*/

		winrt::check_hresult(encoderFrame->WriteSource(forOutput.get(), nullptr));
		
		winrt::check_hresult(encoderFrame->Commit());

		winrt::check_hresult(encoder->Commit());
	}

	bool HasAlpha(IWICBitmapSource * source)
	{
		// stuff that has to be cleaned up
		WICPixelFormatGUID inputFormat = { };
		winrt::check_hresult(source->GetPixelFormat(std::addressof(inputFormat)));
		bool hasAlpha = false;
		if (!this->IsPixelFormatRGBWithAlpha(inputFormat)) {
			return false;
		}

		winrt::com_ptr<IWICBitmapSource> finalSource;
		if (inputFormat == GUID_WICPixelFormat32bppBGRA)
		{
			finalSource.copy_from(source);
		}
		else
		{
			auto converter = is::capture<IWICFormatConverter>(this->Factory(), &IWICImagingFactory2::CreateFormatConverter);
			winrt::check_hresult(
				converter->Initialize(
					source,
					GUID_WICPixelFormat32bppBGRA,
					WICBitmapDitherTypeNone,
					nullptr,
					0.0,
					WICBitmapPaletteTypeCustom
					));

			finalSource = std::move(converter);
		}
		
		auto bitmap = is::capture<IWICBitmap>(
			this->Factory(),
			&IWICImagingFactory2::CreateBitmapFromSource,
			finalSource.get(),
			WICBitmapCacheOnDemand);
		UINT sizeX = 0u, sizeY = 0u;
		winrt::check_hresult(bitmap->GetSize(std::addressof(sizeX), std::addressof(sizeY)));

		const WICRect lockRectangle = { 0, 0, static_cast<INT>(sizeX), static_cast<INT>(sizeY)};
		auto lock = is::capture<IWICBitmapLock>(
				bitmap, &IWICBitmap::Lock, std::addressof(lockRectangle), WICBitmapLockRead);
		
		UINT bufferSize = 0u;
		WICInProcPointer buffer;
		winrt::check_hresult(
			lock->GetDataPointer(std::addressof(bufferSize), std::addressof(buffer)));

		const std::uint32_t * imageBuffer = reinterpret_cast<const std::uint32_t *>(buffer);
		// it's little endian, as such alpha will be stored in the
		// pixel with the highest address if we're in BGRA format
		UINT pixelCount = bufferSize / sizeof(std::uint32_t);
		for (UINT i = 0; i < pixelCount; ++i)
		{				
			UINT pixel = imageBuffer[i];
			if ((pixel & 0xFF000000U) != 0xFF000000U)
			{
				hasAlpha = true;
				break;
			}
		}

		return hasAlpha;
	}

	const bool IsPixelFormatRGBWithAlpha(const WICPixelFormatGUID& pixelFormat) const noexcept
	{
		return pixelFormat == GUID_WICPixelFormat32bppBGRA
			|| pixelFormat == GUID_WICPixelFormat32bppPBGRA
			|| pixelFormat == GUID_WICPixelFormat32bppRGBA
			|| pixelFormat == GUID_WICPixelFormat32bppPRGBA
			|| pixelFormat == GUID_WICPixelFormat128bppPRGBAFloat
			|| pixelFormat == GUID_WICPixelFormat128bppRGBAFixedPoint
			|| pixelFormat == GUID_WICPixelFormat128bppRGBAFloat
			|| pixelFormat == GUID_WICPixelFormat64bppBGRA
			|| pixelFormat == GUID_WICPixelFormat64bppBGRAFixedPoint
			|| pixelFormat == GUID_WICPixelFormat64bppPBGRA
			|| pixelFormat == GUID_WICPixelFormat64bppPRGBA
			|| pixelFormat == GUID_WICPixelFormat64bppRGBA;
	}

	std::pair<WICPixelFormatGUID, bool> GetOutputPixelFormat(const UINT colorCount, const BOOL hasAlpha, const BOOL isBlackAndWhite) const noexcept
	{
		if (isBlackAndWhite)
		{
			return std::make_pair(GUID_WICPixelFormatBlackWhite, false);
		}
		else if (!hasAlpha)
		{
			if (colorCount < 3U)
			{
				return std::make_pair(GUID_WICPixelFormat1bppIndexed, true);
			}
			if (colorCount < 5U)
			{
				return std::make_pair(GUID_WICPixelFormat2bppIndexed, true);
			}
			else if (colorCount < 17U)
			{
				return std::make_pair(GUID_WICPixelFormat4bppIndexed, true);
			}
			// while it would be convenient to check to see if they have less that 257 colors
			// unfortunately the way the palette system works this would result in all images
			// without alpha being caught in this logical trap. So until I find a better way
			// to deal with it... it will have to have 255 colors or less to end up in the 8bpp
			// range.
			else if (colorCount < 256U)
			{
				return std::make_pair(GUID_WICPixelFormat8bppIndexed, true);
			}
			else
			{
				return std::make_pair(GUID_WICPixelFormat24bppBGR, false);
			}
		}
		return std::make_pair(GUID_WICPixelFormat32bppBGRA, false);
	}
};
}

std::unique_ptr<outputImage> outputImage::CreateOutputImage(
		const UINT sizeX,
		const UINT sizeY,
		const ImageType outputType,
		IWICImagingFactory2* factory,
		const double dpi)
{
	if(outputType == ImageType::PNG)
	{
		return std::make_unique<outputPng>(sizeX, sizeY, factory, dpi);
	}
	return std::make_unique<outputJpeg>(sizeX, sizeY, factory, dpi);
}