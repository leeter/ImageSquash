#ifndef OUTPUT_H
#define OUTPUT_H

#	if defined(_MSC_VER)
#		pragma once
#endif

#include "stdafx.h"

STDMETHODIMP OutputImage(IWICImagingFactory *factory, IWICBitmapSource * toOutput, LPCWSTR outputPath, GUID outputFormat, double dpi, UINT sizeX, UINT sizeY);

#endif // OUTPUT_H