#include <httplib.h>

// httplib pulls in <windows.h>, whose CreateDirectory macro otherwise rewrites the
// ResourceAssets::CreateDirectory call below to CreateDirectoryA and breaks linking.
#ifdef CreateDirectory
#undef CreateDirectory
#endif

#include <functional>

#include "Skore/Editor.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Traits.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/Resource/ResourceType.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	bool ServerRunOnMainThreadSync(std::function<void()> fn, u32 timeoutMs = 3000);

	namespace
	{
		struct ResolveResult
		{
			RID  wrapper;
			RID  directoryNode;
			bool isDirectory = false;
		};

		void WriteJson(httplib::Response& res, int status, StringView json)
		{
			res.status = status;
			res.set_content(json.CStr(), json.Size(), "application/json");
		}

		String ErrorJson(StringView message)
		{
			JsonArchiveWriter writer;
			writer.WriteString("error", message);
			return writer.EmitAsString();
		}

		bool LooksLikePath(StringView ref)
		{
			for (usize i = 0; i < ref.Size(); ++i)
			{
				char c = ref.Data()[i];
				if (c == '/' || c == ':') return true;
			}
			return false;
		}

		RID PackageRootDir(RID package)
		{
			if (!package) return {};
			if (ResourceObject packageObject = Resources::Read(package))
			{
				return packageObject.GetSubObject(ResourceAssetPackage::Root);
			}
			return {};
		}

		RID DefaultRootDir()
		{
			return PackageRootDir(Editor::GetProject());
		}

		RID FindPackageByName(StringView name)
		{
			auto matches = [&](RID package)
			{
				ResourceObject packageObject = Resources::Read(package);
				return packageObject && packageObject.GetString(ResourceAssetPackage::Name) == name;
			};

			if (RID project = Editor::GetProject(); matches(project)) return project;
			for (RID package : Editor::GetPackages())
			{
				if (matches(package)) return package;
			}
			return {};
		}

		RID DirectoryAssetOf(RID directoryNode)
		{
			if (!directoryNode) return {};
			if (ResourceObject directoryObject = Resources::Read(directoryNode))
			{
				return directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
			}
			return {};
		}

		ResolveResult ResolveByPath(StringView path)
		{
			ResolveResult result;

			StringView packageName;
			StringView relative = path;
			for (usize i = 0; i + 1 < path.Size(); ++i)
			{
				if (path.Data()[i] == ':' && path.Data()[i + 1] == '/')
				{
					packageName = path.Substr(0, i);
					relative = path.Substr(i + 2);
					break;
				}
			}

			RID rootDir = packageName.Empty() ? DefaultRootDir() : PackageRootDir(FindPackageByName(packageName));
			if (!rootDir) return result;

			Array<String> segments;
			Split(relative, StringView{"/"}, [&](StringView segment)
			{
				if (!segment.Empty()) segments.EmplaceBack(segment);
			});

			if (segments.Empty())
			{
				result.directoryNode = rootDir;
				result.wrapper = DirectoryAssetOf(rootDir);
				result.isDirectory = true;
				return result;
			}

			RID dir = rootDir;
			for (usize i = 0; i < segments.Size(); ++i)
			{
				const bool last = i + 1 == segments.Size();

				RID foundDirNode;
				RID foundDirAsset;
				RID foundAsset;

				ResourceObject dirObject = Resources::Read(dir);

				dirObject.IterateSubObjectList(ResourceAssetDirectory::Directories, [&](RID childDir)
				{
					RID childAsset = DirectoryAssetOf(childDir);
					if (segments[i] == Resources::Read(childAsset).GetString(ResourceAsset::Name))
					{
						foundDirNode = childDir;
						foundDirAsset = childAsset;
					}
				});

				if (foundDirNode)
				{
					if (last)
					{
						result.directoryNode = foundDirNode;
						result.wrapper = foundDirAsset;
						result.isDirectory = true;
						return result;
					}
					dir = foundDirNode;
					continue;
				}

				if (last)
				{
					dirObject.IterateSubObjectList(ResourceAssetDirectory::Assets, [&](RID asset)
					{
						ResourceObject assetObject = Resources::Read(asset);
						String         fullName = String(assetObject.GetString(ResourceAsset::Name)) + assetObject.GetString(ResourceAsset::Extension);
						if (fullName == segments[i]) foundAsset = asset;
					});

					if (foundAsset)
					{
						result.wrapper = foundAsset;
						result.isDirectory = false;
						return result;
					}
				}

				return result;
			}

			return result;
		}

		ResolveResult ResolveByUuid(StringView ref)
		{
			ResolveResult result;

			RID rid = Resources::FindByUUID(UUID::FromString(ref));
			if (!rid) return result;

			ResourceType* type = Resources::GetType(rid);
			if (!type) return result;

			TypeID typeId = type->GetID();
			if (typeId == TypeInfo<ResourceAssetDirectory>::ID())
			{
				result.directoryNode = rid;
				result.wrapper = DirectoryAssetOf(rid);
				result.isDirectory = true;
				return result;
			}

			RID wrapper = typeId == TypeInfo<ResourceAsset>::ID() ? rid : ResourceAssets::GetResourceAssetFromResourceRecursive(rid);
			if (!wrapper) return result;

			result.wrapper = wrapper;
			ResourceObject wrapperObject = Resources::Read(wrapper);
			result.isDirectory = wrapperObject.GetBool(ResourceAsset::Directory);
			if (result.isDirectory)
			{
				result.directoryNode = Resources::GetParent(wrapper);
			}
			return result;
		}

		ResolveResult Resolve(StringView ref)
		{
			if (ref.Empty())
			{
				ResolveResult result;
				result.directoryNode = DefaultRootDir();
				result.wrapper = DirectoryAssetOf(result.directoryNode);
				result.isDirectory = true;
				return result;
			}
			return LooksLikePath(ref) ? ResolveByPath(ref) : ResolveByUuid(ref);
		}

		RID ResolveDataObject(StringView ref)
		{
			ResolveResult result = Resolve(ref);
			if (!result.wrapper) return {};
			return ResourceAssets::GetAssetPayload(result.wrapper);
		}

		TypeID ResolveAssetType(StringView name)
		{
			if (name.Empty()) return {};

			if (ResourceType* type = Resources::FindTypeByName(name); type && ResourceAssets::GetAssetHandler(type->GetID()))
			{
				return type->GetID();
			}

			for (ResourceType* type : Resources::GetTypes())
			{
				if (!ResourceAssets::GetAssetHandler(type->GetID())) continue;
				if (type->GetSimpleName() == name || type->GetName() == name) return type->GetID();
			}
			return {};
		}

		void EmitAssetEntry(JsonArchiveWriter& writer, RID wrapper)
		{
			if (!wrapper) return;
			ResourceObject wrapperObject = Resources::Read(wrapper);
			if (!wrapperObject) return;

			bool isDirectory = wrapperObject.GetBool(ResourceAsset::Directory);

			writer.BeginMap();
			writer.WriteString("name", wrapperObject.GetString(ResourceAsset::Name));
			writer.WriteString("extension", wrapperObject.GetString(ResourceAsset::Extension));
			writer.WriteBool("isDirectory", isDirectory);
			writer.WriteString("pathId", wrapperObject.GetString(ResourceAsset::PathId));

			if (!isDirectory)
			{
				if (RID data = wrapperObject.GetSubObject(ResourceAsset::Object))
				{
					writer.WriteString("uuid", Resources::GetUUID(data).ToString());
					if (ResourceType* type = Resources::GetType(data))
					{
						writer.WriteString("type", type->GetSimpleName());
					}
				}
			}
			writer.EndMap();
		}

		void EmitFieldValue(JsonArchiveWriter& writer, StringView key, const ResourceObject& object, ResourceField* field)
		{
			u32 index = field->GetIndex();
			switch (field->GetType())
			{
				case ResourceFieldType::Bool: writer.WriteBool(key, object.GetBool(index)); break;
				case ResourceFieldType::Int: writer.WriteInt(key, object.GetInt(index)); break;
				case ResourceFieldType::UInt: writer.WriteUInt(key, object.GetUInt(index)); break;
				case ResourceFieldType::Float: writer.WriteFloat(key, object.GetFloat(index)); break;
				case ResourceFieldType::String: writer.WriteString(key, object.GetString(index)); break;
				case ResourceFieldType::Enum: writer.WriteInt(key, object.GetEnum(index)); break;
				case ResourceFieldType::Reference:
				{
					if (RID reference = object.GetReference(index))
					{
						writer.WriteString(key, Resources::GetUUID(reference).ToString());
					}
					break;
				}
				default: break;
			}
		}

		String BuildAssetDetail(RID wrapper)
		{
			JsonArchiveWriter writer;

			ResourceObject wrapperObject = Resources::Read(wrapper);
			if (!wrapperObject)
			{
				writer.WriteString("error", "asset not found");
				return writer.EmitAsString();
			}

			bool isDirectory = wrapperObject.GetBool(ResourceAsset::Directory);
			writer.WriteString("name", wrapperObject.GetString(ResourceAsset::Name));
			writer.WriteString("extension", wrapperObject.GetString(ResourceAsset::Extension));
			writer.WriteBool("isDirectory", isDirectory);
			writer.WriteString("pathId", wrapperObject.GetString(ResourceAsset::PathId));

			if (!isDirectory)
			{
				if (RID data = wrapperObject.GetSubObject(ResourceAsset::Object))
				{
					writer.WriteString("uuid", Resources::GetUUID(data).ToString());
					if (ResourceType* type = Resources::GetType(data))
					{
						writer.WriteString("type", type->GetSimpleName());

						ResourceObject dataObject = Resources::Read(data);
						writer.BeginMap("fields");
						for (ResourceField* field : type->GetFields())
						{
							EmitFieldValue(writer, field->GetName(), dataObject, field);
						}
						writer.EndMap();
					}
				}
			}

			return writer.EmitAsString();
		}
	}

	void RegisterAssetApiRoutes(httplib::Server& svr)
	{
		svr.Get("/api/types", [](const httplib::Request&, httplib::Response& res)
		{
			String out;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				JsonArchiveWriter writer;
				writer.BeginSeq("types");
				for (ResourceType* type : Resources::GetTypes())
				{
					ResourceAssetHandler* handler = ResourceAssets::GetAssetHandler(type->GetID());
					if (!handler) continue;

					writer.BeginMap();
					writer.WriteString("type", type->GetSimpleName());
					writer.WriteString("fullName", type->GetName());
					writer.WriteString("extension", handler->Extension());
					writer.WriteString("desc", handler->GetDesc());
					writer.EndMap();
				}
				writer.EndSeq();
				out = writer.EmitAsString();
			});
			WriteJson(res, ok ? 200 : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Get("/api/assets", [](const httplib::Request& req, httplib::Response& res)
		{
			String dirRef = req.has_param("dir") ? String(req.get_param_value("dir").c_str()) : String{};
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				ResolveResult dir = Resolve(dirRef);
				if (!dir.directoryNode)
				{
					status = 404;
					out = ErrorJson("directory not found");
					return;
				}

				JsonArchiveWriter writer;
				writer.BeginSeq("entries");
				ResourceObject directoryObject = Resources::Read(dir.directoryNode);
				directoryObject.IterateSubObjectList(ResourceAssetDirectory::Directories, [&](RID childDir)
				{
					EmitAssetEntry(writer, DirectoryAssetOf(childDir));
				});
				directoryObject.IterateSubObjectList(ResourceAssetDirectory::Assets, [&](RID asset)
				{
					EmitAssetEntry(writer, asset);
				});
				writer.EndSeq();
				out = writer.EmitAsString();
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Get("/api/asset", [](const httplib::Request& req, httplib::Response& res)
		{
			String ref = req.has_param("ref") ? String(req.get_param_value("ref").c_str()) : String{};
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				ResolveResult result = Resolve(ref);
				if (!result.wrapper)
				{
					status = 404;
					out = ErrorJson("asset not found");
					return;
				}
				out = BuildAssetDetail(result.wrapper);
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Post("/api/assets/create", [](const httplib::Request& req, httplib::Response& res)
		{
			String body(req.body.c_str(), req.body.size());
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				JsonArchiveReader reader(body);
				String            parentRef = reader.ReadString("parent");
				String            typeName = reader.ReadString("type");
				String            name = reader.ReadString("name");

				ResolveResult parent = Resolve(parentRef);
				if (!parent.directoryNode)
				{
					status = 404;
					out = ErrorJson("parent directory not found");
					return;
				}

				TypeID typeId = ResolveAssetType(typeName);
				if (!typeId)
				{
					status = 400;
					out = ErrorJson(String("unknown asset type: ") + typeName);
					return;
				}

				UndoRedoScope* scope = Editor::CreateUndoRedoScope("MCP: Create Asset");
				RID            data = ResourceAssets::CreateAsset(parent.directoryNode, typeId, name, scope);
				if (!data)
				{
					status = 500;
					out = ErrorJson("failed to create asset");
					return;
				}
				out = BuildAssetDetail(Resources::GetParent(data));
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Post("/api/assets/createDirectory", [](const httplib::Request& req, httplib::Response& res)
		{
			String body(req.body.c_str(), req.body.size());
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				JsonArchiveReader reader(body);
				String            parentRef = reader.ReadString("parent");
				String            name = reader.ReadString("name");

				ResolveResult parent = Resolve(parentRef);
				if (!parent.directoryNode)
				{
					status = 404;
					out = ErrorJson("parent directory not found");
					return;
				}

				UndoRedoScope* scope = Editor::CreateUndoRedoScope("MCP: Create Directory");
				RID            dirAsset = ResourceAssets::CreateDirectory(parent.directoryNode, name.Empty() ? StringView("New Folder") : StringView(name), scope);
				out = BuildAssetDetail(dirAsset);
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Post("/api/assets/rename", [](const httplib::Request& req, httplib::Response& res)
		{
			String body(req.body.c_str(), req.body.size());
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				JsonArchiveReader reader(body);
				String            ref = reader.ReadString("ref");
				String            name = reader.ReadString("name");

				ResolveResult result = Resolve(ref);
				if (!result.wrapper)
				{
					status = 404;
					out = ErrorJson("asset not found");
					return;
				}
				if (name.Empty())
				{
					status = 400;
					out = ErrorJson("name is required");
					return;
				}

				UndoRedoScope* scope = Editor::CreateUndoRedoScope("MCP: Rename Asset");
				ResourceObject write = Resources::Write(result.wrapper);
				write.SetString(ResourceAsset::Name, name);
				write.Commit(scope);

				out = BuildAssetDetail(result.wrapper);
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Post("/api/assets/delete", [](const httplib::Request& req, httplib::Response& res)
		{
			String body(req.body.c_str(), req.body.size());
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				JsonArchiveReader reader(body);
				String            ref = reader.ReadString("ref");

				ResolveResult result = Resolve(ref);
				if (!result.wrapper)
				{
					status = 404;
					out = ErrorJson("asset not found");
					return;
				}

				UndoRedoScope* scope = Editor::CreateUndoRedoScope("MCP: Delete Asset");
				if (result.isDirectory)
				{
					Resources::Destroy(result.directoryNode, scope);
				}
				else
				{
					Resources::Destroy(result.wrapper, scope);
				}

				JsonArchiveWriter writer;
				writer.WriteBool("ok", true);
				out = writer.EmitAsString();
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Post("/api/assets/move", [](const httplib::Request& req, httplib::Response& res)
		{
			String body(req.body.c_str(), req.body.size());
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				JsonArchiveReader reader(body);
				String            ref = reader.ReadString("ref");
				String            targetRef = reader.ReadString("targetDir");

				ResolveResult source = Resolve(ref);
				ResolveResult target = Resolve(targetRef);
				if (!source.wrapper)
				{
					status = 404;
					out = ErrorJson("asset not found");
					return;
				}
				if (!target.directoryNode || !target.wrapper)
				{
					status = 404;
					out = ErrorJson("target directory not found");
					return;
				}

				UndoRedoScope* scope = Editor::CreateUndoRedoScope("MCP: Move Asset");
				ResourceAssets::MoveAsset(target.wrapper, source.wrapper, scope);

				out = BuildAssetDetail(source.wrapper);
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Post("/api/assets/duplicate", [](const httplib::Request& req, httplib::Response& res)
		{
			String body(req.body.c_str(), req.body.size());
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				JsonArchiveReader reader(body);
				String            ref = reader.ReadString("ref");
				String            parentRef = reader.ReadString("parent");
				String            name = reader.ReadString("name");

				ResolveResult source = Resolve(ref);
				if (!source.wrapper || source.isDirectory)
				{
					status = 404;
					out = ErrorJson("asset not found");
					return;
				}

				RID parentDir = parentRef.Empty() ? Resources::GetParent(source.wrapper) : Resolve(parentRef).directoryNode;
				if (!parentDir)
				{
					status = 404;
					out = ErrorJson("parent directory not found");
					return;
				}

				RID sourceData = ResourceAssets::GetAssetPayload(source.wrapper);

				UndoRedoScope* scope = Editor::CreateUndoRedoScope("MCP: Duplicate Asset");
				RID            data = ResourceAssets::DuplicateAsset(parentDir, sourceData, name, scope);
				if (!data)
				{
					status = 500;
					out = ErrorJson("failed to duplicate asset");
					return;
				}
				out = BuildAssetDetail(Resources::GetParent(data));
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Post("/api/asset/update", [](const httplib::Request& req, httplib::Response& res)
		{
			String body(req.body.c_str(), req.body.size());
			String out;
			int    status = 200;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				JsonArchiveReader reader(body);
				String            ref = reader.ReadString("ref");

				ResolveResult result = Resolve(ref);
				if (!result.wrapper || result.isDirectory)
				{
					status = 404;
					out = ErrorJson("asset not found");
					return;
				}

				RID           data = ResourceAssets::GetAssetPayload(result.wrapper);
				ResourceType* type = data ? Resources::GetType(data) : nullptr;
				if (!type)
				{
					status = 404;
					out = ErrorJson("asset has no data object");
					return;
				}

				Array<String> updated;
				Array<String> skipped;

				UndoRedoScope* scope = Editor::CreateUndoRedoScope("MCP: Update Asset");
				ResourceObject object = Resources::Write(data);

				if (reader.BeginMap("fields"))
				{
					while (reader.NextMapEntry())
					{
						String         key = reader.GetCurrentKey();
						ResourceField* field = type->FindFieldByName(key);
						if (!field)
						{
							skipped.EmplaceBack(key);
							continue;
						}

						u32 index = field->GetIndex();
						switch (field->GetType())
						{
							case ResourceFieldType::Bool: object.SetBool(index, reader.GetBool()); updated.EmplaceBack(key); break;
							case ResourceFieldType::Int: object.SetInt(index, reader.GetInt()); updated.EmplaceBack(key); break;
							case ResourceFieldType::UInt: object.SetUInt(index, reader.GetUInt()); updated.EmplaceBack(key); break;
							case ResourceFieldType::Float: object.SetFloat(index, reader.GetFloat()); updated.EmplaceBack(key); break;
							case ResourceFieldType::String: object.SetString(index, reader.GetString()); updated.EmplaceBack(key); break;
							case ResourceFieldType::Enum: object.SetEnum(index, reader.GetInt()); updated.EmplaceBack(key); break;
							case ResourceFieldType::Reference: object.SetReference(index, ResolveDataObject(reader.GetString())); updated.EmplaceBack(key); break;
							default: skipped.EmplaceBack(key); break;
						}
					}
					reader.EndMap();
				}

				object.Commit(scope);

				JsonArchiveWriter writer;
				writer.WriteBool("ok", true);
				writer.BeginSeq("updated");
				for (const String& field : updated) writer.AddString(field);
				writer.EndSeq();
				writer.BeginSeq("skipped");
				for (const String& field : skipped) writer.AddString(field);
				writer.EndSeq();
				out = writer.EmitAsString();
			});
			WriteJson(res, ok ? status : 503, ok ? out : ErrorJson("editor busy"));
		});

		svr.Post("/api/save", [](const httplib::Request&, httplib::Response& res)
		{
			String out;
			bool   ok = ServerRunOnMainThreadSync([&]
			{
				Editor::SaveAll();
				JsonArchiveWriter writer;
				writer.WriteBool("ok", true);
				out = writer.EmitAsString();
			});
			WriteJson(res, ok ? 200 : 503, ok ? out : ErrorJson("editor busy"));
		});
	}
}
