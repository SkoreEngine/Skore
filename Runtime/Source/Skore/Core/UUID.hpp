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
#include "Math.hpp"
#include "String.hpp"
#include "StringView.hpp"
#include "Algorithm.hpp"

namespace Skore
{
	struct UUID
	{

		u64 firstValue;
		u64 secondValue;

		constexpr UUID() : firstValue(0), secondValue(0)
		{}

		constexpr UUID(u64 firstValue, u64 secondValue) : firstValue(firstValue), secondValue(secondValue)
		{}

		explicit operator bool() const noexcept
		{
			return this->firstValue != 0 || this->secondValue != 0;
		}

		static constexpr UUID FromName(const char* string)
		{
			u64 values[2] = {};
			usize len = 0;
			for (const char* it = string; *it; ++it)
			{
				++len;
			}
			MurmurHash3X64128(string, len, HashSeed32, values);
			return UUID{values[0], values[1]};
		}


		static UUID RandomUUID()
		{
			return {Random::Xorshift64star(), Random::Xorshift64star()};
		}

		static UUID FromString(const StringView& str)
		{
			if (str.Empty())
			{
				return UUID{};
			}
			auto uuid = UUID{};

			u32 count{};
			Split(str, StringView{"-"}, [&](const StringView& value)
			{
				switch (count)
				{
					case 0 :
					{
						uuid.firstValue = HexTo64(value);
						uuid.firstValue <<= 16;
						break;
					}
					case 1 :
					{
						uuid.firstValue |= HexTo64(value);
						uuid.firstValue <<= 16;
						break;
					}
					case 2 :
					{
						uuid.firstValue |= HexTo64(value);
						break;
					}
					case 3 :
					{
						uuid.secondValue = HexTo64(value);
						uuid.secondValue <<= 48;
						break;
					}
					case 4 :
					{
						uuid.secondValue |= HexTo64(value);
						break;
					}
					default:
					{
					}
				}
				count++;
			});
			return uuid;
		}

		String ToString() const
		{
			String result;
			result.Resize(36);

			constexpr static char  digits[]     = "0123456789abcdef";
			constexpr static i32   base         = 16;

			auto firstValueStr = firstValue;
			i32 i = 17;
			do
			{
				if (i != 8 && i != 13)
				{
					result[i] = digits[firstValueStr % base];
					firstValueStr = firstValueStr / base;
				}
				else
				{
					result[i] = '-';
				}
				i--;
			} while (i > -1);
			result[18] = '-';
			auto secondValueStr = this->secondValue;
			i = 35;
			do
			{
				if (i != 23)
				{
					result[i] = digits[secondValueStr % base];
					secondValueStr = secondValueStr / base;
				}
				else
				{
					result[i] = '-';
				}
				i--;
			} while (i > 18);

			return result;
		}

		bool operator==(const UUID& uuid) const
		{
			return this->firstValue == uuid.firstValue && this->secondValue == uuid.secondValue;
		}

		bool operator!=(const UUID& uuid) const
		{
			return !((*this) == uuid);
		}

		UUID& operator=(const UUID& uuid) = default;

	};

	template<>
	struct Hash<UUID>
	{
		constexpr static bool hasHash = true;
		constexpr static usize Value(const UUID& uuid)
		{
			auto result = (usize) (uuid.firstValue ^ (uuid.firstValue >> 32));
			result = 31 * result + (i32) (uuid.secondValue ^ (uuid.secondValue >> 32));
			return result;
		}
	};

//	template <>
//	struct ArchiveType<UUID>
//	{
//		constexpr static bool hasArchiveImpl = true;
//
//		static ArchiveValue ToValue(ArchiveWriter& writer, const UUID& value)
//		{
//			return writer.StringValue(value.ToString());
//		}
//
//		static void FromValue(ArchiveReader& reader, ArchiveValue archiveValue, UUID& typeValue)
//		{
//			typeValue = UUID::FromString(reader.StringValue(archiveValue));
//		}
//	};

}
