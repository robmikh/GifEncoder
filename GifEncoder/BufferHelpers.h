#pragma once

inline uint32_t ComputePaddedBufferSize(size_t size)
{
	auto paddedSize = size;
	if (paddedSize < 16)
	{
		paddedSize = 16;
	}
	else
	{
		auto remainder = paddedSize % 16;
		paddedSize = paddedSize + remainder;
	}
	return static_cast<uint32_t>(paddedSize);
}

template <typename T>
inline T ReadFromBuffer(
	winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
	winrt::com_ptr<ID3D11Buffer> const& stagingBuffer)
{
	D3D11_BUFFER_DESC desc = {};
	stagingBuffer->GetDesc(&desc);

	assert(sizeof(T) <= desc.ByteWidth);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	d3dContext->Map(stagingBuffer.get(), 0, D3D11_MAP_READ, 0, &mapped);

	T result = {};
	result = *reinterpret_cast<T*>(mapped.pData);

	d3dContext->Unmap(stagingBuffer.get(), 0);

	return result;
}
