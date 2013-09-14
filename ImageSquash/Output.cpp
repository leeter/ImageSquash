#include "stdafx.h"
#include "Output.h"

namespace fs = ::boost::filesystem;
namespace wrl = ::Microsoft::WRL;
using namespace ImageSquash::Output;

outputImage::outputImage(const UINT sizeX, const UINT sizeY, const wrl::ComPtr<IWICImagingFactory> factory, const double dpi)
	:factory(factory), sizeX(sizeX), sizeY(sizeY), dpi(dpi)
{
}

outputImage::~outputImage()
{}

wrl::ComPtr<IWICStream> outputImage::createStreamForPath(const std::wstring& path)
{
	wrl::ComPtr<IWICStream> stream;
	
	_com_util::CheckError( this->factory->CreateStream(&stream));
	_com_util::CheckError(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));

	return stream;
}
namespace {
class outputJpeg: public outputImage
{
public:
#if __cplusplus > 199711L
	using outputImage::outputImage;
#else
	outputJpeg(const UINT sizeX, const UINT sizeY, const wrl::ComPtr<IWICImagingFactory> factory, const double dpi)
		:outputImage(sizeX, sizeY, factory, dpi)
	{
	}
#endif
	~outputJpeg(){}

	void write(const wrl::ComPtr<IWICBitmapSource> source, const std::wstring & outputPath) override
	{
		fs::path outputPathBuffer(outputPath);
		outputPathBuffer.replace_extension(L".jpg");
		
		wrl::ComPtr<IWICPalette> palette;
		wrl::ComPtr<IWICFormatConverter> converter;
		wrl::ComPtr<IWICBitmapEncoder> encoder;
		wrl::ComPtr<IWICBitmapFrameEncode> encoderFrame;
		wrl::ComPtr<IPropertyBag2> propBag;
		
		wrl::ComPtr<IWICStream> outputStream = this->createStreamForPath(outputPathBuffer.wstring());

		wrl::ComPtr<IWICBitmapSource> forOutput = source;

		BOOL isGreyScale = FALSE;
		WICPixelFormatGUID inputFormat = { 0 };
		WICPixelFormatGUID outputFormat = GUID_WICPixelFormat24bppBGR;
		WICPixelFormatGUID selectedOutputFormat = GUID_WICPixelFormat24bppBGR;
		UINT colorCount = 0;

		HRESULT hr = forOutput->GetPixelFormat(&inputFormat);

		if (SUCCEEDED(hr))
		{
			hr = this->Factory()->CreatePalette(&palette);

			if (SUCCEEDED(hr))
			{
				hr = palette->InitializeFromBitmap(forOutput.Get(), 256U, FALSE);
			}			

			if (SUCCEEDED(hr))
			{
				hr = palette->GetColorCount(&colorCount);
			}

			if (SUCCEEDED(hr))
			{
				hr = palette->IsGrayscale(&isGreyScale);
			}

			if (SUCCEEDED(hr))
			{
				// Jpeg only supports 8bit greyscale and 24bit BGR we shouldn't
				// waste our time trying to get anything else
				if(isGreyScale && colorCount <  256U)
				{
					outputFormat = GUID_WICPixelFormat8bppGray;
				}
			}

			palette.Reset();
		}
		// if we need to convert the pixel format... we should do so
		if (SUCCEEDED(hr) && !IsEqualGUID(inputFormat, outputFormat))
		{		
			hr = this->Factory()->CreateFormatConverter(&converter);
		}

		if (SUCCEEDED(hr) && converter)
		{
			converter->Initialize(
				forOutput.Get(),
				outputFormat,
				WICBitmapDitherTypeNone,
				palette.Get(),
				0.0F,
				WICBitmapPaletteTypeCustom
				);
		}

		if (SUCCEEDED(hr) && converter)
		{
			forOutput = converter;
		}

		if (SUCCEEDED(hr))
		{
			hr = this->Factory()->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);	
		}

		if (SUCCEEDED(hr))
		{
			hr = encoder->Initialize(outputStream.Get(), WICBitmapEncoderNoCache);
		}
		if (SUCCEEDED(hr))
		{
			hr = encoder->CreateNewFrame(&encoderFrame, &propBag);
		}

		// this doesn't actually do anything at the moment, but we should keep it around as a sample of
		// how to do it in the future
		if (SUCCEEDED(hr))
		{        
			PROPBAG2 option = { 0 };
			option.pstrName = L"ImageQuality";
			_variant_t varValue(0.08f);     
			hr = propBag->Write(1, &option, &varValue); 
			if (SUCCEEDED(hr))
			{
				hr = encoderFrame->Initialize(propBag.Get());
			}
		}	

