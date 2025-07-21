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

#include "Skore/Common.hpp"
#include "Array.hpp"
#include "String.hpp"
#include "HashMap.hpp"
#include "HashSet.hpp"
#include "StringView.hpp"

namespace Skore
{
	/**
	 * Command line argument parser
	 *
	 * Example usage:
	 * --export-api <path>    Export API documentation to specified path
	 * --project <path>       Set project path
	 *
	 * Example:
	 * --export-api C:\dev\SkoreEngine\Skore --project C:\dev\SkoreEngine\Projects\Sandbox
	 */

	class ArgParser
	{
	public:
		void Parse(i32 argc, char** argv)
		{
			if (argv == nullptr || argc <= 1) return;

			for (int i = 0; i < argc; ++i)
			{
				m_args.EmplaceBack(argv[i]);
			}

			usize i = 0;
			while (i < m_args.Size())
			{
				if (CheckArg(m_args[i]))
				{
					if (i + 1 < m_args.Size() && !CheckArg(m_args[i + 1]))
					{
						m_namedArgsWithValue.Emplace(FormatArg(m_args[i]), String{m_args[i + 1]});
						i++;
					}
					else
					{
						m_namedArgsWithoutValue.Insert(FormatArg(m_args[i]));
					}
				}
				i++;
			}
		}

		StringView Get(const StringView& name)
		{
			if (auto it = m_namedArgsWithValue.Find(name))
			{
				return it->second;
			}
			return {};
		}

		bool Has(const StringView& name)
		{
			return m_namedArgsWithValue.Has(name) || m_namedArgsWithoutValue.Has(name);
		}

		StringView Get(usize i)
		{
			if (i < m_args.Size())
			{
				return m_args[i];
			}
			return {};
		}

	private:
		Array<String>           m_args{};
		HashMap<String, String> m_namedArgsWithValue{};
		HashSet<String>         m_namedArgsWithoutValue{};

		static String FormatArg(const StringView& arg)
		{
			return arg.Substr(arg.FindFirstNotOf("-"));
		}

		static bool CheckArg(const StringView& arg)
		{
			return arg.StartsWith("-") || arg.StartsWith("--");
		}
	};
}
