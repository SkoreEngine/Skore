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

#include "Skore/Core/Math.hpp"
#include "Skore/Scene/Component.hpp"


namespace Skore
{
	class SK_API CharacterController : public Component
	{
	public:
		SK_CLASS(CharacterController, Component);

		void Create(ComponentSettings& settings) override;
		void Destroy() override;

		f32  GetHeight() const;
		void SetHeight(f32 height);
		f32  GetRadius() const;
		void SetRadius(f32 radius);
		f32  GetMaxSlopeAngle() const;
		void SetMaxSlopeAngle(f32 maxSlopeAngle);
		f32  GetMaxStrength() const;
		void SetMaxStrength(f32 maxStrength);
		f32  GetMass() const;
		void SetMass(f32 mass);
		f32  GetPredictiveContactDistance() const;
		void SetPredictiveContactDistance(f32 predictiveContactDistance);
		u32  GetMaxCollisionIterations() const;
		void SetMaxCollisionIterations(u32 maxCollisionIterations);
		u32  GetMaxConstraintIterations() const;
		void SetMaxConstraintIterations(u32 maxConstraintIterations);
		f32  GetMinTimeRemaining() const;
		void SetMinTimeRemaining(f32 minTimeRemaining);
		f32  GetCollisionTolerance() const;
		void SetCollisionTolerance(f32 collisionTolerance);
		f32  GetCharacterPadding() const;
		void SetCharacterPadding(f32 characterPadding);
		u32  GetMaxNumHits() const;
		void SetMaxNumHits(u32 maxNumHits);
		f32  GetHitReductionCosMaxAngle() const;
		void SetHitReductionCosMaxAngle(f32 hitReductionCosMaxAngle);
		f32  GetPenetrationRecoverySpeed() const;
		void SetPenetrationRecoverySpeed(f32 penetrationRecoverySpeed);
		Vec3 GetShapeOffset() const;
		void SetShapeOffset(const Vec3& shapeOffset);

		Vec3 GetUp() const;
		void SetUp(const Vec3& up);
		Vec3 GetLinearVelocity() const;
		void SetLinearVelocity(const Vec3& linearVelocity);
		bool IsOnGround() const;
		void SetOnGround(bool onGround);

		static void RegisterType(NativeReflectType<CharacterController>& type);

	private:
		f32  height = 1.35f;
		f32  radius = 0.3f;
		f32  maxSlopeAngle{45.f};
		f32  maxStrength = 100.0f;
		f32  mass = 70.0f;
		f32  predictiveContactDistance = 0.1f;
		u32  maxCollisionIterations = 5;
		u32  maxConstraintIterations = 15;
		f32  minTimeRemaining = 1.0e-4f;
		f32  collisionTolerance = 1.0e-3f;
		f32  characterPadding = 0.02f;
		u32  maxNumHits = 256;
		f32  hitReductionCosMaxAngle = 0.999f;
		f32  penetrationRecoverySpeed = 1.0f;
		Vec3 shapeOffset{};

		Vec3 up = Vec3::AxisY();
		Vec3 linearVelocity{};
		bool onGround{};
	};
}
