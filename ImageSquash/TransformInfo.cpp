#include "StdAfx.h"
#include "TransformInfo.h"

TransformInfo::TransformInfo(const ImageType type, const std::wstring& inPath, const std::wstring& outPath, const std::wstring & profilePath, const double dpi)
	:imageType(type), inputPath(inPath), outputPath(outPath), profilePath(profilePath), dpi(dpi)
{
}
