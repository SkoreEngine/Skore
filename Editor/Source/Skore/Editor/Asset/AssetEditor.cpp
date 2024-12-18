#include "AssetEditor.hpp"

#include <algorithm>
#include <regex>
#include <thread>

#include "AssetTypes.hpp"
#include "Skore/Engine.hpp"
#include "Skore/Core/Chronometer.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/FileTypes.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Core/StaticContent.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Editor/Editor.hpp"

namespace Skore
{
    namespace
    {
        Array<AssetFile*>                  packages;
        AssetFile*                         project;
        VoidPtr                            projectlibrary;
        AssetFile*                         projectAsset;
        HashMap<UUID, AssetFile*>          assets;
        HashMap<TypeID, Array<AssetFile*>> assetsByType;

        Array<AssetImporter*>           importers;
        HashMap<String, AssetImporter*> extensionImporters;

        Array<AssetHandler*>           handlers;
        HashMap<String, AssetHandler*> handlersByExtension;
        HashMap<TypeID, AssetHandler*> handlersByTypeID;

        Texture folderTexture = {};
        Texture fileTexture = {};

        String tempFolder = {};
        String bufferTempFolder = {};

        Logger& logger = Logger::GetLogger("Skore::AssetEditor");

        AssetFile* AllocateNew(StringView name)
        {
            AssetFile* assetFile = MemoryGlobals::GetDefaultAllocator().Alloc<AssetFile>();
            assetFile->fileName = name;
            assetFile->hash = HashInt32(HashValue(reinterpret_cast<usize>(assetFile)));
            assetFile->currentVersion = 1;
            return assetFile;
        }

        void AddAssetFile(AssetFile* assetFile)
        {
            //assetsByType
            if (assetFile->handler && assetFile->handler->GetAssetTypeID() != 0)
            {
                auto it = assetsByType.Find(assetFile->handler->GetAssetTypeID());
                if (it == assetsByType.end())
                {
                    it = assetsByType.Emplace(assetFile->handler->GetAssetTypeID(), Array<AssetFile*>{}).first;
                }
                it->second.EmplaceBack(assetFile);
            }

            assets.Insert(assetFile->uuid, assetFile);
        }

        void RemoveAssetFile(AssetFile* assetFile)
        {
            if (assetFile->handler && assetFile->handler->GetAssetTypeID() != 0)
            {
                auto it = assetsByType.Find(assetFile->handler->GetAssetTypeID());
                if (it == assetsByType.end())
                {
                    it = assetsByType.Emplace(assetFile->handler->GetAssetTypeID(), Array<AssetFile*>{}).first;
                }

                if (auto itAsset = FindFirst(it->second.begin(), it->second.end(), assetFile))
                {
                    it->second.Erase(itAsset);
                }
            }

            assets.Erase(assetFile->uuid);
        }

        AssetFile* ScanForAssets(StringView path)
        {
            FileStatus status = FileSystem::GetFileStatus(path);
            if (!status.exists) return nullptr;

            //TODO check if exists in assets

            if (status.isDirectory)
            {
                AssetFile* assetFile = AllocateNew(Path::Name(path));
                assetFile->absolutePath = path;
                assetFile->isDirectory = true;
                assetFile->persistedVersion = 1;
                assetFile->uuid = UUID::RandomUUID();

                for (const String& child : DirectoryEntries{path})
                {
                    if (AssetFile* assetChild = ScanForAssets(child))
                    {
                        assetChild->parent = assetFile;
                        assetFile->children.EmplaceBack(assetChild);
                    }
                }
                AddAssetFile(assetFile);
                return assetFile;
            }

            String extension = Path::Extension(path);
            if (extension == ".buffer") return nullptr;

            if (extension != ".info")
            {
                AssetFile* assetFile = AllocateNew(Path::Name(path));
                assetFile->isDirectory = false;
                assetFile->absolutePath = path;
                assetFile->extension = Path::Extension(path);
                assetFile->persistedVersion = 1;

                String infoFile = Path::Join(path, ".info");
                if (FileSystem::GetFileStatus(infoFile).exists)
                {
                    JsonArchiveReader jsonAssetReader(FileSystem::ReadFileAsString(infoFile));
                    ArchiveValue      root = jsonAssetReader.GetRoot();
                    assetFile->uuid = UUID::FromString(jsonAssetReader.StringValue(jsonAssetReader.GetObjectValue(root, "uuid")));
                }
                else
                {
                    assetFile->uuid = UUID::RandomUUID();
                }

                if (auto it = handlersByExtension.Find(assetFile->extension))
                {
                    assetFile->handler = it->second;
                    Assets::Create(assetFile->uuid, assetFile);
                }

                AddAssetFile(assetFile);
                return assetFile;
            }

            return nullptr;
        }
    }

