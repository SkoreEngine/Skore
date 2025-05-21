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
#include "Skore/Core/Event.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	using OnInit = EventType<"Skore::OnInit"_h, void()>;
	using OnBeginFrame = EventType<"Skore::OnBeginFrame"_h, void()>;
	using OnUpdate = EventType<"Skore::OnUpdate"_h, void()>;
	using OnEndFrame = EventType<"Skore::OnEndFrame"_h, void()>;
	using OnShutdown = EventType<"Skore::OnShutdown"_h, void()>;
	using OnUIRender = EventType<"Skore::OnUIRender"_h, void()>;
	using OnShutdownRequest = EventType<"Skore::OnShutdownRequest"_h, void(bool* canClose)>;
	using OnDropFileCallback = EventType<"Skore::OnDropFileCallback"_h, void(StringView path)>;
}
