#pragma once

#include "TextureAsset.hpp"
#include "Skore/Common.hpp"
#include "Skore/IO/Asset.hpp"

namespace Skore
{
    class SK_API MaterialAsset : public Asset
    {
    public:
        SK_BASE_TYPES(Asset);

        Color         GetBaseColor() const;
        void          SetBaseColor(const Color& baseColor);
        TextureAsset* GetBaseColorTexture() const;
        void          SetBaseColorTexture(TextureAsset* baseColorTexture);
        TextureAsset* GetNormalTexture() const;
        void          SetNormalTexture(TextureAsset* normalTexture);
        f32           GetNormalMultiplier() const;
        void          SetNormalMultiplier(f32 normalMultiplier);
        f32           GetMetallic() const;
        void          SetMetallic(f32 metallic);
        TextureAsset* GetMetallicTexture() const;
        void          SetMetallicTexture(TextureAsset* metallicTexture);
        f32           GetRoughness() const;
        void          SetRoughness(f32 roughness);
        TextureAsset* GetRoughnessTexture() const;
        void          SetRoughnessTexture(TextureAsset* roughnessTexture);
        TextureAsset* GetMetallicRoughnessTexture() const;
        void          SetMetallicRoughnessTexture(TextureAsset* metallicRoughnessTexture);
        TextureAsset* GetAoTexture() const;
        void          SetAoTexture(TextureAsset* aoTexture);
        TextureAsset* GetEmissiveTexture() const;
        void          SetEmissiveTexture(TextureAsset* emissiveTexture);
        Vec3          GetEmissiveFactor() const;
        void          SetEmissiveFactor(const Vec3& emissiveFactor);
        f32           GetAlphaCutoff() const;
        void          SetAlphaCutoff(f32 alphaCutoff);
        AlphaMode     GetAlphaMode() const;
        void          SetAlphaMode(AlphaMode alphaMode);
        Vec2          GetUvScale() const;
        void          SetUvScale(const Vec2& uvScale);

        //void          OnChange() override;

        static void RegisterType(NativeTypeHandler<MaterialAsset>& type);

    private:
        Color         baseColor{Color::WHITE};
        TextureAsset* baseColorTexture{};
        TextureAsset* normalTexture{};
        f32           normalMultiplier{1.0};
        f32           metallic{0.0};
        TextureAsset* metallicTexture{};
        f32           roughness{1.0};
        TextureAsset* roughnessTexture{};
        TextureAsset* metallicRoughnessTexture{};
        TextureAsset* aoTexture{};
        TextureAsset* emissiveTexture{};
        Vec3          emissiveFactor{1.0, 1.0, 1.0};
        f32           alphaCutoff{0.5};
        AlphaMode     alphaMode{};
        Vec2          uvScale{1.0f, 1.0f};
    };
}
