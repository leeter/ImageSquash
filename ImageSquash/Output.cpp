#include "stdafx.h"
#include "Output.h"
using namespace ImageSquash::Output;

static BOOL STDMETHODCALLTYPE IsPixelFormatRGBWithAlpha(WICPixelFormatGUID pixelFormat)
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
	CComPtr<IWICFormatConverter> converter = nullptr;
	CComPtr<IWICBitmap> bitmap = nullptr;
	CComPtr<IWICBitmapLock> lock = nullptr;

	// stuff that doesn't
	WICInProcPointer buffer = nullptr;
	UINT * bitmapPixels = nullptr;
	IWICBitmapSource * finalSource = nullptr;	
	WICPixelFormatGUID inputFormat = { 0 };	
	UINT sizeX = 0, sizeY = 0, bufferSize = 0;

	HRESULT hr = source->GetPixelFormat(&inputFormat);	
	hasAlpha = false;
	if (SUCCEEDED(hr))
	{
		if(IsPixelFormatRGBWithAlpha(inputFormat))
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

	return hr;
}
static STDMETHODIMP OutputJpg(const OutputInfo & info, IWICStream * outputStream)
{
	CComPtr<IWICPalette> palette = nullptr;
	CComPtr<IWICFormatConverter> converter = nullptr;
	CComPtr<IWICBitmapEncoder> encoder = nullptr;
	CComPtr<IWICBitmapFrameEncode> encoderFrame = nullptr;
	CComPtr<IPropertyBag2> propBag = nullptr;

	// hand off pointer no cleanup
	IWICBitmapSource * forOutput = info.source;

	BOOL isGreyScale = FALSE;
	WICPixelFormatGUID inputFormat = { 0 }, outputFormat = GUID_WICPixelFormat24bppBGR, selectedOutputFormat = GUID_WICPixelFormat24bppBGR;
	UINT colorCount = 0;

	HRESULT hr = forOutput->GetPixelFormat(&inputFormat);

	if (SUCCEEDED(hr))
	{
		hr = info.factory->CreatePalette(&palette);

		if (SUCCEEDED(hr))
		{
			hr = palette->InitializeFromBitmap(forOutput, 256U, FALSE);
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

		palette.Release();
	}
	// if we need to convert the pixel format... we should do so
	if (SUCCEEDED(hr) && !IsEqualGUID(inputFormat, outputFormat))
	{		
		hr = info.factory->CreateFormatConverter(&converter);
	}

	if (SUCCEEDED(hr) && converter)
	{
		converter->Initialize(
			forOutput,
			outputFormat,
			WICBitmapDitherTypeNone,
			palette,
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
		hr = info.factory->CreateEncoder(GUID_ContainerFormatJpeg, NULL, &encoder);	
	}

	if (SUCCEEDED(hr))
	{
		hr = encoder->Initialize(outputStream, WICBitmapEncoderNoCache);
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
		VARIANT varValue;    
		VariantInit(&varValue);
		varValue.vt = VT_R4;
		varValue.fltVal = 0.8f;      
		hr = propBag->Write(1, &option, &varValue); 
		if (SUCCEEDED(hr))
		{
			hr = encoderFrame->Initialize(propBag);
		}
	}	

	if (SUCCEEDED(hr))
	{
		hr = encoderFrame->SetResolution(info.dpi, info.dpi);
	}

	if (SUCCEEDED(hr))
	{
		hr = encoderFrame->SetSize(info.sizeX, info.sizeY);
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
		hr = encoderFrame->WriteSource(forOutput, NULL);
	}	

	if (SUCCEEDED(hr))
	{
		hr = encoderFrame->Commit();
	}

	if (SUCCEEDED(hr))
	{
		hr = encoder->Commit();
	}

	return hr;
}
static STDMETHODIMP OutputPng(const OutputInfo & info, IWICStream * outputStream)
{
	CComPtr<IWICPalette> palette = nullptr;
	CComPtr<IWICFormatConverter> converter = nullptr;
	CComPtr<IWICBitmapEncoder> encoder = nullptr;
	CComPtr<IWICBitmapFrameEncode> encoderFrame = nullptr;
	CComPtr<IPropertyBag2> propBag = nullptr;

	// hand off pointer no cleanup
	IWICBitmapSource * forOutput = info.source;

	BOOL hasAlpha = FALSE, isGreyScale = FALSE, isBlackAndWhite = FALSE, hasPalette = FALSE;
	WICPixelFormatGUID inputFormat = { 0 }, outputFormat = GUID_WICPixelFormat32bppBGRA, selectedOutputFormat = GUID_WICPixelFormat32bppBGRA;
	UINT colorCount = 0;

	HRESULT hr = forOutput->GetPixelFormat(&inputFormat);

	if (SUCCEEDED(hr))
	{
		if (!IsEqualGUID(inputFormat, GUID_WICPixelFormatBlackWhite))
		{
			hr = info.factory->CreatePalette(&palette);

			if (SUCCEEDED(hr))
			{
				hr = HasAlpha(forOutput, info.factory, hasAlpha);
			}

			if (SUCCEEDED(hr))
			{
				hr = palette->InitializeFromBitmap(forOutput, 256U, hasAlpha);
			}			

			if (SUCCEEDED(hr))
			{
				hr = palette->GetColorCount(&colorCount);
			}

			if (SUCCEEDED(hr))
			{
				hr = palette->IsBlackWhite(&isBlackAndWhite);
			}

			if (SUCCEEDED(hr))
			{
				hr = palette->IsGrayscale(&isGreyScale);
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
					palette.Release();
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
		hr = info.factory->CreateFormatConverter(&converter);
	}

	if (SUCCEEDED(hr) && converter)
	{
		converter->Initialize(
			forOutput,
			outputFormat,
			WICBitmapDitherTypeNone,
			palette,
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
		hr = info.factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder);	
	}

	if (SUCCEEDED(hr))
	{
		hr = encoder->Initialize(outputStream, WICBitmapEncoderNoCache);
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
		option.pstrName = L"InterlaceOption";
		VARIANT varValue;    
		VariantInit(&varValue);
		varValue.vt = VT_BOOL;
		varValue.boolVal = 0;      
		hr = propBag->Write(1, &option, &varValue); 
		if (SUCCEEDED(hr))
		{
			hr = encoderFrame->Initialize(propBag);
		}
	}	

	if (SUCCEEDED(hr))
	{
		hr = encoderFrame->SetResolution(info.dpi, info.dpi);
	}

	if (SUCCEEDED(hr))
	{
		hr = encoderFrame->SetSize(info.sizeX, info.sizeY);
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
		hr = encoderFrame->WriteSource(forOutput, NULL);
	}	

	if (SUCCEEDED(hr))
	{
		hr = encoderFrame->Commit();
	}

	if (SUCCEEDED(hr))
	{
		hr = encoder->Commit();
	}

	return hr;
}

STDMETHODIMP CreateStreamForPath(IWICImagingFactory * factory, CComPtr<IWICStream>& stream, LPCWSTR path)
{
	HRESULT hr = factory->CreateStream(&stream);

	if(SUCCEEDED(hr))
	{
		hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
	}
	return hr;
}

STDMETHODIMP ImageSquash::Output::OutputImage(const OutputInfo & info, LPCWSTR outputPath, GUID outputFormat)
{
	CComPtr<IWICStream> outputStream = nullptr;
	WCHAR outputPathBuffer[MAX_PATH];

	HRESULT hr = StringCchCopy(outputPathBuffer, MAX_PATH, outputPath);
	if (FAILED(hr))
	{
		return hr;
	}

	if(IsEqualGUID(outputFormat, GUID_ContainerFormatPng))
	{

		if(!PathRenameExtension(outputPathBuffer, L".png"))
		{
			// TODO: write out last error to stderr
			hr = E_FAIL;
		}

		if (SUCCEEDED(hr))
		{
			hr = CreateStreamForPath(info.factory, outputStream, outputPathBuffer);
		}

		if (SUCCEEDED(hr))
		{
			hr = OutputPng(info, outputStream);
		}

	}
	/* JPEG */
	else if(IsEqualGUID(outputFormat, GUID_ContainerFormatJpeg))
	{

		if(!PathRenameExtension(outputPathBuffer, L".jpg"))
		{
			// TODO: write out last error to stderr
			hr = E_FAIL;
		}

		if (SUCCEEDED(hr))
		{
			hr = CreateStreamForPath(info.factory, outputStream, outputPathBuffer);
		}

		if (SUCCEEDED(hr))
		{
			hr = OutputJpg(info, outputStream);
		}

	}

	return hr;
}