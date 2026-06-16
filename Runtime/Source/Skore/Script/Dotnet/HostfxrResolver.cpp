#include "HostfxrResolver.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

#if SK_WIN
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
#else
	#include <cstdlib>
#endif

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::HostfxrResolver");

		struct DotnetSemVer
		{
			i64    major = 0;
			i64    minor = 0;
			i64    patch = 0;
			String prerelease{};

			static bool ParseDigits(StringView field, i64& result)
			{
				if (field.Empty())
				{
					return false;
				}

				i64 value = 0;
				for (usize i = 0; i < field.Size(); ++i)
				{
					char c = field[i];
					if (c < '0' || c > '9')
					{
						return false;
					}
					value = value * 10 + (c - '0');
				}
				result = value;
				return true;
			}

			static bool Parse(StringView text, DotnetSemVer& out)
			{
				if (text.Empty())
				{
					return false;
				}

				usize      plus = text.FindFirstOf('+');
				StringView core = plus != StringView::s_npos ? text.Substr(0, plus) : text;

				usize      dash = core.FindFirstOf('-');
				StringView numbers = dash != StringView::s_npos ? core.Substr(0, dash) : core;
				StringView prerelease = dash != StringView::s_npos ? core.Substr(dash + 1) : StringView{};

				usize firstDot = numbers.FindFirstOf('.');
				if (firstDot == StringView::s_npos)
				{
					return false;
				}

				usize secondDot = numbers.FindFirstOf('.', firstDot + 1);
				if (secondDot == StringView::s_npos)
				{
					return false;
				}

				DotnetSemVer result{};
				if (!ParseDigits(numbers.Substr(0, firstDot), result.major) ||
					!ParseDigits(numbers.Substr(firstDot + 1, secondDot - firstDot - 1), result.minor) ||
					!ParseDigits(numbers.Substr(secondDot + 1), result.patch))
				{
					return false;
				}

				result.prerelease = prerelease;
				out = result;
				return true;
			}

			static i32 CompareIdentifier(StringView a, StringView b)
			{
				i64  aNumber = 0;
				i64  bNumber = 0;
				bool aIsNumber = ParseDigits(a, aNumber);
				bool bIsNumber = ParseDigits(b, bNumber);

				if (aIsNumber && bIsNumber)
				{
					if (aNumber != bNumber)
					{
						return aNumber < bNumber ? -1 : 1;
					}
					return 0;
				}

				if (aIsNumber != bIsNumber)
				{
					return aIsNumber ? -1 : 1;
				}

				i32 cmp = a.Compare(b);
				return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
			}

			static i32 ComparePrerelease(StringView a, StringView b)
			{
				usize ai = 0;
				usize bi = 0;
				while (true)
				{
					bool aEnd = ai >= a.Size();
					bool bEnd = bi >= b.Size();
					if (aEnd && bEnd)
					{
						return 0;
					}
					if (aEnd)
					{
						return -1;
					}
					if (bEnd)
					{
						return 1;
					}

					usize aDot = a.FindFirstOf('.', ai);
					usize bDot = b.FindFirstOf('.', bi);
					usize aEndIdx = aDot != StringView::s_npos ? aDot : a.Size();
					usize bEndIdx = bDot != StringView::s_npos ? bDot : b.Size();

					i32 cmp = CompareIdentifier(a.Substr(ai, aEndIdx - ai), b.Substr(bi, bEndIdx - bi));
					if (cmp != 0)
					{
						return cmp;
					}

					ai = aEndIdx + 1;
					bi = bEndIdx + 1;
				}
			}

			static i32 Compare(const DotnetSemVer& a, const DotnetSemVer& b)
			{
				if (a.major != b.major)
				{
					return a.major < b.major ? -1 : 1;
				}
				if (a.minor != b.minor)
				{
					return a.minor < b.minor ? -1 : 1;
				}
				if (a.patch != b.patch)
				{
					return a.patch < b.patch ? -1 : 1;
				}

				bool aPre = !a.prerelease.Empty();
				bool bPre = !b.prerelease.Empty();
				if (aPre != bPre)
				{
					return aPre ? -1 : 1;
				}
				if (!aPre)
				{
					return 0;
				}
				return ComparePrerelease(a.prerelease, b.prerelease);
			}

			bool operator>(const DotnetSemVer& other) const
			{
				return Compare(*this, other) > 0;
			}
		};

		String GetHostfxrFileName()
		{
#if SK_WIN
			return "hostfxr" SK_SHARED_EXT;
#else
			return "libhostfxr" SK_SHARED_EXT;
#endif
		}

		const char* GetDotnetArch()
		{
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
			return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
			return "arm64";
#elif defined(_M_ARM) || defined(__arm__)
			return "arm";
#elif defined(_M_IX86) || defined(__i386__)
			return "x86";
#elif defined(__riscv) && (__riscv_xlen == 64)
			return "riscv64";
#else
			return "";
#endif
		}

		String ToUpper(StringView value)
		{
			String result{};
			result.Reserve(value.Size());
			for (usize i = 0; i < value.Size(); ++i)
			{
				char c = value[i];
				result.Append(c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c);
			}
			return result;
		}

		String GetBaseName(StringView path)
		{
			usize start = 0;
			for (usize i = 0; i < path.Size(); ++i)
			{
				if (path[i] == '/' || path[i] == '\\')
				{
					start = i + 1;
				}
			}
			return String{path.CStr() + start, path.Size() - start};
		}

		String GetEnvVar(const String& key)
		{
#if SK_WIN
			DWORD required = ::GetEnvironmentVariableA(key.CStr(), nullptr, 0);
			if (required == 0)
			{
				return {};
			}

			String value{};
			value.Resize(required);
			DWORD written = ::GetEnvironmentVariableA(key.CStr(), value.begin(), required);
			value.SetSize(written);
			return value;
#else
			const char* value = ::getenv(key.CStr());
			if (value == nullptr)
			{
				return {};
			}
			return String{value};
#endif
		}