    bool AssetFile::IsDirty() const
    {
        return currentVersion > persistedVersion;
    }

    void AssetFile::RemoveFromParent()
    {
        if (parent)
        {
            if (auto it = FindFirst(parent->children.begin(), parent->children.end(), this))
            {
                parent->children.Erase(it);
            }
        }
    }

    Asset* AssetFile::LoadAsset()
    {
        if (TypeHandler* typeHandler = Registry::FindTypeById(handler->GetAssetTypeID()))
        {
            Asset* asset = typeHandler->Cast<Asset>(typeHandler->NewInstance());
            asset->SetTypeHandler(typeHandler);
            asset->SetUUID(uuid);
            asset->SetLoader(this);
            handler->Load(this, typeHandler, asset);
            return asset;
        }
        return nullptr;
    }

    void AssetFile::Reload(Asset* asset)
    {
        if (TypeHandler* typeHandler = Registry::FindTypeById(handler->GetAssetTypeID()))
        {
            handler->Load(this, typeHandler, asset);
        }
    }

    usize AssetFile::LoadStream(usize offset, usize size, Array<u8>& arr)
    {
        String bufferFile = tempBuffer.Empty() ? Path::Join(absolutePath, ".buffer") : tempBuffer;
        FileHandler file = FileSystem::OpenFile(bufferFile, AccessMode::ReadOnly);

        if (size == 0)
        {
            size = FileSystem::GetFileSize(file);
        }

        if (size >= arr.Size())
        {
            arr.Resize(size);
        }

        FileSystem::ReadFileAt(file, arr.Data(), size, offset);
        FileSystem::CloseFile(file);

        return size;
    }

    StringView AssetFile::GetName()
    {
        return fileName;
    }

    String AssetFile::MakePathName() const
    {
        String pathName = path;
        pathName = std::regex_replace(pathName.begin(), std::regex("//"), "_").c_str();
        pathName = std::regex_replace(pathName.begin(), std::regex(":"), "").c_str();
        std::replace(pathName.begin(), pathName.end(), '/', '_');
        return pathName;
    }

    OutputFileStream AssetFile::CreateStream()
    {
        tempBuffer = Path::Join(bufferTempFolder, uuid.ToString(), ".buffer");
        return OutputFileStream(tempBuffer);
    }

    Texture AssetFile::GetThumbnail()
    {
        if (isDirectory)
        {
            return folderTexture;
        }

        if (!thumbnailVerified && handler)
        {
            thumbnailVerified = true;

            String thumbnailFolders = Path::Join(tempFolder, "Thumbnails");
            if (!FileSystem::GetFileStatus(thumbnailFolders).exists)
            {
                FileSystem::CreateDirectory(thumbnailFolders);
            }

            std::thread thread = std::thread([&]
            {
                String cachePath = Path::Join(tempFolder, "Thumbnails", uuid.ToString(), ".image");
                if (FileSystem::GetFileStatus(cachePath).exists)
                {
                    Image image(128, 128, 4);
                    image.data = FileSystem::ReadFileAsByteArray(cachePath);

                    Editor::ExecuteOnMainThread([image, this]
                    {
                        thumbnail = Graphics::CreateTextureFromImage(image);
                    });
                }
                else
                {
                    if (Image image = handler->GenerateThumbnail(this); !image.Empty())
                    {
                        if (FileHandler fileHandler = FileSystem::OpenFile(cachePath, AccessMode::WriteOnly))
                        {
                            FileSystem::WriteFile(fileHandler, image.GetData().Data(), image.GetData().Size());
                            FileSystem::CloseFile(fileHandler);
                        }
                        Editor::ExecuteOnMainThread([image, this]
                        {
                            thumbnail = Graphics::CreateTextureFromImage(image);
                        });
                    }
                }
            });
            thread.detach();
        }

        if (thumbnail)
        {
            return thumbnail;
        }

        return fileTexture;
    }

