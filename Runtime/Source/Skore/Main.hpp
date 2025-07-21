// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

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
#else
int main(int argc, char** argv)
{
	return Main(argc, argv);
}
#endif