		if (SUCCEEDED(hr))
		{
			hr = encoderFrame->SetResolution(this->Dpi(), this->Dpi());
		}

		if (SUCCEEDED(hr))
		{
			hr = encoderFrame->SetSize(this->SizeX(), this->SizeY());
		}

		if (SUCCEEDED(hr))
		{
			hr = encoderFrame->SetPixelFormat(&outputFormat);
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
			hr = encoderFrame->WriteSource(forOutput.Get(), nullptr);
		}	

		if (SUCCEEDED(hr))
		{
			hr = encoderFrame->Commit();
		}

		if (SUCCEEDED(hr))
		{
			hr = encoder->Commit();
		}

		if(FAILED(hr))
		{
			throw _com_error(hr);
		}
	}
};

class outputPng: public outputImage
{
public:
#if __cplusplus > 199711L
	using outputImage::outputImage;
#else
	outputPng(const UINT sizeX, const UINT sizeY, const wrl::ComPtr<IWICImagingFactory> factory, const double dpi)
		:outputImage(sizeX, sizeY, factory, dpi)
	{
	}
#endif
	~outputPng(){}

	void write(const wrl::ComPtr<IWICBitmapSource> source, const std::wstring & outputPath) override
	{
		fs::path outputPathBuffer(outputPath);
		outputPathBuffer.replace_extension(L".png");

		wrl::ComPtr<IWICPalette> palette;
		wrl::ComPtr<IWICFormatConverter> converter;
		wrl::ComPtr<IWICBitmapEncoder> encoder;
		wrl::ComPtr<IWICBitmapFrameEncode> encoderFrame;
		wrl::ComPtr<IPropertyBag2> propBag;

		wrl::ComPtr<IWICStream> outputStream = this->createStreamForPath(outputPathBuffer.wstring());


		wrl::ComPtr<IWICBitmapSource> forOutput = source;

		BOOL hasAlpha = FALSE, isGreyScale = FALSE, isBlackAndWhite = FALSE, hasPalette = FALSE;
		WICPixelFormatGUID inputFormat = { 0 };
		WICPixelFormatGUID outputFormat = GUID_WICPixelFormat32bppBGRA;
		WICPixelFormatGUID selectedOutputFormat = GUID_WICPixelFormat32bppBGRA;
		UINT colorCount = 0;

		_com_util::CheckError(forOutput->GetPixelFormat(&inputFormat));

		if (!IsEqualGUID(inputFormat, GUID_WICPixelFormatBlackWhite))
		{
			_com_util::CheckError(this->Factory()->CreatePalette(&palette));

			hasAlpha = this->HasAlpha(forOutput.Get());

			_com_util::CheckError(palette->InitializeFromBitmap(forOutput.Get(), 256U, hasAlpha));		

			_com_util::CheckError(palette->GetColorCount(&colorCount));
			_com_util::CheckError(palette->IsBlackWhite(&isBlackAndWhite));

			_com_util::CheckError(palette->IsGrayscale(&isGreyScale));

			outputFormat = selectedOutputFormat = this->GetOutputPixelFormat(
				colorCount,
				hasAlpha,
				isBlackAndWhite,
				isGreyScale,
				hasPalette
				);

			if (!hasPalette)
			{
				palette.Reset();
			}
		}
		else
		{
			outputFormat = selectedOutputFormat = inputFormat;
		}
		// if we need to convert the pixel format... we should do so
		if (!IsEqualGUID(inputFormat, outputFormat))
		{		
			_com_util::CheckError(this->Factory()->CreateFormatConverter(&converter));
		}

		if (converter)
		{
			_com_util::CheckError(converter->Initialize(
				forOutput.Get(),
				outputFormat,
				WICBitmapDitherTypeNone,
				palette.Get(),
				0.0F,
				WICBitmapPaletteTypeCustom
				));
			forOutput = converter;
		}

		_com_util::CheckError(this->Factory()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));

		_com_util::CheckError(encoder->Initialize(outputStream.Get(), WICBitmapEncoderNoCache));

		_com_util::CheckError(encoder->CreateNewFrame(&encoderFrame, &propBag));

		// this doesn't actually do anything at the moment, but we should keep it around as a sample of
		// how to do it in the future
		PROPBAG2 option = { 0 };
		option.pstrName = L"InterlaceOption";
		_variant_t varValue(false);     
		_com_util::CheckError(propBag->Write(1, &option, &varValue));
		_com_util::CheckError(encoderFrame->Initialize(propBag.Get()));