    void AssetFile::MoveTo(AssetFile* newParent)
    {
        RemoveFromParent();

        parent = newParent;
        newParent->children.EmplaceBack(this);

        UpdatePath();

        currentVersion++;
    }

    bool AssetFile::IsChildOf(AssetFile* item) const
    {
        if (parent != nullptr)
        {
            if (parent == item)
            {
                return true;
            }

            if (parent->IsChildOf(item))
            {
                return true;
            }
        }
        return false;
    }

    void AssetFile::Destroy()
    {
        RemoveAssetFile(this);

        String infoFile = Path::Join(absolutePath, ".info");
        String bufferFile = Path::Join(absolutePath, ".buffer");
        String thumbnail = Path::Join(tempFolder, "Thumbnails", uuid.ToString(), ".image");

        FileSystem::Remove(infoFile);
        FileSystem::Remove(bufferFile);
        FileSystem::Remove(absolutePath);
        FileSystem::Remove(thumbnail);

        if (isDirectory)
        {
            for (AssetFile* file : children)
            {
                file->Destroy();
            }
        }

        logger.Debug("asset {} destroyed ", absolutePath);

        MemoryGlobals::GetDefaultAllocator().DestroyAndFree(this);
    }

    void AssetFile::UpdatePath()
    {
        path = parent->path + "/" + fileName + extension;
        Assets::SetPath(uuid, path);
        for(AssetFile* child : children)
        {
            child->UpdatePath();
        }
    }

    bool AssetFile::IsNewAsset() const
    {
        return persistedVersion == 0;
    }

    AssetFile::~AssetFile()
    {
        if (thumbnail)
        {
            Graphics::DestroyTexture(thumbnail);
        }
    }

    void AssetEditor::AddPackage(StringView name, StringView directory)
    {
        logger.Debug("start scanning package files {} ", directory);
        if (AssetFile* assetFile = ScanForAssets(directory))
        {
            assetFile->fileName = name;
            packages.EmplaceBack(assetFile);

            assetFile->path = String{name} + ":/";
            for(AssetFile* child : assetFile->children)
            {
                child->UpdatePath();
            }
        }
        logger.Debug("end scanning files");
    }

    void AssetEditor::SetProject(StringView name, StringView directory)
    {
        tempFolder = Path::Join(directory, "Temp");
        if (!FileSystem::GetFileStatus(tempFolder).exists)
        {
            FileSystem::CreateDirectory(tempFolder);
        }

        bufferTempFolder = Path::Join(tempFolder, "Buffers");
        if (FileSystem::GetFileStatus(bufferTempFolder).exists)
        {
            FileSystem::Remove(bufferTempFolder);
        }
        FileSystem::CreateDirectory(bufferTempFolder);

        String assetFolder = Path::Join(directory, "Assets");

        if (!FileSystem::GetFileStatus(assetFolder).exists)
        {
            FileSystem::CreateDirectory(assetFolder);
        }

        String binaries = Path::Join(directory, "Binaries");
        if (FileSystem::GetFileStatus(binaries).exists)
        {
            projectlibrary = Platform::LoadDynamicLib(Path::Join(binaries, name));
            if (auto func = reinterpret_cast<void(*)()>(Platform::GetFunctionAddress(projectlibrary, "SK_LoadPlugin")))
            {
                func();
            }
        }

        project = AllocateNew(name);
        project->absolutePath = directory;
        project->isDirectory = true;
        project->persistedVersion = 1;
        project->uuid = UUID::RandomUUID();//TODO read from project file.
        project->canAcceptNewAssets = false;

        logger.Debug("start scanning asset files {}", assetFolder);

        projectAsset = ScanForAssets(assetFolder);

        if (projectAsset)
        {
            project->children.EmplaceBack(projectAsset);

            projectAsset->parent = project;
            projectAsset->path = String{name} + ":/";
            for (AssetFile* child : projectAsset->children)
            {
                child->UpdatePath();
            }
        }

        logger.Debug("asset files scanned successfully ");
    }

