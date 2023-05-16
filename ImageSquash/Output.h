#ifndef OUTPUT_H
#define OUTPUT_H

#	if defined(_MSC_VER)
#		pragma once
#endif

#include "stdafx.h"
#include <string_view>
#include "TransformInfo.h"

namespace ImageSquash::Output{

class _declspec(novtable) outputImage
{
	outputImage(const outputImage&) = delete;
public:
	static std::unique_ptr<outputImage> CreateOutputImage(
		const UINT sizeX,
		const UINT sizeY,
		const ImageType outputType,
		IWICImagingFactory2* factory,
		const double dpi);
	outputImage(
		const UINT sizeX,
		const UINT sizeY,
		IWICImagingFactory2* factory,
		const double dpi);
	virtual ~outputImage() noexcept;

public:
	virtual void write(IWICBitmapSource* source, const std::wstring_view outputPath) = 0;

protected:
	
	winrt::com_ptr<IWICStream> createStreamForPath(const std::wstring& path);
	IWICImagingFactory2* Factory() const noexcept { return this->factory; }
	UINT SizeX() const noexcept { return this->sizeX; }
	UINT SizeY() const noexcept { return this->sizeY; }
	double Dpi() const noexcept { return this->dpi; }
private:
	const outputImage& operator=(const outputImage &rhs) = delete;
	IWICImagingFactory2* factory;
	const UINT sizeX;
	const UINT sizeY;
	const double dpi;
};
}
#endif // OUTPUT_H