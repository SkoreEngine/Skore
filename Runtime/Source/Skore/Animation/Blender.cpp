#include "Blender.hpp"

#include "BoneMask.hpp"

namespace Skore::Anim
{
	namespace
	{
		// Normal interpolation.
		struct NormalBlend
		{
			static Quat Rotation(const Quat& a, const Quat& b, f32 t) { return FastSlerp(a, b, t); }
			static Vec3 Translation(const Vec3& a, const Vec3& b, f32 t) { return Vec3::Mix(a, b, t); }
			static f32  Scale(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
		};

		// Additive: apply `b` (a delta) on top of `a`.
		struct AdditiveBlendFn
		{
			static Quat Rotation(const Quat& a, const Quat& b, f32 t)
			{
				Quat target = a * b; // Esoterica: b * a; operands reversed for Skore Hamilton
				return Quat::Slerp(a, target, t);
			}
			static Vec3 Translation(const Vec3& a, const Vec3& b, f32 t) { return a + b * t; }
			static f32  Scale(f32 a, f32 b, f32 t) { return a + b * t; }
		};

		template<typename BF>
		void ParentSpaceBlendT(Skeleton::LOD lod, const Pose* src, const Pose* tgt, f32 w, Pose* result, bool layered)
		{
			Pose::State finalState = (src->IsAdditivePose() && tgt->IsAdditivePose()) ? Pose::State::AdditivePose : Pose::State::Pose;

			if (w == 0.0f) // fully in source
			{
				if (src != result) result->CopyFrom(*src);
			}
			else if (!layered && w == 1.0f) // fully in target (skip when layering)
			{
				if (tgt != result) result->CopyFrom(*tgt);
			}
			else
			{
				i32                 numBones = result->GetNumBones(lod);
				const Array<XForm>& s = src->GetTransforms();
				const Array<XForm>& t = tgt->GetTransforms();
				Array<XForm>&       r = result->GetWritableTransforms();
				for (i32 b = 0; b < numBones; ++b)
				{
					r[b].m_rotation    = BF::Rotation(s[b].m_rotation, t[b].m_rotation, w);
					r[b].m_translation = BF::Translation(s[b].m_translation, t[b].m_translation, w);
					r[b].m_scale       = BF::Scale(s[b].m_scale, t[b].m_scale, w);
				}
				result->ClearModelSpaceTransforms();
			}

			result->SetState(finalState);
		}

		template<typename BF>
		void ParentSpaceBlendMaskedT(Skeleton::LOD lod, const Pose* src, const Pose* tgt, f32 w, const BoneMask* mask, Pose* result, bool layered)
		{
			Pose::State finalState = (src->IsAdditivePose() && tgt->IsAdditivePose()) ? Pose::State::AdditivePose : Pose::State::Pose;

			if (w == 0.0f)
			{
				if (src != result) result->CopyFrom(*src);
			}
			else
			{
				i32                 numBones = result->GetNumBones(lod);
				const Array<XForm>& s = src->GetTransforms();
				const Array<XForm>& t = tgt->GetTransforms();
				Array<XForm>&       r = result->GetWritableTransforms();
				for (i32 b = 0; b < numBones; ++b)
				{
					f32 bw = w * mask->GetWeight(b); // per-bone weight multiplies the global weight
					if (bw == 0.0f)
					{
						r[b] = s[b];
					}
					else if (!layered && bw == 1.0f)
					{
						r[b] = t[b];
					}
					else
					{
						r[b].m_rotation    = BF::Rotation(s[b].m_rotation, t[b].m_rotation, bw);
						r[b].m_translation = BF::Translation(s[b].m_translation, t[b].m_translation, bw);
						r[b].m_scale       = BF::Scale(s[b].m_scale, t[b].m_scale, bw);
					}
				}
				result->ClearModelSpaceTransforms();
			}

			result->SetState(finalState);
		}

		template<typename BF>
		void BlendToReferencePoseT(Skeleton::LOD lod, const Pose* src, f32 w, Pose* result)
		{
			Pose::State finalState = src->IsAdditivePose() ? Pose::State::AdditivePose : Pose::State::Pose;

			if (w == 0.0f)
			{
				if (src != result) result->CopyFrom(*src);
			}
			else if (w == 1.0f)
			{
				result->Reset(finalState == Pose::State::AdditivePose ? Pose::Type::ZeroPose : Pose::Type::ReferencePose);
			}
			else
			{
				const Array<XForm>& ref = src->GetSkeleton()->GetParentSpaceReferencePose();
				const Array<XForm>& s = src->GetTransforms();
				Array<XForm>&       r = result->GetWritableTransforms();
				i32                 numBones = result->GetNumBones(lod);
				for (i32 b = 0; b < numBones; ++b)
				{
					r[b].m_rotation    = BF::Rotation(s[b].m_rotation, ref[b].m_rotation, w);
					r[b].m_translation = BF::Translation(s[b].m_translation, ref[b].m_translation, w);
					r[b].m_scale       = BF::Scale(s[b].m_scale, ref[b].m_scale, w);
				}
				result->ClearModelSpaceTransforms();
			}

			result->SetState(finalState);
		}

