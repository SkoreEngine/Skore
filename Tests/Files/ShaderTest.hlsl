// Constant buffer for transformations
cbuffer CameraBuffer
{
	matrix View;         // View matrix
	matrix Projection;   // Projection matrix
};

struct PushConstants
{
	matrix World;        // World matrix
};

[[vk::push_constant]] PushConstants pushConstants;

// Texture and sampler
Texture2D diffuseTexture : register(space1);
SamplerState textureSampler : register(space1);

// Input vertex structure
struct VertexInput
{
	float3 Position : POSITION;
	float2 TexCoord : TEXCOORD0;
	float3 Normal : NORMAL;
};

// Output from vertex shader to pixel shader
struct PixelInput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
	float3 Normal : NORMAL;
};

// Vertex Shader
PixelInput MainVS(VertexInput input)
{
	PixelInput output;

	// Transform the position from object space to clip space
	float4 worldPosition = mul(float4(input.Position, 1.0f), pushConstants.World);
	float4 viewPosition = mul(worldPosition, View);
	output.Position = mul(viewPosition, Projection);

	// Pass texture coordinates through
	output.TexCoord = input.TexCoord;

	// Transform normal to world space
	output.Normal = mul(input.Normal, (float3x3)pushConstants.World);
	output.Normal = normalize(output.Normal);

	return output;
}

// Pixel Shader
float4 MainPS(PixelInput input) : SV_TARGET
{
	// Sample the texture
	float4 textureColor = diffuseTexture.Sample(textureSampler, input.TexCoord);

	// Basic lighting (ambient + diffuse)
	float3 lightDir = normalize(float3(1.0f, 1.0f, 1.0f));
	float3 ambientColor = float3(0.2f, 0.2f, 0.2f);
	float diffuseFactor = max(dot(input.Normal, lightDir), 0.0f);
	float3 diffuseColor = float3(1.0f, 1.0f, 1.0f) * diffuseFactor;

	// Combine lighting with texture color
	float3 finalColor = textureColor.rgb * (ambientColor + diffuseColor);

	return float4(finalColor, textureColor.a);
}