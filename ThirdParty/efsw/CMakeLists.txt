find_package(Threads REQUIRED)

set(EFSW_CPP_SOURCE
	src/efsw/Debug.cpp
	src/efsw/DirectorySnapshot.cpp
	src/efsw/DirectorySnapshotDiff.cpp
	src/efsw/DirWatcherGeneric.cpp
	src/efsw/FileInfo.cpp
	src/efsw/FileSystem.cpp
	src/efsw/FileWatcher.cpp
	src/efsw/FileWatcherCWrapper.cpp
	src/efsw/FileWatcherGeneric.cpp
	src/efsw/FileWatcherImpl.cpp
	src/efsw/Log.cpp
	src/efsw/String.cpp
	src/efsw/System.cpp
	src/efsw/Watcher.cpp
	src/efsw/WatcherGeneric.cpp
)

if(WIN32)
	list(APPEND EFSW_CPP_SOURCE
		src/efsw/platform/win/FileSystemImpl.cpp
		src/efsw/platform/win/SystemImpl.cpp
	)
else()
	list(APPEND EFSW_CPP_SOURCE
		src/efsw/platform/posix/FileSystemImpl.cpp
		src/efsw/platform/posix/SystemImpl.cpp
	)
endif()

if(APPLE)
	list(APPEND EFSW_CPP_SOURCE
		src/efsw/FileWatcherFSEvents.cpp
		src/efsw/FileWatcherKqueue.cpp
		src/efsw/WatcherFSEvents.cpp
		src/efsw/WatcherKqueue.cpp
	)
elseif(WIN32)
	list(APPEND EFSW_CPP_SOURCE
		src/efsw/FileWatcherWin32.cpp
		src/efsw/WatcherWin32.cpp
	)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux" OR ${CMAKE_SYSTEM_NAME} MATCHES "Android")
	list(APPEND EFSW_CPP_SOURCE
		src/efsw/FileWatcherInotify.cpp
		src/efsw/WatcherInotify.cpp
	)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	list(APPEND EFSW_CPP_SOURCE
		src/efsw/FileWatcherKqueue.cpp
		src/efsw/WatcherKqueue.cpp
	)
endif()

add_library(efsw-static STATIC ${EFSW_CPP_SOURCE})

target_include_directories(efsw-static PUBLIC include)
target_include_directories(efsw-static PRIVATE src)

target_compile_features(efsw-static PRIVATE cxx_std_11)

if(MSVC)
	target_compile_definitions(efsw-static PRIVATE _SCL_SECURE_NO_WARNINGS)
	target_compile_options(efsw-static PRIVATE /W0)
else()
	target_compile_options(efsw-static PRIVATE -Wall -Wno-long-long -fPIC)
endif()

if(APPLE)
	target_link_libraries(efsw-static PRIVATE "-framework CoreFoundation" "-framework CoreServices")
elseif(NOT WIN32)
	target_link_libraries(efsw-static PRIVATE Threads::Threads)
endif()
