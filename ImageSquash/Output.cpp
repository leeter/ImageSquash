#include "stdafx.h"
#include "Output.h"


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

			SafeRelease(&converter);
			SafeRelease(&lock);
			SafeRelease(&bitmap);
		}
	}

	return hr;
}
static STDMETHODIMP OutputPng(IWICImagingFactory *factory, IWICBitmapSource * toOutput, IWICStream * outputStream, double dpi, UINT sizeX, UINT sizeY)
{
	IWICPalette * palette = NULL;
	IWICFormatConverter * converter = NULL;
	IWICBitmapEncoder * encoder = NULL;
	IWICBitmapFrameEncode * encoderFrame = NULL;
	IPropertyBag2 * propBag = NULL;

	// hand off pointer no cleanup
	IWICBitmapSource * forOutput = toOutput;

	BOOL hasAlpha = FALSE, isGreyScale = FALSE, isBlackAndWhite = FALSE, hasPalette = FALSE;
	WICPixelFormatGUID inputFormat = { 0 }, outputFormat = GUID_WICPixelFormat32bppBGRA, selectedOutputFormat = GUID_WICPixelFormat32bppBGRA;
	UINT colorCount = 0;

	HRESULT hr = toOutput->GetPixelFormat(&inputFormat);

	if (SUCCEEDED(hr))
	{
		if (!IsEqualGUID(inputFormat, GUID_WICPixelFormatBlackWhite))
		{
			hr = factory->CreatePalette(&palette);

			if (SUCCEEDED(hr))
			{
				hr = HasAlpha(toOutput, factory, hasAlpha);
			}

			if (SUCCEEDED(hr))
			{
				hr = palette->InitializeFromBitmap(toOutput, 256U, hasAlpha);
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
					SafeRelease(&palette);
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
		hr = factory->CreateFormatConverter(&converter);
	}

	if (SUCCEEDED(hr) && converter)
	{
		converter->Initialize(
			toOutput,
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
		hr = factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder);	
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
		hr = encoderFrame->SetResolution(dpi, dpi);
	}

	if (SUCCEEDED(hr))
	{
		hr = encoderFrame->SetSize(sizeX, sizeY);
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
		hr = encoderFrame->WriteSource(toOutput, NULL);
	}	

	if (SUCCEEDED(hr))
	{
		hr = encoderFrame->Commit();
	}

	if (SUCCEEDED(hr))
	{
		hr = encoder->Commit();
	}

	SafeRelease(&palette);
	SafeRelease(&converter);
	SafeRelease(&propBag);
	SafeRelease(&encoderFrame);
	SafeRelease(&encoder);
	return hr;
}

STDMETHODIMP CreateStreamForPath(IWICImagingFactory * factory, IWICStream ** stream, LPCWSTR path)
{
	HRESULT hr = factory->CreateStream(stream);

	if(SUCCEEDED(hr))
	{
		hr = (*stream)->InitializeFromFilename(path, GENERIC_WRITE);
	}
	return hr;
}

STDMETHODIMP OutputImage(IWICImagingFactory *factory, IWICBitmapSource * toOutput, LPCWSTR outputPath, GUID outputFormat, double dpi, UINT sizeX, UINT sizeY)
{
	IWICStream * outputStream = NULL;
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
			hr = CreateStreamForPath(factory, &outputStream, outputPathBuffer);
		}

		if (SUCCEEDED(hr))
		{
			hr = OutputPng(factory, toOutput, outputStream, dpi, sizeX, sizeY);
		}
		
	}

	SafeRelease(&outputStream);	
	return hr;
}