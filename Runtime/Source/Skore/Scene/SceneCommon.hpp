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

namespace Skore
{

	struct EntityResource
	{
		enum
		{
			Name,
			Deactivated,
			Locked,
			Transform,
			BoneIndex,
			Components,
			Children,
		};
	};

	struct EntityEventType
	{
		constexpr static i32 EntityActivated = 100;
		constexpr static i32 EntityDeactivated = 101;

		constexpr static i32 TransformUpdated = 1000;
		constexpr static i32 CollectPhysicsShapes = 1100;

	};

	enum class EntityFlags : u64
	{
		None        = 0,
		HasPhysics  = 1 << 0,
		HasGraphics = 1 << 1
	};

	struct ComponentDesc
	{
		bool          allowMultiple = true;
		Array<TypeID> dependencies{};
		String		  category = "";
	};

	struct EntityEventDesc
	{
		i64     type = 0;
		VoidPtr eventData = nullptr;
	};
}