		_com_util::CheckError(encoderFrame->SetResolution(this->Dpi(), this->Dpi()));

		_com_util::CheckError(encoderFrame->SetSize(this->SizeX(), this->SizeY()));

		_com_util::CheckError(encoderFrame->SetPixelFormat(&outputFormat));

		if (!IsEqualGUID(outputFormat, selectedOutputFormat))
		{
			throw _com_error(E_FAIL);
		}
		// disabled as this was adding 4k to the image
		/*if (SUCCEEDED(hr))
		{
		hr = pOutputFrame->SetColorContexts(1, outputContexts);
		}*/

		_com_util::CheckError(encoderFrame->WriteSource(forOutput.Get(), nullptr));
		
		_com_util::CheckError(encoderFrame->Commit());

		_com_util::CheckError(encoder->Commit());
	}

	bool HasAlpha(const wrl::ComPtr<IWICBitmapSource> & source)
	{
		// stuff that has to be cleaned up
		wrl::ComPtr<IWICFormatConverter> converter;
		wrl::ComPtr<IWICBitmap> bitmap;
		wrl::ComPtr<IWICBitmapLock> lock;

		// stuff that doesn't
		WICInProcPointer buffer;
		wrl::ComPtr<IWICBitmapSource> finalSource;	
		WICPixelFormatGUID inputFormat = { 0 };	
		UINT sizeX = 0, sizeY = 0, bufferSize = 0;

		HRESULT hr = source->GetPixelFormat(&inputFormat);	
		bool hasAlpha = false;
		if (SUCCEEDED(hr))
		{
			if(this->IsPixelFormatRGBWithAlpha(inputFormat))
			{
				if (IsEqualGUID(inputFormat, GUID_WICPixelFormat32bppBGRA))
				{
					finalSource = source;
				}
				else
				{
					hr = this->Factory()->CreateFormatConverter(&converter);
					if (SUCCEEDED(hr))
					{
						hr = converter->Initialize(
							source.Get(),
							GUID_WICPixelFormat32bppBGRA,
							WICBitmapDitherTypeNone,
							nullptr,
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
					hr = this->Factory()->CreateBitmapFromSource(finalSource.Get(), WICBitmapCacheOnDemand, &bitmap);
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
					// it's little endian, as such alpha will be stored in the
					// pixel with the highest address if we're in BGRA format
					size_t i = bufferSize - 1;
					do
					{					
						if(buffer[i] != 0xFFU)
						{
							hasAlpha = TRUE;
							break;
						}
						i -= 4;
					}while(i > 3);
				}
			}
		}

		return hasAlpha;
	}

	const bool IsPixelFormatRGBWithAlpha(const WICPixelFormatGUID& pixelFormat) const
	{
		return InlineIsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA) ||
			InlineIsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppPBGRA) ||
			InlineIsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppRGBA) ||
			InlineIsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppPRGBA) ||
			IsEqualGUID(pixelFormat, GUID_WICPixelFormat128bppPRGBAFloat) ||
			IsEqualGUID(pixelFormat, GUID_WICPixelFormat128bppRGBAFixedPoint) ||
			IsEqualGUID(pixelFormat, GUID_WICPixelFormat128bppRGBAFloat) ||
			IsEqualGUID(pixelFormat, GUID_WICPixelFormat64bppBGRA) ||
			IsEqualGUID(pixelFormat, GUID_WICPixelFormat64bppBGRAFixedPoint) ||
			IsEqualGUID(pixelFormat, GUID_WICPixelFormat64bppPBGRA) ||
			IsEqualGUID(pixelFormat, GUID_WICPixelFormat64bppPRGBA) ||
			IsEqualGUID(pixelFormat, GUID_WICPixelFormat64bppRGBA);
	}

	const WICPixelFormatGUID GetOutputPixelFormat(const UINT colorCount, const BOOL hasAlpha, const BOOL isBlackAndWhite, const BOOL isGreyScale, BOOL & hasPalette)
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
};
}

std::unique_ptr<outputImage> outputImage::CreateOutputImage(
		const UINT sizeX,
		const UINT sizeY,
		const ImageType outputType,
		const wrl::ComPtr<IWICImagingFactory> factory,
		const double dpi)
{
	if(outputType == PNG)
	{
		return std::unique_ptr<outputImage>(new outputPng(sizeX, sizeY, factory, dpi));
	}
	return std::unique_ptr<outputImage>(new outputJpeg(sizeX, sizeY, factory, dpi));
}