#if SK_WIN
		bool IsWow64()
		{
			BOOL wow64 = FALSE;
			if (!IsWow64Process(GetCurrentProcess(), &wow64))
			{
				wow64 = FALSE;
			}
			return wow64;
		}
#endif

		bool GetLatestFxr(StringView fxrRoot, String& fxrPath)
		{
			bool         foundVer = false;
			DotnetSemVer latestVer{};
			String       latestVerStr{};

			for (const String& entry : DirectoryEntries(fxrRoot))
			{
				if (!FileSystem::GetFileStatus(entry).isDirectory)
				{
					continue;
				}

				String       ver = GetBaseName(entry);
				DotnetSemVer fxVer{};
				if (DotnetSemVer::Parse(ver, fxVer))
				{
					if (!foundVer || fxVer > latestVer)
					{
						latestVer = fxVer;
						latestVerStr = ver;
						foundVer = true;
					}
				}
			}

			if (!foundVer)
			{
				return false;
			}

			String fxrWithVer = Path::Join(fxrRoot, latestVerStr);
			String hostfxrFilePath = Path::Join(fxrWithVer, GetHostfxrFileName());

			if (!FileSystem::GetFileStatus(hostfxrFilePath).exists)
			{
				logger.Error("missing hostfxr library in directory: {}", fxrWithVer);
				return false;
			}

			fxrPath = hostfxrFilePath;
			return true;
		}

		bool GetDefaultInstallationDir(String& dotnetRoot)
		{
#if SK_WIN
			String programFilesEnv = IsWow64() ? "ProgramFiles(x86)" : "ProgramFiles";
			String programFilesDir = GetEnvVar(programFilesEnv);
			if (programFilesDir.Empty())
			{
				return false;
			}

	#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
			String dotnetRootEmulated = Path::Join(programFilesDir, "dotnet", "x64");
			if (FileSystem::GetFileStatus(Path::Join(dotnetRootEmulated, "dotnet" SK_EXEC_EXT)).exists)
			{
				dotnetRoot = dotnetRootEmulated;
				return true;
			}
	#endif

			dotnetRoot = Path::Join(programFilesDir, "dotnet");
			return true;
#elif SK_APPLE
			dotnetRoot = "/usr/local/share/dotnet";

	#if defined(__x86_64__) || defined(__amd64__)
			String dotnetRootEmulated = Path::Join(dotnetRoot, "x64");
			if (FileSystem::GetFileStatus(Path::Join(dotnetRootEmulated, "dotnet")).exists)
			{
				dotnetRoot = dotnetRootEmulated;
				return true;
			}
	#endif

			return true;
#else
			dotnetRoot = "/usr/share/dotnet";
			return true;
#endif
		}

