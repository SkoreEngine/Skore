#include "FileWatcher.hpp"
#include "FileSystem.hpp"

#include <mutex>
#include <optional>
#include <queue>
#include <thread>

#include "Path.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Logger.hpp"

namespace Skore
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Skore::FileWatcher");
    }

    struct FileWatcherData
    {
        String       path{};
        VoidPtr      userData{};
        bool         isDirectory{};
        u64          lastModifiedTime{};
        u64          fileId{};
        HashSet<u64> children{};
    };

    struct FileWatcherInternal
    {
        bool                       running = true;
        std::optional<std::thread> thread;

        std::mutex                      modifiedMutex{};
        std::queue<FileWatcherModified> modified{};

        std::queue<FileWatcherData> newFiles{};
        std::mutex                  newFilesMutex{};

        Array<FileWatcherData> watchedFiles{};

        void ThreadLoop()
        {
            while (running)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                {
                    std::unique_lock lock(newFilesMutex);
                    while (!newFiles.empty())
                    {
                        FileWatcherData& data = newFiles.front();
                        if (data.isDirectory)
                        {
                            for (const String& file : DirectoryEntries{data.path})
                            {
                                data.children.Emplace(FileSystem::GetFileId(file));
                            }
                        }
                        watchedFiles.EmplaceBack(Traits::Move(data));
                        newFiles.pop();
                    }
                }

                auto it = watchedFiles.begin();
                while (it != watchedFiles.end())
                {
                    FileWatcherData& data = *it;
                    FileStatus       fileStatus = FileSystem::GetFileStatus(data.path);

                    if (!fileStatus.exists)
                    {
                        //check if that's renamed
                        bool renamed = false;
                        for (const String& file : DirectoryEntries{Path::Parent(data.path)})
                        {
                            if (FileSystem::GetFileId(file) == data.fileId)
                            {
                                std::unique_lock lockQueue(modifiedMutex);
                                modified.emplace(FileWatcherModified{
                                    .userData = data.userData,
                                    .oldName = Path::Name(data.path),
                                    .name = Path::Name(file),
                                    .path = file,
                                    .event = FileNotifyEvent::Renamed
                                });

                                data.path = file;

                                logger.Debug("file {} id {} renamed",  data.path, data.fileId);

                                renamed = true;
                                ++it;
                                break;
                            }
                        }

                        if (!renamed)
                        {
                            std::unique_lock lockQueue(modifiedMutex);
                            modified.emplace(FileWatcherModified{
                                .userData = data.userData,
                                .path = data.path,
                                .event = FileNotifyEvent::Removed
                            });
                            logger.Debug("file {} id {} removed",  data.path, data.fileId);
                            it = watchedFiles.Erase(it);
                        }
                    }
                    else if (fileStatus.isDirectory)
                    {
                        //if that's directory only need to check if there is a new file.
                        HashSet<u64> actualIds{};
                        for (const String& file : DirectoryEntries{data.path})
                        {
                            u64 fileId = FileSystem::GetFileId(file);
                            if (fileId == 0) continue;

                            actualIds.Insert(fileId);

                            if (!data.children.Has(fileId))
                            {
                                data.children.Emplace(fileId);
                                std::unique_lock lockQueue(modifiedMutex);
                                modified.emplace(FileWatcherModified{
                                    .userData = data.userData,
                                    .path = file,
                                    .event = FileNotifyEvent::Added
                                });
                                logger.Debug("file {} id {} added on {} ", file, fileId,  data.path);
                            }
                        }

                        Array<u64> toDelete{};
                        for(auto& it: data.children)
                        {
                            if (!actualIds.Has(it))
                            {
                                toDelete.EmplaceBack(it);
                            }
                        }
                        for(auto id: toDelete)
                        {
                            data.children.Erase(id);
                        }

                        ++it;
                    }
                    else if (fileStatus.lastModifiedTime != data.lastModifiedTime)
                    {
                        data.lastModifiedTime = fileStatus.lastModifiedTime;
                        std::unique_lock lockQueue(modifiedMutex);
                        modified.emplace(FileWatcherModified{
                            .userData = data.userData,
                            .path = data.path,
                            .event = FileNotifyEvent::Modified
                        });
                        logger.Debug("file {} modified",  data.path);

                        ++it;
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }
    };


    void FileWatcher::Watch(VoidPtr userData, const StringView& path)
    {
        if (internal)
        {
            if (FileStatus fileStatus = FileSystem::GetFileStatus(path); fileStatus.exists)
            {
                internal->newFiles.emplace(FileWatcherData{
                    .path = path,
                    .userData = userData,
                    .isDirectory = fileStatus.isDirectory,
                    .lastModifiedTime = fileStatus.lastModifiedTime,
                    .fileId = FileSystem::GetFileId(path)
                });
                logger.Debug("file {} added to watcher ", path);
            }
        }
    }

    void FileWatcher::CheckForUpdates(FileWatcherCallbackFn watcherCallbackFn) const
    {
        if (internal)
        {
            std::unique_lock lockQueue(internal->modifiedMutex);
            while (!internal->modified.empty())
            {
                FileWatcherModified& data = internal->modified.front();
                watcherCallbackFn(data);
                internal->modified.pop();
            }
        }
    }

    void FileWatcher::Check()
    {
        //TODO
    }

    void FileWatcher::Start()
    {
        internal = Alloc<FileWatcherInternal>();
        internal->thread = std::make_optional(std::thread(&FileWatcherInternal::ThreadLoop, internal));
    }

    void FileWatcher::Stop()
    {
        if (internal)
        {
            internal->running = false;
            if (internal->thread->joinable())
            {
                internal->thread->join();
                internal->thread.reset();
            }
            DestroyAndFree(internal);
            internal = nullptr;
        }
    }
}
