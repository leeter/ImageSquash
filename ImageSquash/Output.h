#ifndef OUTPUT_H
#define OUTPUT_H

#	if defined(_MSC_VER)
#		pragma once
#endif

#include "stdafx.h"

struct OutputInfo
{
	IWICImagingFactory * factory;
	IWICBitmapSource * source;
	double dpi;
	UINT sizeX;
	UINT sizeY;
};

STDMETHODIMP OutputImage(OutputInfo * info, LPCWSTR outputPath, GUID outputFormat);

#endif // OUTPUT_H