#if !SK_WIN
		bool GetInstallLocationFromFile(StringView filePath, String& dotnetRoot)
		{
			if (!FileSystem::GetFileStatus(filePath).exists)
			{
				return false;
			}

			String content = FileSystem::ReadFileAsString(filePath);

			String line{};
			for (usize i = 0; i < content.Size(); ++i)
			{
				char c = content[i];
				if (c == '\n' || c == '\r')
				{
					break;
				}
				line.Append(c);
			}

			if (line.Empty())
			{
				return false;
			}

			dotnetRoot = line;
			return true;
		}
#endif

		bool GetDotnetSelfRegisteredDir(String& dotnetRoot)
		{
#if SK_WIN
			String subKey = String{"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\"} + GetDotnetArch();

			HKEY hkey = nullptr;
			if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey.CStr(), 0, KEY_READ | KEY_WOW64_32KEY, &hkey) != ERROR_SUCCESS)
			{
				return false;
			}

			DWORD size = 0;
			if (RegGetValueA(hkey, nullptr, "InstallLocation", RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0)
			{
				RegCloseKey(hkey);
				return false;
			}

			String value{};
			value.Resize(size);
			LSTATUS result = RegGetValueA(hkey, nullptr, "InstallLocation", RRF_RT_REG_SZ, nullptr, value.begin(), &size);
			RegCloseKey(hkey);

			if (result != ERROR_SUCCESS)
			{
				return false;
			}

			value.SetSize(size > 0 ? size - 1 : 0);
			dotnetRoot = value;
			return true;
#else
			String installLocationFile = Path::Join("/etc/dotnet", String{"install_location_"} + GetDotnetArch());
			if (GetInstallLocationFromFile(installLocationFile, dotnetRoot))
			{
				return true;
			}

			if (FileSystem::GetFileStatus(installLocationFile).exists)
			{
				return false;
			}

			String legacyInstallLocationFile = Path::Join("/etc/dotnet", "install_location");
			return GetInstallLocationFromFile(legacyInstallLocationFile, dotnetRoot);
#endif
		}

		bool GetFilePathFromEnv(const String& envKey, String& dotnetRoot)
		{
			String envValue = GetEnvVar(envKey);
			if (!envValue.Empty())
			{
				FileStatus status = FileSystem::GetFileStatus(envValue);
				if (status.exists && status.isDirectory)
				{
					dotnetRoot = envValue;
					return true;
				}
			}
			return false;
		}

		bool GetDotnetRootFromEnv(String& dotnetRoot)
		{
			String dotnetRootEnv = "DOTNET_ROOT";
			String archForEnv = GetDotnetArch();

			if (!archForEnv.Empty())
			{
				if (GetFilePathFromEnv(dotnetRootEnv + "_" + ToUpper(archForEnv), dotnetRoot))
				{
					return true;
				}
			}

#if SK_WIN
			if (IsWow64() && GetFilePathFromEnv("DOTNET_ROOT(x86)", dotnetRoot))
			{
				return true;
			}
#endif

			return GetFilePathFromEnv(dotnetRootEnv, dotnetRoot);
		}
	}

	bool HostfxrResolver::TryGetPathFromDotnetRoot(StringView dotnetRoot, String& fxrPath)
	{
		String     fxrDir = Path::Join(dotnetRoot, "host", "fxr");
		FileStatus status = FileSystem::GetFileStatus(fxrDir);
		if (!status.exists || !status.isDirectory)
		{
			logger.Warn("the host fxr folder does not exist: {}", fxrDir);
			return false;
		}
		return GetLatestFxr(fxrDir, fxrPath);
	}

	bool HostfxrResolver::TryGetPath(String& dotnetRoot, String& fxrPath)
	{
		if (!GetDotnetRootFromEnv(dotnetRoot) &&
			!GetDotnetSelfRegisteredDir(dotnetRoot) &&
			!GetDefaultInstallationDir(dotnetRoot))
		{
			return false;
		}

		return TryGetPathFromDotnetRoot(dotnetRoot, fxrPath);
	}
}
