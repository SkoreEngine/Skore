#include "FileInterfaceSkore.hpp"

#include <cstdio>
#include <cstring>

#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore.RmlUi");

		struct SkoreFile
		{
			String content;
			usize  pos = 0;
		};
	}

	Rml::FileHandle FileInterfaceSkore::Open(const Rml::String& path)
	{
		RID rid = Resources::FindByPath(StringView(path.c_str(), path.size()));
		if (!rid)
		{
			logger.Warn("RmlUi could not resolve file '{}' through Resources", path.c_str());
			return 0;
		}

		ResourceObject object = Resources::Read(rid);
		if (!object)
		{
			return 0;
		}

		SkoreFile* file = Alloc<SkoreFile>();
		file->content = object.GetString(object.GetIndex("Content"));
		return reinterpret_cast<Rml::FileHandle>(file);
	}

	void FileInterfaceSkore::Close(Rml::FileHandle file)
	{
		DestroyAndFree(reinterpret_cast<SkoreFile*>(file));
	}

	size_t FileInterfaceSkore::Read(void* buffer, size_t size, Rml::FileHandle file)
	{
		SkoreFile* skoreFile = reinterpret_cast<SkoreFile*>(file);
		usize      remaining = skoreFile->content.Size() - skoreFile->pos;
		usize      toRead = Math::Min(static_cast<usize>(size), remaining);
		memcpy(buffer, skoreFile->content.CStr() + skoreFile->pos, toRead);
		skoreFile->pos += toRead;
		return toRead;
	}

	bool FileInterfaceSkore::Seek(Rml::FileHandle file, long offset, int origin)
	{
		SkoreFile* skoreFile = reinterpret_cast<SkoreFile*>(file);
		usize      size = skoreFile->content.Size();
		i64        base = 0;

		switch (origin)
		{
			case SEEK_SET: base = 0; break;
			case SEEK_CUR: base = static_cast<i64>(skoreFile->pos); break;
			case SEEK_END: base = static_cast<i64>(size); break;
			default: return false;
		}

		i64 newPos = base + offset;
		if (newPos < 0 || newPos > static_cast<i64>(size))
		{
			return false;
		}

		skoreFile->pos = static_cast<usize>(newPos);
		return true;
	}

	size_t FileInterfaceSkore::Tell(Rml::FileHandle file)
	{
		return reinterpret_cast<SkoreFile*>(file)->pos;
	}
}