    AssetFile* AssetEditor::CreateDirectory(AssetFile* parent)
    {
        SK_ASSERT(parent, "parent cannot be null");

        AssetFile* newDirectory = MemoryGlobals::GetDefaultAllocator().Alloc<AssetFile>();

        newDirectory->fileName = CreateUniqueName(parent, "New Folder");
        newDirectory->absolutePath = Path::Join(parent->absolutePath, newDirectory->fileName);
        newDirectory->hash = HashInt32(HashValue(newDirectory->absolutePath));
        newDirectory->isDirectory = true;
        newDirectory->currentVersion = 1;
        newDirectory->persistedVersion = 0;
        newDirectory->parent = parent;
        newDirectory->uuid = UUID::RandomUUID();

        AddAssetFile(newDirectory);

        parent->children.EmplaceBack(newDirectory);

        return newDirectory;
    }

    AssetFile* AssetEditor::CreateAsset(AssetFile* parent, TypeID typeId, StringView suggestedName)
    {
        SK_ASSERT(parent, "parent cannot be null");

        if (auto it = handlersByTypeID.Find(typeId))
        {
            TypeHandler* typeHandler = Registry::FindTypeById(typeId);
            String       assetName = suggestedName.Empty() ? String("New ").Append(typeHandler->GetSimpleName()) : String(suggestedName);
            String       absolutePath = Path::Join(parent->absolutePath, assetName, it->second->Extension());

            for(AssetFile* child: parent->children)
            {
                if (child->absolutePath == absolutePath)
                {
                    child->currentVersion++;
                    return child;
                }
            }

            AssetFile* newAsset = MemoryGlobals::GetDefaultAllocator().Alloc<AssetFile>();
            newAsset->fileName = CreateUniqueName(parent, assetName);
            newAsset->extension = it->second->Extension();
            newAsset->absolutePath = absolutePath;
            newAsset->hash = HashInt32(HashValue(newAsset->absolutePath));
            newAsset->isDirectory = false;
            newAsset->currentVersion = 1;
            newAsset->persistedVersion = 0;
            newAsset->parent = parent;
            newAsset->uuid = UUID::RandomUUID();
            newAsset->handler = it->second;
            AddAssetFile(newAsset);

            parent->children.EmplaceBack(newAsset);

            Assets::Create(newAsset->uuid, newAsset);

            logger.Debug("asset {} created on {} ", assetName, parent->absolutePath);

            return newAsset;
        }
        SK_ASSERT(false, "handler not found");
        return nullptr;
    }

    void AssetEditor::UpdateAssetValue(AssetFile* assetFile, Asset* asset)
    {
        asset->OnChange();
        assetFile->currentVersion++;
    }

    void AssetEditor::Rename(AssetFile* assetFile, StringView newName)
    {
        assetFile->fileName = newName;
        assetFile->UpdatePath();
        assetFile->currentVersion++;
    }

    void AssetEditor::GetUpdatedAssets(Array<AssetFile*>& updatedAssets)
    {
        for (auto& it : assets)
        {
            if (it.second->IsDirty())
            {
                updatedAssets.EmplaceBack(it.second);
            }
        }
    }

    void AssetEditor::SaveAssets(Span<AssetFile*> assetsToSave)
    {
        for (AssetFile* assetFile : assetsToSave)
        {
            if (assetFile->active)
            {
                String newAbsolutePath = Path::Join(assetFile->parent->absolutePath, assetFile->fileName, assetFile->extension);
                bool   moved = newAbsolutePath != assetFile->absolutePath;

                if (assetFile->isDirectory)
                {
                    if (FileSystem::GetFileStatus(assetFile->absolutePath).exists)
                    {
                        if (assetFile->absolutePath != newAbsolutePath)
                        {
                            FileSystem::Rename(assetFile->absolutePath, newAbsolutePath);
                        }
                    }
                    else
                    {
                        FileSystem::CreateDirectory(newAbsolutePath);
                    }
                }
                else
                {
                    if (moved)
                    {
                        String oldBufferFile = Path::Join(assetFile->absolutePath, ".buffer");
                        String newBufferFile = Path::Join(newAbsolutePath, ".buffer");
                        FileSystem::Rename(oldBufferFile, newBufferFile);

                        FileSystem::Rename(Path::Join(assetFile->absolutePath, ".info"), Path::Join(newAbsolutePath, ".info"));
                        FileSystem::Rename(assetFile->absolutePath, newAbsolutePath);
                    }

                    if (auto it = handlersByExtension.Find(assetFile->extension))
                    {
                        String            infoFile = Path::Join(newAbsolutePath, ".info");
                        JsonArchiveWriter writer;

                        ArchiveValue root = writer.CreateObject();
                        writer.AddToObject(root, "uuid", writer.StringValue(assetFile->uuid.ToString()));
                        FileSystem::SaveFileAsString(infoFile, JsonArchiveWriter::Stringify(root));

                        AssetHandler* handler = it->second;
                        handler->Save(newAbsolutePath, assetFile);
                    }

                    if (!assetFile->tempBuffer.Empty())
                    {
                        String newBufferFile = Path::Join(newAbsolutePath, ".buffer");
                        FileSystem::Rename(assetFile->tempBuffer, newBufferFile);
                        assetFile->tempBuffer.Clear();
                    }
                }

                logger.Debug("asset updated from path {} to path {} ", assetFile->absolutePath, newAbsolutePath);

                assetFile->absolutePath = newAbsolutePath;
                assetFile->persistedVersion = assetFile->currentVersion;
            }
            else
            {
                assetFile->Destroy();
            }
        }
    }

