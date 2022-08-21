#pragma once
#include "IComposedFrameProvider.h"
#include "RaniFormat.h"

struct RaniComposedFrameProvider : IComposedFrameProvider
{
	RaniComposedFrameProvider(std::unique_ptr<RaniProject>&& project)
	{
		m_project = std::move(project);
	}
	~RaniComposedFrameProvider() {}

	uint32_t Width() override { return m_project->Width; }
	uint32_t Height() override { return m_project->Height; }
	std::vector<ComposedFrame> GetFrames(
		winrt::com_ptr<ID3D11Device> const& d3dDevice,
		winrt::com_ptr<ID2D1DeviceContext> const& d2dContext) override
	{
		auto textures = ComposeFrames(m_project, d3dDevice, d2dContext);
		std::vector<ComposedFrame> frames;
		frames.reserve(textures.size());
		for (auto i = 0; i < textures.size(); i++)
		{
			auto&& texture = textures[i];
			winrt::Windows::Foundation::TimeSpan frameTime = {};
			if (i > 0)
			{
				frameTime = m_project->FrameTime;
			}
			frames.push_back(ComposedFrame{ texture, frameTime });
		}
		return frames;
	}

private:
	std::unique_ptr<RaniProject> m_project;
};