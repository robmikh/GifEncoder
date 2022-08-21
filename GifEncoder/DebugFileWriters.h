#pragma once

inline std::string ImageViewerFileNameFromSize(std::string const& name, uint32_t width, uint32_t height)
{
    std::stringstream stringStream;
    stringStream << name << "_" << width << "x" << height << ".bin";
    return stringStream.str();
}

inline void WriteIndexedPixelBytesToFileAsBgra8(std::string const& path, std::vector<uint8_t> const& indexPixelBytes)
{
    std::ofstream file(path, std::ios::out | std::ios::binary);
    for (auto&& index : indexPixelBytes)
    {
        std::vector<uint8_t> bgraBytes(4, 0);
        bgraBytes[0] = index;
        bgraBytes[1] = index;
        bgraBytes[2] = index;
        bgraBytes[3] = 255;
        file.write(reinterpret_cast<const char*>(bgraBytes.data()), bgraBytes.size());
    }
}

inline void WriteBgra8PixelsToFile(std::string const& path, std::vector<uint8_t> const& bytes)
{
    std::ofstream file(path, std::ios::out | std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