    void AssetEditor::DeleteAssets(Span<AssetFile*> assetFiles)
    {
        for (AssetFile* asset : assetFiles)
        {
            asset->active = false;
            asset->currentVersion++;
            asset->RemoveFromParent();
        }
    }

    String AssetEditor::CreateUniqueName(AssetFile* parent, StringView desiredName)
    {
        if (parent == nullptr) return {};

        u32    count{};
        String finalName = desiredName;
        bool   nameFound;
        do
        {
            nameFound = true;
            for (AssetFile* child : parent->children)
            {
                if (finalName == child->fileName)
                {
                    finalName = desiredName;
                    finalName += " (";
                    finalName.Append(ToString(++count));
                    finalName += ")";
                    nameFound = false;
                    break;
                }
            }
        }
        while (!nameFound);
        return finalName;
    }

    void AssetEditor::ImportAssets(AssetFile* parent, Span<String> paths)
    {
        for (const String& path : paths)
        {
            String extension = Path::Extension(path);
            if (auto it = extensionImporters.Find(extension))
            {
                AssetImporter* io = it->second;
                io->ImportAsset(parent, path);
            }
        }
    }

    void AssetEditor::FilterExtensions(Array<FileFilter>& extensions)
    {
        for (auto& it : extensionImporters)
        {
            extensions.EmplaceBack(FileFilter{
                .name = it.first.CStr(),
                .spec = it.first.CStr()
            });
        }
    }

    Span<AssetFile*> AssetEditor::GetPackages()
    {
        return packages;
    }

    AssetFile* AssetEditor::GetProject()
    {
        return project;
    }

    AssetFile* AssetEditor::GetAssetFolder()
    {
        return projectAsset;
    }

    void AssetEditorShutdown()
    {
        Graphics::WaitQueue();
        Graphics::DestroyTexture(folderTexture);
        Graphics::DestroyTexture(fileTexture);

        for (auto& it : assets)
        {
            MemoryGlobals::GetDefaultAllocator().DestroyAndFree(it.second);
        }

        packages.Clear();
        assetsByType.Clear();
        assets.Clear();

        for (AssetImporter* io : importers)
        {
            MemoryGlobals::GetDefaultAllocator().DestroyAndFree(io);
        }

        importers.Clear();
        extensionImporters.Clear();

        handlers.Clear();

        //Platform::FreeDynamicLib(projectlibrary);
    }

    Span<AssetFile*> AssetEditor::GetAssetsOfType(TypeID typeId)
    {
        auto it = assetsByType.Find(typeId);
        if (it != assetsByType.end())
        {
            return it->second;
        }
        return {};
    }

    AssetFile* AssetEditor::FindAssetFileByUUID(UUID uuid)
    {
        if (auto it = assets.Find(uuid))
        {
            return it->second;
        }
        return nullptr;
    }

    String AssetEditor::GetTempFolder()
    {
        return tempFolder;
    }

    void AssetEditor::CreateCMakeProject()
    {
        String cmakePath = Path::Join(project->absolutePath, "CMakeLists.txt");
        String source = Path::Join(project->absolutePath, "Source");
    }

