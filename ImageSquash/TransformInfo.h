#pragma once

typedef enum
{
	PNG,
	JPG
} ImageType;

class TransformInfo
{
public:
	TransformInfo(const ImageType type, const std::wstring& inPath, const std::wstring& outPath, const std::wstring & profilePath, const double dpi);
	~TransformInfo(void);

	const std::wstring& InputPath() const
	{
		return this->inputPath;
	}

	const std::wstring& OutputPath() const
	{
		return this->outputPath;
	}

	const std::wstring& ProfilePath() const
	{
		return this->profilePath;
	}

	const double Dpi() const
	{
		return this->dpi;
	}

	const ImageType type() const
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

