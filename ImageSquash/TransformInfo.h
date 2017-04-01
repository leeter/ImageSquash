#ifndef TRANSFORM_H
#define TRANSFORM_H

#	if defined(_MSC_VER)
#		pragma once
#endif

enum class ImageType
{
	PNG,
	JPG
};

class TransformInfo
{
public:
	TransformInfo(const ImageType type, const std::wstring& inPath, const std::wstring& outPath, const std::wstring & profilePath, const double dpi);
	// ~TransformInfo(void){};

	std::wstring InputPath() const
	{
		return this->inputPath;
	}

	std::wstring OutputPath() const
	{
		return this->outputPath;
	}

	std::wstring ProfilePath() const
	{
		return this->profilePath;
	}

	double Dpi() const noexcept
	{
		return this->dpi;
	}

	ImageType type() const noexcept
	{
		return this->imageType;
	}

private:
	std::wstring inputPath;
	std::wstring outputPath;
	std::wstring profilePath;
	double dpi;
	ImageType imageType;
};

#endif

