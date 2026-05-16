#pragma once

#include "Common.hpp"
#include "App.hpp"

using namespace Skore;

namespace Skore
{
	i32 Main(int argc, char** argv);
}

#ifdef SK_WIN
#define NOMINMAX
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	int argc;
	LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);

	char** argv = new char*[argc];
	for (int i = 0; i < argc; i++)
	{
		int size = WideCharToMultiByte(CP_ACP, 0, argvW[i], -1, NULL, 0, NULL, NULL);
		argv[i] = new char[size];
		WideCharToMultiByte(CP_ACP, 0, argvW[i], -1, argv[i], size, NULL, NULL);
	}

	i32 res = Main(argc, argv);

	for (int i = 0; i < argc; i++) {
		delete[] argv[i];
	}
	delete[] argv;
	LocalFree(argvW);

	return res;
}
#elif defined(SK_ANDROID)
#ifdef __cplusplus
extern "C" {
#endif

SK_API int SkoreMain(int argc, char** argv)
{
	return Main(argc, argv);
}

#ifdef __cplusplus
}
#endif
#else
int main(int argc, char** argv)
{
	return Main(argc, argv);
}
#endif