    bool AssetEditor::CanCreateCMakeProject()
    {
        for (const String& dir : DirectoryEntries{project->absolutePath})
        {
            String name = Path::Name(dir) + Path::Extension(dir);
            if (name == "CMakeLists.txt")
            {
                return false;
            }
        }
        return true;
    }

    void ExportAssetFile(AssetFile* assetFile, OutputFileStream& stream, ArchiveWriter& writer, ArchiveValue arr)
    {
        if (!assetFile->isDirectory)
        {
            if (Asset* asset = Assets::LoadNoCache(assetFile->uuid))
            {
                Array<u8>    tempArray;
                ArchiveValue assetObj = writer.CreateObject();

                String    assetStr = JsonArchiveWriter::Stringify(Serialization::Serialize(asset->GetTypeHandler(), writer, asset), false, true);
                usize size = asset->LoadStream(0, 0, tempArray);

                usize assetOffset = stream.Write(reinterpret_cast<u8*>(assetStr.begin()), assetStr.Size());
                usize streamOffset = stream.Write(tempArray.begin(), size);

                writer.AddToObject(assetObj, "uuid", writer.StringValue(assetFile->uuid.ToString()));
                writer.AddToObject(assetObj, "name", writer.StringValue(assetFile->fileName));
                writer.AddToObject(assetObj, "path", writer.StringValue(assetFile->path));

                if (TypeHandler* typeHandler = Registry::FindTypeById(assetFile->handler->GetAssetTypeID()))
                {
                    writer.AddToObject(assetObj, "type", writer.StringValue(typeHandler->GetName()));
                }

                //TODO asset format
                //TODO asset compression

                writer.AddToObject(assetObj, "assetOffset", writer.UIntValue(assetOffset));
                writer.AddToObject(assetObj, "assetSize", writer.UIntValue(assetStr.Size()));
                writer.AddToObject(assetObj, "streamOffset", writer.UIntValue(streamOffset));
                writer.AddToObject(assetObj, "streamSize", writer.UIntValue(size));

                writer.AddToArray(arr, assetObj);

                DestroyAndFree(asset);
            }
        }
        else
        {
            for (AssetFile* childAssetFile : assetFile->children)
            {
                ExportAssetFile(childAssetFile, stream, writer, arr);
            }
        }
    }

    void ExportAssetFile(AssetFile* file, StringView directory)
    {
        OutputFileStream stream(Path::Join(directory, file->fileName, ".pak"));
        JsonArchiveWriter writer;
        ArchiveValue arr = writer.CreateArray();
        ExportAssetFile(file, stream, writer, arr);
        FileSystem::SaveFileAsString(Path::Join(directory, file->fileName, ".assets"), JsonArchiveWriter::Stringify(arr));
        stream.Close();
    }

    void AssetEditor::Export(StringView directory)
    {
        String assetDir = Path::Join(directory, "Assets");
        if (!FileSystem::GetFileStatus(assetDir).exists)
        {
            FileSystem::CreateDirectory(assetDir);
        }

        for (AssetFile* package : packages)
        {
            ExportAssetFile(package, assetDir);
        }
        ExportAssetFile(project, assetDir);
    }

    void AssetEditorUpdate(f64 deltaTime)
    {

    }

    void AssetEditorInit()
    {
        Event::Bind<OnShutdown, AssetEditorShutdown>();
        Event::Bind<OnUpdate, AssetEditorUpdate>();

        importers = Registry::InstantiateDerived<AssetImporter>();

        for (AssetImporter* importer : importers)
        {
            for (const String& extension : importer->ImportExtensions())
            {
                extensionImporters.Insert(extension, importer);
            }
        }

        handlers = Registry::InstantiateDerived<AssetHandler>();

        for (AssetHandler* handler : handlers)
        {
            logger.Debug("registered asset handler for extension {} ", handler->Extension());

            if (StringView extension = handler->Extension(); !extension.Empty())
            {
                handlersByExtension.Insert(extension, handler);
            }

            if (TypeID typeId = handler->GetAssetTypeID(); typeId != 0)
            {
                handlersByTypeID.Insert(typeId, handler);
            }
        }

        folderTexture = StaticContent::GetTextureFile("Content/Images/FolderIcon.png");
        fileTexture = StaticContent::GetTextureFile("Content/Images/file.png");
    }
}
