
#include "Skore/Common.hpp"

#ifdef SK_ANDROID

#include "Skore/IO/FileTypes.hpp"
#include "Skore/IO/FileSystem.hpp"

#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <Skore/Core/Logger.hpp>
#include "SDL3/SDL_system.h"

static AAssetManager* g_assetManager = nullptr;

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::FileSystemAndroid");

	void FileSystemPlatformInit()
	{
		JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
		jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());

		// Get AssetManager from activity
		jclass    activityClass = env->FindClass("android/app/Activity");
		jmethodID getAssetsMethod = env->GetMethodID(activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
		jobject   assetManager = env->CallObjectMethod(activity, getAssetsMethod);

		g_assetManager = AAssetManager_fromJava(env, assetManager);

		logger.Debug("asset manager found {} ", PtrToInt(g_assetManager));
	}

	DirIterator::DirIterator(const StringView& directory) : m_directory(directory),
	                                                        m_handler(nullptr)
	{
		AAssetDir* assetDir = AAssetManager_openDir(g_assetManager, directory.CStr());
		if (assetDir != nullptr)
		{
			if (const char* file = AAssetDir_getNextFileName(assetDir))
			{
				m_path = file;
			}
			m_handler = assetDir;
		}
		else
		{
			logger.Error("cannot read AAssetDir* from {}", directory);
		}
	}

	DirIterator& DirIterator::operator++()
	{
		if (m_handler)
		{
			m_path = "";
			AAssetDir* assetDir = static_cast<AAssetDir*>(m_handler);
			if (const char* file = AAssetDir_getNextFileName(assetDir))
			{
				m_path = file;
			}
		}
		return *this;
	}

	DirIterator::~DirIterator()
	{
		if (m_handler)
		{
			AAssetDir_close(static_cast<AAssetDir*>(m_handler));
		}
	}


	String FileSystem::CurrentDir()
	{
		return {};
	}

	String FileSystem::DocumentsDir()
	{
		return "";
	}

	FileStatus FileSystem::GetFileStatus(const StringView& path)
	{
		//				struct stat st{};
		//				bool exists = stat(path.CStr(), &st) == 0;
		//				return {
		//						.exists = exists,
		//						.isDirectory = S_ISDIR(st.st_mode),
		//						.lastModifiedTime = static_cast<u64>(st.st_mtime),
		//						.fileSize = static_cast<u64>(st.st_size),
		//				};
		return {};
	}

	String FileSystem::AppFolder()
	{
		return {};
	}

	FileHandler FileSystem::OpenFile(const StringView& path, AccessMode accessMode)
	{
		return AAssetManager_open(g_assetManager, path.CStr(), AASSET_MODE_BUFFER);
	}

	u64 FileSystem::GetFileSize(FileHandler fileHandler)
	{
		return AAsset_getLength64(static_cast<AAsset*>(fileHandler.ToPtr()));
	}

	u64 FileSystem::GetFileId(const StringView& path)
	{
		return 0;
	}

	u64 FileSystem::WriteFile(FileHandler fileHandler, ConstPtr data, usize size)
	{
		logger.Error("Write file not implemented on android");
		return 0;
	}

	u64 FileSystem::ReadFile(FileHandler fileHandler, VoidPtr data, usize size)
	{
		return AAsset_read(static_cast<AAsset*>(fileHandler.ToPtr()), data, size);
	}

	u64 FileSystem::ReadFileAt(FileHandler fileHandler, VoidPtr data, usize size, usize offset)
	{
		return 0;
	}

	FileHandler FileSystem::CreateFileMapping(FileHandler fileHandler, AccessMode accessMode, usize size)
	{
		return FileHandler{};
	}

	VoidPtr FileSystem::MapViewOfFile(FileHandler fileHandler)
	{
		return nullptr;
	}

	bool FileSystem::UnmapViewOfFile(VoidPtr map)
	{
		return false;
	}

	void FileSystem::CloseFileMapping(FileHandler fileHandler) {}

	void FileSystem::CloseFile(FileHandler fileHandler) {}
}

#endif