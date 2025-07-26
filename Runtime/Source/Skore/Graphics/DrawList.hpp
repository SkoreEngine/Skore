// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore {
	class GPUBuffer;
}

namespace Skore {
	class GPUPipeline;
}

namespace Skore
{
	class GPURenderPass;
	class GPUCommandBuffer;

	class SK_API DrawList2D
	{
	public:
		DrawList2D();
		~DrawList2D();

		struct Vertex
		{
			Vec2 position;
			Vec2 uv;
			u32  color;
		};

		void AddRectFilled(const Vec2& min, const Vec2& max, const Color& color);

		void DrawItems(GPURenderPass* renderPass, GPUCommandBuffer* cmd);

	private:
		Array<Vertex> vertices;
		Array<u32>    indices;

		GPUBuffer* vertexBuffer = nullptr;
		GPUBuffer* indexBuffer  = nullptr;

		GPUPipeline*  drawListPipeline = nullptr;
	};
}
