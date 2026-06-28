#pragma once
#include "Skore/Core/Object.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	enum class TextureChannel : u8
	{
		Red   = 0,
		Green = 1,
		Blue  = 2,
		Alpha = 3
	};

	struct ShaderVariantResource
	{
		enum
		{
			Name,         //String
			Spriv,        //Blob
			PipelineDesc, //Subobject
			Stages,       //SubobjectList
		};
	};


	struct ShaderResource
	{
		enum
		{
			Name,        //String
			Variants,    //SubobjectList
			RayHitGroup, //Uint
		};

		static RID    GetVariant(RID shader, StringView name);
		SK_API static String GetVariantName(Span<String> macros);
		static u32    GetRayHitGroup(RID shader);
	};


	struct TextureImageResource
	{
		enum
		{
			Extent,          //Vec2
			Mip,             //Uint
			ArrayLayer,      //Uint
			DataOffset,      //Uint
			DataSize,        //Uint
			UncompressedSize //Uint
		};
	};


	struct TextureResource
	{
		enum
		{
			Name,                  //String
			Format,                //Enum
			MipLevels,             //Uint
			WrapMode,              //Enum
			FilterMode,            //Enum
			CompressionMode,       //Enum
			TotalUncompressedSize, //Uint
			Images,                //SubobjectList
			PixelData,             //Buffer
		};
	};

	struct MeshPrimitive
	{
		u32  firstIndex;
		u32  indexCount;
		u32  materialIndex;
		AABB aabb;
	};

	struct MeshStaticVertex
	{
		Vec3 position;
		Vec3 normal;
		Vec2 uv;
		Vec2 uv2;
		Vec3 color;
		Vec4 tangent;
	};

	struct VertexAttributeResource
	{
		enum
		{
			Name,   //String
			Offset, //Uint
			Size,   //Uint
		};
	};

	struct VertexLayoutResource
	{
		enum
		{
			Attributes, //SubObjectList
			Stride,     //Uint
		};
	};

	struct MeshLodResource
	{
		enum
		{
			LodNumber,       //Uint
			VerticesOffset,  //Uint
			VerticesCount,   //Uint
			IndicesOffset,   //Uint
			IndicesCount,    //Uint
			PrimitiveCount,  //Uint
			PrimitiveOffset, //Uint
			Distance,        //Float
			ScreenSize,      //Float
		};
	};

	struct MeshResource
	{
		enum
		{
			Name,             //String
			Materials,        //ReferenceArray
			Skin,             //Subobject
			AABBMin,          //Vec3
			AABBMax,          //Vec3
			LightmapSizeHint, //Vec2
			VertexLayout,     //SubObject
			MeshLODs,         //SubobjectList
			MeshData,         //Buffer
		};
	};

	struct MaterialResource
	{
		enum class MaterialType
		{
			Opaque,
			SkyboxEquirectangular,
		};

		struct Buffer
		{
			Vec3 baseColor;
			f32  alphaCutoff;

			f32 metallic;
			f32 roughness;

			i32 textureFlags;
			i32 textureProps;

			Vec2 uvScale;
			Vec2 _pad;
		};

		enum class MaterialAlphaMode
		{
			None   = 0,
			Opaque = 1,
			Mask   = 2,
			Blend  = 3
		};


		enum
		{
			Name,                    //String
			Type,                    //Enum
			BaseColor,               //Color
			BaseColorTexture,        //Reference
			NormalTexture,           //Reference
			NormalMultiplier,        //Float
			Metallic,                //Float
			MetallicTexture,         //Reference,
			MetallicTextureChannel,  //Enum
			Roughness,               //Float
			RoughnessTexture,        //Reference,
			RoughnessTextureChannel, //Enum
			EmissiveColor,           //Color
			EmissiveFactor,          //Float
			EmissiveTexture,         //Reference
			OcclusionTexture,        //Reference
			OcclusionStrength,       //Float
			OcclusionTextureChannel, //Enum
			AlphaCutoff,             //Float
			AlphaMode,               //Enum
			UvScale,                 //Vec2
			SphericalTexture,        //Reference
			Exposure,                //Float
			BackgroundColor,         //Color
		};
	};

	using MaterialArray = Array<TypedRID<MaterialResource>>;

	//Node-based material system (editor-authored shader graph).
	//A graph owns a list of nodes and connections; one node is flagged as the output (master) node.
	struct MaterialGraphNodeResource
	{
		enum
		{
			Type,          //String        - node type id (matches MaterialNodeRegistry)
			Position,      //Vec2          - canvas position
			Value,         //Vec4          - literal/default value for constant- and parameter-style nodes
			Texture,       //Reference     - optional TextureResource for texture nodes
			InputValues,   //SubObjectList - per-input-pin literal overrides used when a pin is unconnected
			ParameterName, //String        - exposed name for Parameters-category nodes; drives material instance overrides
		};
	};

	//Literal value an input pin uses when nothing is connected to it. Stored sparsely: only pins the
	//user has edited get an entry; the rest fall back to the node type's pin default.
	struct MaterialGraphPinValueResource
	{
		enum
		{
			PinIndex, //UInt - input pin index on the owning node
			Value,    //Vec4 - literal value (components used depend on the pin type)
		};
	};

	struct MaterialGraphConnectionResource
	{
		enum
		{
			OutputNode, //Reference (MaterialGraphNodeResource) - value source
			OutputPin,  //UInt      - source output pin index
			InputNode,  //Reference (MaterialGraphNodeResource) - value consumer
			InputPin,   //UInt      - destination input pin index
		};
	};

	struct MaterialGraphResource
	{
		//How the material's alpha is interpreted. Opaque ignores opacity entirely; Mask clips the pixel
		//when the Opacity Mask output falls below MaskCutoff; Blend writes the Opacity output as alpha.
		enum class GraphAlphaMode : u8
		{
			Opaque,
			Mask,
			Blend,
		};

		enum
		{
			Name,        //String
			Nodes,       //SubObjectList (MaterialGraphNodeResource)
			Connections, //SubObjectList (MaterialGraphConnectionResource)
			OutputNode,  //Reference (the master/output node)
			AlphaMode,   //Enum (GraphAlphaMode)
			MaskCutoff,  //Float - clip threshold used when AlphaMode == Mask
		};
	};

	struct AnimationKeyFrame
	{
		f64  time = 0.f;
		Vec3 position = {};
		Quat rotation = {};
		Vec3 scale = {};
	};

	struct AnimationKeyFrameResource
	{
		enum
		{
			Time,     //Float
			Position, //Vec
			Rotation, //Quat
			Scale,    //Vec3
		};
	};

	struct AnimationChannelResource
	{
		enum
		{
			Name,         //String
			KeyFrames,    //SubobjectList
			BufferOffset, //Uint
		};
	};

	struct AnimationClipResource
	{
		enum
		{
			Name,           //String
			Duration,       //Float
			NumFrames,      //UInt
			FrameRate,      //Float
			TimeBegin,      //Float
			TimeEnd,        //Float
			Channels,       //SubobjectList
			KeyFramesBuffer //Buffer
		};
	};

	template <>
	struct Hash<MeshStaticVertex>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const MeshStaticVertex& value)
		{
			return (Hash<Vec3>::Value(value.position) ^ Hash<Vec3>::Value(value.normal) << 1) >> 1 ^ Hash<Vec2>::Value(value.uv) << 1;
		}
	};

	inline bool operator==(const MeshStaticVertex& lhs, const MeshStaticVertex& rhs)
	{
		return lhs.position == rhs.position
			&& lhs.normal == rhs.normal
			&& lhs.uv == rhs.uv
			&& lhs.color == rhs.color;
	}

	inline bool operator!=(const MeshStaticVertex& lhs, const MeshStaticVertex& rhs)
	{
		return !(lhs == rhs);
	}

	struct FontMetrics : Object
	{
		SK_CLASS(FontMetrics, Object);

		f64 emSize;
		f64 ascenderY;
		f64 descenderY;
		f64 lineHeight;
		f64 underlineY;
		f64 underlineThickness;

		static void RegisterType(NativeReflectType<FontMetrics>& type);
	};

	struct FontAtlasData
	{
		Vec2      extent;
		Array<u8> pixels;

		static void RegisterType(NativeReflectType<FontAtlasData>& type);
	};

	struct FontKerning
	{
		u32 first;
		u32 second;
		f32 offset;

		static void RegisterType(NativeReflectType<FontKerning>& type);
	};

	struct FontGlyph
	{
		u32  codepoint;
		u32  index;
		f32  advance;
		Vec4 atlasBounds;
		Vec4 planeBounds;

		static void RegisterType(NativeReflectType<FontGlyph>& type);
	};

	struct FontResourceData : Object
	{
		SK_CLASS(FontResourceData, Object);

		FontMetrics        metrics;
		Array<FontGlyph>   glyphs;
		Array<FontKerning> kernings;
		FontAtlasData      atlas;
		f32                maxHeightGlyph;

		static void RegisterType(NativeReflectType<FontResourceData>& type);
	};

	struct FontResource
	{
		enum
		{
			Name,                    //String
			FontData,                //Blob
			FontDataUncompressedSize //Uint
		};
	};

	struct SkinBindResource
	{
		enum
		{
			Pose, //Mat4
		};
	};


	struct SkinResource
	{
		enum
		{
			Name,  //String
			Binds, //SubobjectList
		};
	};


	enum class AnimationParameterType
	{
		Float,
		Int,
		Bool,
		Trigger
	};

	struct AnimationParameterResource
	{
		enum
		{
			Name,
			Type,       //Enum (AnimationParameterType)
			FloatValue, //Float
			IntValue,   //Int
			BoolValue,  //Bool
		};
	};

	enum class AnimationTransitionCondition
	{
		Greater,
		Less,
		Equal,
		NotEqual,
		True,
		False,
	};


	struct AnimationTransitionConditionResource
	{
		enum
		{
			Parameter, //Reference
			Condition, //Enum (AnimationTransitionCondition)
			Value      //Float
		};
	};


	struct AnimationTransitionResource
	{
		enum
		{
			From,             //Reference
			To,               //Reference
			CrossFadeTime,    //Float
			HasExitTime,      //Bool
			ExitTime,         //Float  (normalized 0-1, fraction of source clip)
			FixedDuration,    //Bool   (true = seconds, false = normalized)
			TransitionOffset, //Float  (normalized 0-1, where destination starts)
			Conditions,       //SubObjectList
		};
	};

	struct AnimationStateResource
	{
		enum
		{
			Name,      //String
			Animation, //Reference
			Position,  //Vec2
			Speed      //Float
		};
	};

	enum class AnimationLayerBlendMode
	{
		Override,
		Additive
	};

	enum class RootMotionMode
	{
		None,
		Transform,
		Velocity
	};

	enum class RootMotionAxes
	{
		XZ,
		XYZ
	};

	struct AnimationLayerResource
	{
		enum
		{
			Name,           //String
			States,         //SubObjectList
			Transitions,    //SubObjectList
			DefaultState,   //Reference
			Weight,         //Float
			Avatar,         //Reference
			BlendMode,      //Enum (AnimationLayerBlendMode)
			RootMotion,     //Enum (RootMotionMode)
			RootMotionAxis, //Enum (RootMotionAxes)
			ApplyRotation,  //Bool
		};
	};

	struct AnimationAvatarBoneResource
	{
		enum
		{
			BoneName,	//String
			Enabled,	//Bool
			Children  // SubObjectList
		};
	};

	struct AnimationAvatarResource
	{
		enum
		{
			Name,				//String
			RootBone,		//Subobject(AnimationAvatarBoneResource)
		};
	};

	struct AnimationControllerResource
	{
		enum
		{
			Name,						//String
			PreviewEntity,	//Reference
			Layers,					//SubObjectList
			Parameters,			//SubObjectList
			Avatars,				//SubObjectList
		};
	};

	struct DCCAsset
	{
		enum
		{
			Name,           //String
			Meshes,         //SubObjectList
			Textures,       //SubObjectList
			Materials,      //SubObjectList
			AnimationClips, //SubObjectList
			RootEntity,     //SubObject
		};
	};
}
