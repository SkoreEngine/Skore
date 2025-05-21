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

#include "Skore/Core/StringView.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	struct Path
	{
		static String Parent(const StringView& path)
		{
			String parentPath{};
			bool   foundSeparator = false;
			auto   it = path.end();
			do
			{
				if (foundSeparator)
				{
					parentPath.Insert(parentPath.begin(), *it);
				}
				if (*it == SK_PATH_SEPARATOR)
				{
					foundSeparator = true;
				}
				if (it == path.begin())
				{
					return parentPath;
				}
				it--;
			}
			while (true);
		}

		static StringView Extension(const StringView& path)
		{
			auto it = path.end();
			while (it != path.begin())
			{
				it--;
				if (*it == '.')
				{
					return StringView{it, (usize)(path.end() - it)};
				}
				else if (*it == SK_PATH_SEPARATOR)
				{
					break;
				}
			}
			return {};
		}

		template <typename... T>
		static String Join(const T&... paths)
		{
			String retPath{};
			auto   append = [&](StringView path)
			{
				if (path.Empty()) return;

				char first = path[0];
				if (first != '/' && first != '\\' && !retPath.Empty())
				{
					char last = retPath[retPath.Size() - 1];
					if (last != '/' && last != '\\')
					{
						retPath.Append(SK_PATH_SEPARATOR);
					}
				}

				for (int i = 0; i < path.Size(); ++i)
				{
					if (path[i] == '/' || path[i] == '\\')
					{
						if (i < path.Size() - 1)
						{
							retPath.Append(SK_PATH_SEPARATOR);
						}
					}
					else
					{
						retPath.Append(path[i]);
					}
				}
			};
			(append(StringView{paths}), ...);
			return retPath;
		}

		static String ExtractName(const StringView& parent, const StringView& path)
		{
			if (parent.Size() >= path.Size()) return {};

			String ret;
			ret.Reserve(path.Size() - parent.Size());

			for (usize i = parent.Size(); i < path.Size(); ++i)
			{
				char c = path[i];
				if (c != SK_PATH_SEPARATOR)
				{
					ret.Append(c);
				}
			}

			return ret;
		}

		static String Name(const StringView& path)
		{
			bool hasExtension = !Extension(path).Empty();

			auto it = path.end();
			if (it == path.begin()) return {};
			it--;

			//if the last char is a separator
			//like /path/folder/
			if (*it == SK_PATH_SEPARATOR)
			{
				it--;
			}

			String name{};
			bool   found{};

			if (!hasExtension)
			{
				found = true;
			}

			while (it != path.begin())
			{
				if (found && *it == SK_PATH_SEPARATOR)
				{
					return name;
				}
				if (found)
				{
					name.Insert(name.begin(), *it);
				}
				if (*it == '.' || *it == SK_PATH_SEPARATOR)
				{
					found = true;
				}
				it--;
			}
			return path;
		}
	};
}