		template<typename BF>
		void BlendFromReferencePoseT(Skeleton::LOD lod, const Pose* tgt, f32 w, Pose* result)
		{
			Pose::State finalState = tgt->IsAdditivePose() ? Pose::State::AdditivePose : Pose::State::Pose;

			if (w == 0.0f)
			{
				result->Reset(finalState == Pose::State::AdditivePose ? Pose::Type::ZeroPose : Pose::Type::ReferencePose);
			}
			else if (w == 1.0f)
			{
				if (tgt != result) result->CopyFrom(*tgt);
			}
			else
			{
				const Array<XForm>& ref = tgt->GetSkeleton()->GetParentSpaceReferencePose();
				const Array<XForm>& t = tgt->GetTransforms();
				Array<XForm>&       r = result->GetWritableTransforms();
				i32                 numBones = result->GetNumBones(lod);
				for (i32 b = 0; b < numBones; ++b)
				{
					r[b].m_rotation    = BF::Rotation(ref[b].m_rotation, t[b].m_rotation, w);
					r[b].m_translation = BF::Translation(ref[b].m_translation, t[b].m_translation, w);
					r[b].m_scale       = BF::Scale(ref[b].m_scale, t[b].m_scale, w);
				}
				result->ClearModelSpaceTransforms();
			}

			result->SetState(finalState);
		}
	} // namespace

	//-------------------------------------------------------------------------

	void Blender::ParentSpaceBlend(Skeleton::LOD lod, const Pose* source, const Pose* target, f32 blendWeight, const BoneMask* boneMask, Pose* result)
	{
		if (boneMask != nullptr)
		{
			ParentSpaceBlendMaskedT<NormalBlend>(lod, source, target, blendWeight, boneMask, result, false);
		}
		else
		{
			ParentSpaceBlendT<NormalBlend>(lod, source, target, blendWeight, result, false);
		}
	}

	void Blender::AdditiveBlend(Skeleton::LOD lod, const Pose* source, const Pose* target, f32 blendWeight, const BoneMask* boneMask, Pose* result)
	{
		if (boneMask != nullptr)
		{
			ParentSpaceBlendMaskedT<AdditiveBlendFn>(lod, source, target, blendWeight, boneMask, result, true);
		}
		else
		{
			ParentSpaceBlendT<AdditiveBlendFn>(lod, source, target, blendWeight, result, true);
		}
	}

	void Blender::ParentSpaceBlendToReferencePose(Skeleton::LOD lod, const Pose* source, f32 blendWeight, const BoneMask* boneMask, Pose* result)
	{
		(void) boneMask; // unused, matches Esoterica
		BlendToReferencePoseT<NormalBlend>(lod, source, blendWeight, result);
	}

	void Blender::ParentSpaceBlendFromReferencePose(Skeleton::LOD lod, const Pose* target, f32 blendWeight, const BoneMask* boneMask, Pose* result)
	{
		(void) boneMask;
		BlendFromReferencePoseT<NormalBlend>(lod, target, blendWeight, result);
	}

	void Blender::ApplyAdditiveToReferencePose(Skeleton::LOD lod, const Pose* additivePose, f32 blendWeight, const BoneMask* boneMask, Pose* result)
	{
		(void) boneMask;

		if (blendWeight == 0.0f)
		{
			result->Reset(Pose::Type::ReferencePose);
		}
		else
		{
			const Array<XForm>& ref = additivePose->GetSkeleton()->GetParentSpaceReferencePose();
			const Array<XForm>& a = additivePose->GetTransforms();
			Array<XForm>&       r = result->GetWritableTransforms();
			i32                 numBones = result->GetNumBones(lod);
			for (i32 b = 0; b < numBones; ++b)
			{
				r[b].m_rotation    = AdditiveBlendFn::Rotation(ref[b].m_rotation, a[b].m_rotation, blendWeight);
				r[b].m_translation = AdditiveBlendFn::Translation(ref[b].m_translation, a[b].m_translation, blendWeight);
				r[b].m_scale       = AdditiveBlendFn::Scale(ref[b].m_scale, a[b].m_scale, blendWeight);
			}
			result->ClearModelSpaceTransforms();
		}

		result->SetState(Pose::State::Pose);
	}

