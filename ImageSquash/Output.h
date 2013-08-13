#ifndef OUTPUT_H
#define OUTPUT_H

#	if defined(_MSC_VER)
#		pragma once
#endif

#include "stdafx.h"
#include "TransformInfo.h"

namespace ImageSquash{
	namespace Output{

class _declspec(novtable) outputImage
{
public:
	static std::unique_ptr<outputImage> CreateOutputImage(
		const UINT sizeX,
		const UINT sizeY,
		const ImageType outputType,
		const ATL::CComPtr<IWICImagingFactory> factory,
		const double dpi);
	virtual ~outputImage(void);

public:
	virtual void write(const ATL::CComPtr<IWICBitmapSource> source, const std::wstring & outputPath) = 0;

protected:
	outputImage(
		const UINT sizeX,
		const UINT sizeY,
		const ATL::CComPtr<IWICImagingFactory> factory,
		const double dpi);
	const ATL::CComPtr<IWICStream> createStreamForPath(const std::wstring& path);
	const ATL::CComPtr<IWICImagingFactory>& Factory() const { return this->factory; }
	const UINT SizeX() const { return this->sizeX; }
	const UINT SizeY() const{ return this->sizeY; }
	const double Dpi() const { return this->dpi; }
private:
	const ATL::CComPtr<IWICImagingFactory> factory;
	const UINT sizeX;
	const UINT sizeY;
	const double dpi;
};

//STDMETHODIMP OutputImage(const OutputInfo & info, const std::wstring & outputPath, const GUID & outputFormat);
	}
}
#endif // OUTPUT_H