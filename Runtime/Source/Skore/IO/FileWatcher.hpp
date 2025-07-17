#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
    enum class FileNotifyEvent
    {
        Added,
        Removed,
        Modified,
        Renamed,
    };


    struct FileWatcherModified
    {
        VoidPtr         userData{};
        String          oldName{};
        String          name{};
        String          path{};
        FileNotifyEvent event{};
    };

    typedef void (*FileWatcherCallbackFn)(const FileWatcherModified& modified);

    struct FileWatcherInternal;

    class SK_API FileWatcher
    {
    public:
        SK_NO_COPY_CONSTRUCTOR(FileWatcher);

        FileWatcher() = default;

        void Start();
        void Stop();
        void Check();

        void Watch(VoidPtr userData, const StringView& path);
        void CheckForUpdates(FileWatcherCallbackFn watcherCallbackFn) const;

    private:
        FileWatcherInternal* internal = nullptr;
    };
}
