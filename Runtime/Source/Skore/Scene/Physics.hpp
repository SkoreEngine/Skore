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

#include "Entity.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
	class GPUCommandBuffer;
	class GPUPipeline;

	constexpr u32 DebugPhysicsVertexSize = 36;

	enum class CollisionDetectionType
	{
		Discrete,
		LinearCast
	};

	struct PhysicsSettings
	{
		enum
		{
			MaxBodies,              //UInt
			MaxBodyPairs,           //UInt
			MaxContactConstraints,  //UInt
			PhysicsTicksPerSeconds, //UInt
		};
	};

	enum class BodyShapeType
	{
		None     = 0,
		Plane    = 1,
		Box      = 2,
		Sphere   = 3,
		Capsule  = 4,
		Cylinder = 5,
		Mesh     = 6,
		Convex   = 7,
		Terrain  = 8
	};

	struct BodyShapeBuilder
	{
		BodyShapeType bodyShape = BodyShapeType::None;
		Vec3          size{1.0f, 1.0f, 1.0f};
		Vec3          center{0.0f, 0.0f, 0.0f};
		f32           height{1.0};
		f32           radius{0.5};
		f32           density = 1000;
		bool          sensor = false;
	};

	struct ShapeCollector
	{
		Array<BodyShapeBuilder> shapes;
	};

	class SK_API PhysicsScene
	{
	public:
		struct Context;
		SK_NO_COPY_CONSTRUCTOR(PhysicsScene);

		PhysicsScene();
		~PhysicsScene();

		void RegisterPhysicsEntity(Entity* entity);
		void UnregisterPhysicsEntity(Entity* entity);
		void PhysicsEntityRequireUpdate(Entity* entity);
		void UpdateTransform(Entity* entity);

		void DrawEntities(GPUCommandBuffer* cmd, GPUPipeline* pipeline, const HashSet<Entity*>& entities);

		friend class Scene;
	private:
		Context* context = nullptr;

		void ExecuteEvents();
		void OnUpdate();
		void OnSceneActivated();
		void OnSceneDeactivated();
	};

	class SK_API Physics
	{
	public:

	};
}
