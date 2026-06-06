#include "doctest.h"

#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/IO/Compression.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

using namespace Skore;

namespace Skore
{
	void SK_API ResourceInit();
	void SK_API ResourceShutdown();
	void SK_API RegisterResourceImportedAssetTypes();
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

		ResourceAssets::EnsureCooked(wrapper, nullptr);

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
		CHECK(!Resources::HasValue(cooked));

		ResourceAssets::EnsureCooked(wrapper, nullptr);

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
		ResourceAssets::EnsureCooked(wrapper, nullptr);
		{
			ResourceObject object = Resources::Read(Resources::FindByUUID(mainUUID));
			CHECK(object.GetInt(TestImportTarget::Value) == 50);
		}

		WriteTestWrapper(wrapper, 90, "hash-2", 1);
		ResourceAssets::EnsureCooked(wrapper, nullptr);
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

		ResourceAssets::EnsureCooked(wrapperA, nullptr);
		ResourceAssets::EnsureCooked(wrapperB, nullptr);

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

	// --- Multi-resource importer: exercises stable sub-resource UUIDs across recooks ---

	struct StableRoot
	{
		enum
		{
			Value,
			Children
		};
	};

	struct StableChild
	{
		enum
		{
			Value,
			Leaf
		};
	};

	struct StableLeaf
	{
		enum
		{
			Value
		};
	};

	struct StableImporter : ResourceAssetImporter
	{
		SK_CLASS(StableImporter, ResourceAssetImporter);

		Array<String> ImportedExtensions() override
		{
			return {".stablesrc"};
		}

		StringView OutputExtension() override
		{
			return ".stableasset";
		}

		u32 CookerVersion() override
		{
			return 1;
		}

		void Ingest(IngestContext& ctx) override
		{
			ctx.DeclareSubResource("root", TypeInfo<StableRoot>::ID());
		}

		void Cook(CookContext& ctx) override
		{
			SubResourceAllocator alloc = ctx.Allocator();
			i64                  firstByte = ctx.sourceBytes.Size() > 0 ? static_cast<i64>(ctx.sourceBytes.begin()[0]) : -1;

			RID            root = ctx.SubResource("root", TypeInfo<StableRoot>::ID());
			ResourceObject rootObject = Resources::Write(root);
			rootObject.SetInt(StableRoot::Value, firstByte);

			for (int i = 0; i < 3; i++)
			{
				RID            child = alloc.Create<StableChild>(String("child:") + ToString(i));
				ResourceObject childObject = Resources::Write(child);
				childObject.SetInt(StableChild::Value, firstByte + i);

				// child of a child: UUID derived from the owner resource, not the wrapper
				RID            leaf = Resources::Create<StableLeaf>(SubResourceUUID(Resources::GetUUID(child), "leaf"), ctx.scope);
				ResourceObject leafObject = Resources::Write(leaf);
				leafObject.SetInt(StableLeaf::Value, firstByte * 10 + i);
				leafObject.Commit(ctx.scope);

				childObject.SetSubObject(StableChild::Leaf, leaf);
				childObject.Commit(ctx.scope);

				rootObject.AddToSubObjectList(StableRoot::Children, child);
			}

			rootObject.Commit(ctx.scope);
		}
	};

	void SetupStableTypes()
	{
		RegisterResourceImportedAssetTypes();

		Resources::Type<StableRoot>()
			.Field<StableRoot::Value>(ResourceFieldType::Int)
			.Field<StableRoot::Children>(ResourceFieldType::SubObjectList)
			.Build();

		Resources::Type<StableChild>()
			.Field<StableChild::Value>(ResourceFieldType::Int)
			.Field<StableChild::Leaf>(ResourceFieldType::SubObject)
			.Build();

		Resources::Type<StableLeaf>()
			.Field<StableLeaf::Value>(ResourceFieldType::Int)
			.Build();

		Reflection::Type<StableImporter>();
		ReloadAssetHandlers();
	}

	void WriteStableWrapper(RID wrapper, u8 firstByte, StringView contentHash, u32 cookerVersion)
	{
		u8         src[] = {firstByte, 1, 2, 3};
		usize      bound = Compression::GetMaxCompressedBufferSize(sizeof(src), CompressionMode::ZSTD);
		ByteBuffer compressed;
		compressed.Resize(bound);
		usize compressedSize = Compression::Compress(compressed.begin(), bound, src, sizeof(src), CompressionMode::ZSTD);

		ResourceObject w = Resources::Write(wrapper);
		w.SetTypeID(ResourceImportedAsset::ImporterId, TypeInfo<StableImporter>::ID());
		w.SetUInt(ResourceImportedAsset::CookerVersion, cookerVersion);
		w.SetString(ResourceImportedAsset::ContentHash, contentHash);
		w.SetUInt(ResourceImportedAsset::OriginalSize, sizeof(src));
		w.SetBuffer(ResourceImportedAsset::OriginalData, ResourceAssets::CreateTempBuffer(compressed.begin(), compressedSize));
		w.Commit();
	}