	void Blender::ModelSpaceBlend(Skeleton::LOD lod, const Pose* base, const Pose* layer, f32 layerWeight, const BoneMask* mask, Pose* result)
	{
		// Requires a bone mask. Layer-only; verify against Esoterica when layers land (S6).
		if (layerWeight == 0.0f)
		{
			if (base != result) result->CopyFrom(*base);
			return;
		}

		const Array<i32>& parentIndices = base->GetSkeleton()->GetParentBoneIndices();
		i32               numBones = result->GetNumBones(lod);

		Array<Quat> baseRot;
		Array<Quat> layerRot;
		Array<Quat> resultRot;
		baseRot.Resize(numBones);
		layerRot.Resize(numBones);
		resultRot.Resize(numBones);

		const Array<XForm>& baseT = base->GetTransforms();
		const Array<XForm>& layerT = layer->GetTransforms();
		Array<XForm>&       r = result->GetWritableTransforms();

		// Model-space rotations (Skore: childModel = parentModel * childLocal).
		baseRot[0] = baseT[0].m_rotation;
		layerRot[0] = layerT[0].m_rotation;
		for (i32 b = 1; b < numBones; ++b)
		{
			i32 p = parentIndices[b];
			baseRot[b]  = baseRot[p] * baseT[b].m_rotation;
			layerRot[b] = layerRot[p] * layerT[b].m_rotation;
		}

		// Root blended in local space.
		f32 bw0 = mask->GetWeight(0);
		if (bw0 != 0.0f)
		{
			r[0].m_translation = NormalBlend::Translation(baseT[0].m_translation, layerT[0].m_translation, bw0);
			r[0].m_scale       = NormalBlend::Scale(baseT[0].m_scale, layerT[0].m_scale, bw0);
			resultRot[0]       = NormalBlend::Rotation(baseT[0].m_rotation, layerT[0].m_rotation, bw0);
		}
		else
		{
			resultRot[0] = baseT[0].m_rotation;
		}
		r[0].m_rotation = resultRot[0];

		// Blend model-space rotations per bone, convert back to local space.
		for (i32 b = 1; b < numBones; ++b)
		{
			f32 bw = mask->GetWeight(b);
			if (bw == 0.0f) // masked out -> use base local
			{
				resultRot[b] = baseRot[b];
				r[b] = baseT[b];
			}
			else
			{
				r[b].m_translation = NormalBlend::Translation(baseT[b].m_translation, layerT[b].m_translation, bw); // translation in local space
				r[b].m_scale       = NormalBlend::Scale(baseT[b].m_scale, layerT[b].m_scale, bw);
				resultRot[b]       = NormalBlend::Rotation(baseRot[b], layerRot[b], bw);
				i32 p = parentIndices[b];
				r[b].m_rotation = QuatDelta(resultRot[p], resultRot[b]); // model -> local
			}
		}

		result->ClearModelSpaceTransforms();
		result->SetState(Pose::State::Pose);

		// Apply the overall layer weight: blend base -> the masked model-space result.
		ParentSpaceBlend(lod, base, result, layerWeight, nullptr, result);
		result->ClearModelSpaceTransforms();
		result->SetState(Pose::State::Pose);
	}

	XForm Blender::BlendRootMotionDeltas(const XForm& source, const XForm& target, f32 blendWeight, RootMotionBlendMode mode)
	{
		XForm result;

		if (mode == RootMotionBlendMode::IgnoreTarget)
		{
			result = source;
		}
		else if (mode == RootMotionBlendMode::IgnoreSource)
		{
			result = target;
		}
		else if (blendWeight <= 0.0f)
		{
			result = source;
		}
		else if (blendWeight >= 1.0f)
		{
			result = target;
		}
		else if (mode == RootMotionBlendMode::Additive)
		{
			result.m_rotation    = AdditiveBlendFn::Rotation(source.m_rotation, target.m_rotation, blendWeight);
			result.m_translation = AdditiveBlendFn::Translation(source.m_translation, target.m_translation, blendWeight);
			result.m_scale       = 1.0f; // root motion forces unit scale (Esoterica GetWithW1)
		}
		else // regular blend
		{
			result.m_rotation    = NormalBlend::Rotation(source.m_rotation, target.m_rotation, blendWeight);
			result.m_translation = NormalBlend::Translation(source.m_translation, target.m_translation, blendWeight);
			result.m_scale       = 1.0f;
		}

		return result;
	}
}
