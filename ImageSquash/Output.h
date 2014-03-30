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
		const Microsoft::WRL::ComPtr<IWICImagingFactory>& factory,
		const double dpi);
	virtual ~outputImage(void) throw();

public:
	virtual void write(const Microsoft::WRL::ComPtr<IWICBitmapSource> source, const std::wstring & outputPath) = 0;

protected:
	outputImage(
		const UINT sizeX,
		const UINT sizeY,
		const Microsoft::WRL::ComPtr<IWICImagingFactory>& factory,
		const double dpi);
	Microsoft::WRL::ComPtr<IWICStream> createStreamForPath(const std::wstring& path);
	Microsoft::WRL::ComPtr<IWICImagingFactory> Factory() const { return this->factory; }
	UINT SizeX() const { return this->sizeX; }
	UINT SizeY() const { return this->sizeY; }
	double Dpi() const { return this->dpi; }
private:
	const outputImage& operator=(const outputImage &rhs) = delete;
	const Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
	const UINT sizeX;
	const UINT sizeY;
	const double dpi;
};
	}
}
#endif // OUTPUT_H