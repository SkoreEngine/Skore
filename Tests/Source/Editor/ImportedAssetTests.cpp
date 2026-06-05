#include "doctest.h"

#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

using namespace Skore;

namespace Skore
{
	void SK_API ResourceInit();
	void SK_API ResourceShutdown();
}

namespace
{
	struct TestImportTarget
	{
		enum
		{
			Value,
			Data
		};
	};

	struct TestImporter : ResourceAssetImporter
	{
		SK_CLASS(TestImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".testsrc"};
		}

		StringView OutputExtension() override
		{
			return ".testasset";
		}

		u32 CookerVersion() override
		{
			return 1;
		}

		void Ingest(IngestContext& ctx) override
		{
			ctx.DeclareSubResource("main", TypeInfo<TestImportTarget>::ID());
		}

		void Cook(CookContext& ctx) override
		{
			RID            main = ctx.SubResource("main", TypeInfo<TestImportTarget>::ID());
			ResourceObject object = Resources::Write(main);
			i64            firstByte = ctx.sourceBytes.Size() > 0 ? static_cast<i64>(ctx.sourceBytes.begin()[0]) : -1;
			object.SetInt(TestImportTarget::Value, firstByte);
			object.SetBuffer(TestImportTarget::Data, ResourceAssets::CreateTempBuffer(ctx.sourceBytes.begin(), ctx.sourceBytes.Size()));
			object.Commit(ctx.scope);
		}
	};

	void SetupImportedTypes()
	{
		RegisterResourceImportedAssetTypes();

		Resources::Type<TestImportTarget>()
			.Field<TestImportTarget::Value>(ResourceFieldType::Int)
			.Field<TestImportTarget::Data>(ResourceFieldType::Buffer)
			.Build();
	}

	struct TmpFolders
	{
		String dir;

		TmpFolders() : dir(Path::Join(FileSystem::CurrentDir(), "ImportedAssetTestTmp"))
		{
			ResourceAssets::InitTestFolders(dir);
		}

		~TmpFolders()
		{
			ResourceAssets::ClearCookCacheState();
			FileSystem::Remove(dir);
		}
	};

	TEST_CASE("ImportedAsset::SubResourceUUIDStable")
	{
		ResourceInit();
		SetupImportedTypes();

		RID wrapperA = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());
		RID wrapperB = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());

		UUID a1 = SubResourceUUID(wrapperA, "mesh:Body");
		UUID a1again = SubResourceUUID(wrapperA, "mesh:Body");
		UUID a2 = SubResourceUUID(wrapperA, "mesh:Wheel");
		UUID b1 = SubResourceUUID(wrapperB, "mesh:Body");

		CHECK(static_cast<bool>(a1));
		CHECK(a1 == a1again);
		CHECK(a1 != a2);
		CHECK(a1 != b1);

		ResourceShutdown();
	}

	TEST_CASE("ImportedAsset::DeclareSubResourceDedup")
	{
		ResourceInit();
		SetupImportedTypes();

		RID wrapper = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());

		IngestContext ctx;
		ctx.importedAsset = wrapper;

		UUID first = ctx.DeclareSubResource("mesh:Body", TypeInfo<TestImportTarget>::ID());
		UUID dup = ctx.DeclareSubResource("mesh:Body", TypeInfo<TestImportTarget>::ID());

		CHECK(first == dup);
		CHECK(first == SubResourceUUID(wrapper, "mesh:Body"));
		CHECK(ctx.subResources.Size() == 1);

		ctx.DeclareSubResource("mat:Paint", TypeInfo<TestImportTarget>::ID());
		CHECK(ctx.subResources.Size() == 2);

		ResourceShutdown();
	}

	TEST_CASE("ImportedAsset::AddDependencyDedup")
	{
		ResourceInit();
		SetupImportedTypes();

		RID wrapper = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());

		IngestContext ctx;
		ctx.importedAsset = wrapper;

		u8 bytes[] = {10, 20, 30, 40};

		CHECK(!ctx.HasDependency("tex/albedo.png"));
		ctx.AddDependency("tex/albedo.png", Span<u8>(bytes, 4));
		CHECK(ctx.HasDependency("tex/albedo.png"));
		CHECK(ctx.dependencies.Size() == 1);
		CHECK(ctx.dependencies[0].bytes.Size() == 4);
		CHECK(ctx.dependencies[0].bytes[0] == 10);
		CHECK(ctx.dependencies[0].bytes[3] == 40);

		ctx.AddDependency("tex/albedo.png", Span<u8>(bytes, 4));
		CHECK(ctx.dependencies.Size() == 1);

		ctx.AddDependency("tex/normal.png", Span<u8>(bytes, 4));
		CHECK(ctx.dependencies.Size() == 2);

		ResourceShutdown();
	}

	TEST_CASE("ImportedAsset::CookReusesReservedRID")
	{
		ResourceInit();
		SetupImportedTypes();

		RID wrapper = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());

		UUID meshUUID = SubResourceUUID(wrapper, "mesh:Body");
		RID  reserved = Resources::FindOrReserveByUUID(meshUUID);
		CHECK(reserved);

		CookContext ctx;
		ctx.importedAsset = wrapper;
		ctx.scope = nullptr;

		RID cooked = ctx.SubResource("mesh:Body", TypeInfo<TestImportTarget>::ID());

		CHECK(cooked == reserved);
		CHECK(Resources::GetUUID(cooked) == meshUUID);
		CHECK(ctx.produced.Size() == 1);

		RID cookedAgain = ctx.SubResource("mesh:Body", TypeInfo<TestImportTarget>::ID());
		CHECK(cookedAgain == cooked);
		CHECK(ctx.produced.Size() == 1);

		ResourceShutdown();
	}

	TEST_CASE("ImportedAsset::DependencyRoundtrip")
	{
		ResourceInit();
		SetupImportedTypes();
		TmpFolders tmpFolders;

		RID wrapper = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());

		IngestContext ctx;
		ctx.importedAsset = wrapper;
		ctx.scope = nullptr;

		u8 albedo[] = {1, 2, 3, 4, 5, 6, 7, 8};
		u8 normal[] = {200, 150, 100, 50};

		ctx.AddDependency("tex/albedo.png", Span<u8>(albedo, 8));
		ctx.AddDependency("tex/normal.png", Span<u8>(normal, 4));
		ctx.DeclareSubResource("root", TypeInfo<TestImportTarget>::ID());

		ApplyIngest(ctx);

		ResourceObject wrapperObj = Resources::Read(wrapper);
		CHECK(wrapperObj.GetSubObjectList(ResourceImportedAsset::Dependencies).Size() == 2);
		CHECK(wrapperObj.GetSubObjectList(ResourceImportedAsset::SubResources).Size() == 1);
		CHECK(!wrapperObj.GetString(ResourceImportedAsset::ContentHash).Empty());

		CookContext cookCtx;
		cookCtx.importedAsset = wrapper;

		ByteBuffer gotAlbedo = cookCtx.Dependency("tex/albedo.png");
		REQUIRE(gotAlbedo.Size() == 8);
		CHECK(gotAlbedo[0] == 1);
		CHECK(gotAlbedo[7] == 8);

		ByteBuffer gotNormal = cookCtx.Dependency("tex/normal.png");
		REQUIRE(gotNormal.Size() == 4);
		CHECK(gotNormal[0] == 200);
		CHECK(gotNormal[3] == 50);

		ByteBuffer missing = cookCtx.Dependency("tex/none.png");
		CHECK(missing.Size() == 0);

		ResourceShutdown();
	}

	TEST_CASE("ImportedAsset::LibraryCookAndReload")
	{
		ResourceInit();
		SetupImportedTypes();
		Reflection::Type<TestImporter>();
		ReloadAssetHandlers();
		TmpFolders tmpFolders;

		RID wrapper = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());
		u8  src[] = {77, 11, 22, 33, 44};
		{
			usize      bound = Compression::GetMaxCompressedBufferSize(sizeof(src), CompressionMode::ZSTD);
			ByteBuffer compressed;
			compressed.Resize(bound);
			usize compressedSize = Compression::Compress(compressed.begin(), bound, src, sizeof(src), CompressionMode::ZSTD);

			ResourceObject w = Resources::Write(wrapper);
			w.SetTypeID(ResourceImportedAsset::ImporterId, TypeInfo<TestImporter>::ID());
			w.SetUInt(ResourceImportedAsset::CookerVersion, 1);
			w.SetString(ResourceImportedAsset::ContentHash, "hash-A");
			w.SetUInt(ResourceImportedAsset::OriginalSize, sizeof(src));
			w.SetBuffer(ResourceImportedAsset::OriginalData, ResourceAssets::CreateTempBuffer(compressed.begin(), compressedSize));
			w.Commit();
		}

		UUID mainUUID = SubResourceUUID(wrapper, "main");

		ResourceAssets::EnsureCooked(wrapper);

		RID cooked = Resources::FindByUUID(mainUUID);
		REQUIRE(cooked);
		{
			ResourceObject object = Resources::Read(cooked);
			CHECK(object.GetInt(TestImportTarget::Value) == 77);
			ResourceBuffer buffer = object.GetBuffer(TestImportTarget::Data);
			REQUIRE(static_cast<bool>(buffer));
			CHECK(buffer.GetSize() == sizeof(src));
		}

		Resources::Destroy(cooked);
		ResourceAssets::ClearCookCacheState();
		CHECK(!Resources::HasValue(cooked));

		ResourceAssets::EnsureCooked(wrapper);

		RID reloaded = Resources::FindByUUID(mainUUID);
		REQUIRE(reloaded);
		{
			ResourceObject object = Resources::Read(reloaded);
			CHECK(object.GetInt(TestImportTarget::Value) == 77);
			ResourceBuffer buffer = object.GetBuffer(TestImportTarget::Data);
			REQUIRE(static_cast<bool>(buffer));
			REQUIRE(buffer.GetSize() == sizeof(src));

			u8 readback[5] = {};
			buffer.CopyData(readback, sizeof(src));
			CHECK(readback[0] == 77);
			CHECK(readback[4] == 44);
		}

		ResourceShutdown();
	}

	void WriteTestWrapper(RID wrapper, u8 firstByte, StringView contentHash, u32 cookerVersion)
	{
		u8        src[] = {firstByte, 7, 8};
		usize     bound = Compression::GetMaxCompressedBufferSize(sizeof(src), CompressionMode::ZSTD);
		ByteBuffer compressed;
		compressed.Resize(bound);
		usize compressedSize = Compression::Compress(compressed.begin(), bound, src, sizeof(src), CompressionMode::ZSTD);

		ResourceObject w = Resources::Write(wrapper);
		w.SetTypeID(ResourceImportedAsset::ImporterId, TypeInfo<TestImporter>::ID());
		w.SetUInt(ResourceImportedAsset::CookerVersion, cookerVersion);
		w.SetString(ResourceImportedAsset::ContentHash, contentHash);
		w.SetUInt(ResourceImportedAsset::OriginalSize, sizeof(src));
		w.SetBuffer(ResourceImportedAsset::OriginalData, ResourceAssets::CreateTempBuffer(compressed.begin(), compressedSize));
		w.Commit();
	}

	TEST_CASE("ImportedAsset::CookInvalidatesOnContentChange")
	{
		ResourceInit();
		SetupImportedTypes();
		Reflection::Type<TestImporter>();
		ReloadAssetHandlers();
		TmpFolders tmpFolders;

		RID  wrapper = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());
		UUID mainUUID = SubResourceUUID(wrapper, "main");

		WriteTestWrapper(wrapper, 50, "hash-1", 1);
		ResourceAssets::EnsureCooked(wrapper);
		{
			ResourceObject object = Resources::Read(Resources::FindByUUID(mainUUID));
			CHECK(object.GetInt(TestImportTarget::Value) == 50);
		}

		WriteTestWrapper(wrapper, 90, "hash-2", 1);
		ResourceAssets::ClearCookCacheState();
		ResourceAssets::EnsureCooked(wrapper);
		{
			ResourceObject object = Resources::Read(Resources::FindByUUID(mainUUID));
			CHECK(object.GetInt(TestImportTarget::Value) == 90);
		}

		ResourceShutdown();
	}

	TEST_CASE("ImportedAsset::IdenticalContentDistinctWrappers")
	{
		ResourceInit();
		SetupImportedTypes();
		Reflection::Type<TestImporter>();
		ReloadAssetHandlers();
		TmpFolders tmpFolders;

		RID wrapperA = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());
		RID wrapperB = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());

		WriteTestWrapper(wrapperA, 100, "same-hash", 1);
		WriteTestWrapper(wrapperB, 100, "same-hash", 1);

		ResourceAssets::EnsureCooked(wrapperA);
		ResourceAssets::EnsureCooked(wrapperB);

		RID mainA = Resources::FindByUUID(SubResourceUUID(wrapperA, "main"));
		RID mainB = Resources::FindByUUID(SubResourceUUID(wrapperB, "main"));

		REQUIRE(mainA);
		REQUIRE(mainB);
		CHECK(mainA != mainB);

		ResourceObject objectA = Resources::Read(mainA);
		ResourceObject objectB = Resources::Read(mainB);
		REQUIRE(static_cast<bool>(objectA));
		REQUIRE(static_cast<bool>(objectB));
		CHECK(objectA.GetInt(TestImportTarget::Value) == 100);
		CHECK(objectB.GetInt(TestImportTarget::Value) == 100);

		ResourceShutdown();
	}
}