	// Reimporting a different source file (content change) keeps every sub-resource UUID,
	// so external references survive. This is the bug the deterministic UUIDs fix.
	TEST_CASE("ImportedAsset::ReimportPreservesSubResourceUUIDs")
	{
		ResourceInit();
		SetupStableTypes();
		TmpFolders tmpFolders;

		RID wrapper = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());

		UUID rootUUID = SubResourceUUID(wrapper, "root");
		UUID childUUID[3];
		UUID leafUUID[3];
		for (int i = 0; i < 3; i++)
		{
			childUUID[i] = SubResourceUUID(wrapper, String("child:") + ToString(i));
			leafUUID[i] = SubResourceUUID(childUUID[i], "leaf");
		}

		auto verifyTree = [&](i64 expectedFirstByte)
		{
			RID root = Resources::FindByUUID(rootUUID);
			REQUIRE(root);
			ResourceObject rootObject = Resources::Read(root);
			CHECK(rootObject.GetInt(StableRoot::Value) == expectedFirstByte);

			Span<RID> children = rootObject.GetSubObjectList(StableRoot::Children);
			REQUIRE(children.Size() == 3);

			for (int i = 0; i < 3; i++)
			{
				// the sub-resource is reachable by its deterministic UUID (would be null if cook used random UUIDs)
				RID child = Resources::FindByUUID(childUUID[i]);
				REQUIRE(child);

				bool inList = false;
				for (RID c : children)
				{
					if (c == child) inList = true;
				}
				CHECK(inList);

				ResourceObject childObject = Resources::Read(child);
				CHECK(childObject.GetInt(StableChild::Value) == expectedFirstByte + i);

				RID leaf = Resources::FindByUUID(leafUUID[i]);
				REQUIRE(leaf);
				CHECK(childObject.GetSubObject(StableChild::Leaf) == leaf);
				CHECK(Resources::Read(leaf).GetInt(StableLeaf::Value) == expectedFirstByte * 10 + i);
			}
		};

		// First import (content A)
		WriteStableWrapper(wrapper, 10, "hash-A", 1);
		ResourceAssets::EnsureCooked(wrapper, nullptr);
		verifyTree(10);

		// Reimport with a DIFFERENT source file (new content => fresh cook, no cached blob reuse)
		WriteStableWrapper(wrapper, 50, "hash-B", 1);
		ResourceAssets::EnsureCooked(wrapper, nullptr);
		verifyTree(50);

		ResourceShutdown();
	}

	// Forcing an actual recook of identical content (cooker version bump => cache miss)
	// must produce byte-for-byte the same sub-resource UUID set.
	TEST_CASE("ImportedAsset::RecookKeepsSubResourceUUIDs")
	{
		ResourceInit();
		SetupStableTypes();
		TmpFolders tmpFolders;

		RID wrapper = Resources::Create<ResourceImportedAsset>(UUID::RandomUUID());

		UUID rootUUID = SubResourceUUID(wrapper, "root");
		UUID child0UUID = SubResourceUUID(wrapper, "child:0");
		UUID leaf0UUID = SubResourceUUID(child0UUID, "leaf");

		WriteStableWrapper(wrapper, 7, "hash", 1);
		ResourceAssets::EnsureCooked(wrapper, nullptr);

		RID rootA = Resources::FindByUUID(rootUUID);
		REQUIRE(rootA);
		REQUIRE(Resources::FindByUUID(child0UUID));
		REQUIRE(Resources::FindByUUID(leaf0UUID));

		Array<UUID> before;
		{
			ResourceObject rootObject = Resources::Read(rootA);
			for (RID c : rootObject.GetSubObjectList(StableRoot::Children))
			{
				before.EmplaceBack(Resources::GetUUID(c));
			}
		}
		REQUIRE(before.Size() == 3);

		// Bump the stored cooker version => different cache key => a real recook (not a blob reload)
		WriteStableWrapper(wrapper, 7, "hash", 2);
		ResourceAssets::EnsureCooked(wrapper, nullptr);

		REQUIRE(Resources::FindByUUID(rootUUID));
		REQUIRE(Resources::FindByUUID(child0UUID));
		REQUIRE(Resources::FindByUUID(leaf0UUID));

		Array<UUID> after;
		{
			ResourceObject rootObject = Resources::Read(Resources::FindByUUID(rootUUID));
			for (RID c : rootObject.GetSubObjectList(StableRoot::Children))
			{
				after.EmplaceBack(Resources::GetUUID(c));
			}
		}

		REQUIRE(after.Size() == before.Size());
		for (usize i = 0; i < before.Size(); i++)
		{
			CHECK(before[i] == after[i]);
		}

		ResourceShutdown();
	}
}
