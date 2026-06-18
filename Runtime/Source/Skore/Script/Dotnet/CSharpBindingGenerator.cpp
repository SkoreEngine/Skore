#include "CSharpBindingGenerator.hpp"

#include <cstdio>

#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Traits.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

namespace Skore
{
	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::CSharpBindingGenerator");

		enum class CsKind
		{
			Class,
			Struct,
			Enum,
		};

		struct GenType
		{
			TypeID       typeId{};
			String       simpleName{};
			String       ns{};
			String       fullName{};
			String       scope{};
			CsKind       kind = CsKind::Class;
			ReflectType* type = nullptr;
		};

		struct Resolved
		{
			String name{};
		};

		bool IsIdentStart(char c)
		{
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
		}

		bool IsIdentChar(char c)
		{
			return IsIdentStart(c) || (c >= '0' && c <= '9');
		}

		bool IsValidIdentifier(StringView name)
		{
			if (name.Empty()) return false;
			if (!IsIdentStart(name[0])) return false;
			for (usize i = 1; i < name.Size(); ++i)
			{
				if (!IsIdentChar(name[i])) return false;
			}
			return true;
		}

		bool IsKeyword(StringView name)
		{
			static const char* keywords[] = {
				"abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked",
				"class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else",
				"enum", "event", "explicit", "extern", "false", "finally", "fixed", "float", "for",
				"foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock",
				"long", "namespace", "new", "null", "object", "operator", "out", "override", "params",
				"private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
				"sizeof", "stackalloc", "static", "string", "struct", "switch", "this", "throw", "true",
				"try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using", "virtual",
				"void", "volatile", "while",
			};
			for (const char* kw : keywords)
			{
				if (name == kw) return true;
			}
			return false;
		}

		String EscapeName(StringView name)
		{
			String result{};
			if (name.Empty())
			{
				result = "_";
				return result;
			}
			if (!IsIdentStart(name[0]))
			{
				result += "_";
			}
			for (usize i = 0; i < name.Size(); ++i)
			{
				char c = name[i];
				result += IsIdentChar(c) ? c : '_';
			}
			if (IsKeyword(result))
			{
				String escaped = "@";
				escaped += result;
				return escaped;
			}
			return result;
		}

		String NamespaceFromScope(StringView scope)
		{
			String ns = "Skore";
			if (scope.Empty()) return ns;
			usize start = 0;
			for (usize i = 0; i <= scope.Size(); ++i)
			{
				if (i == scope.Size() || scope[i] == '.')
				{
					if (i > start)
					{
						ns += ".";
						ns += String{scope.Substr(start, i - start)};
					}
					start = i + 1;
				}
			}
			return ns;
		}

		String I64ToStr(i64 value)
		{
			char tmp[32];
			snprintf(tmp, sizeof(tmp), "%lld", static_cast<long long>(value));
			return String{tmp};
		}

		String ToPascalCase(StringView name)
		{
			String result{};
			for (usize i = 0; i < name.Size(); ++i)
			{
				char c = name[i];
				result += IsIdentChar(c) ? c : '_';
			}
			if (result.Empty() || !IsIdentStart(result[0]))
			{
				String prefixed = "_";
				prefixed += result;
				result = prefixed;
			}
			usize first = 0;
			while (first < result.Size() && result[first] == '_')
			{
				++first;
			}
			if (first < result.Size() && result[first] >= 'a' && result[first] <= 'z')
			{
				result[first] = static_cast<char>(result[first] - 'a' + 'A');
			}
			if (IsKeyword(result))
			{
				String escaped = "@";
				escaped += result;
				return escaped;
			}
			return result;
		}

		String Disambiguate(const String& desired, Array<String>& used)
		{
			String candidate = desired;
			i32    suffix = 1;
			bool   collision = true;
			while (collision)
			{
				collision = false;
				for (const String& name : used)
				{
					if (name == candidate)
					{
						collision = true;
						break;
					}
				}
				if (collision)
				{
					candidate = desired;
					candidate += "_";
					candidate += I64ToStr(static_cast<i64>(suffix++));
				}
			}
			used.EmplaceBack(candidate);
			return candidate;
		}

		const char* EnumUnderlying(u32 size)
		{
			switch (size)
			{
				case 1: return "byte";
				case 2: return "short";
				case 8: return "long";
				default: return "int";
			}
		}

		bool IsDenied(StringView cppName)
		{
			static const char* denied[] = {
				"Skore::String",
				"Skore::StringView",
			};
			for (const char* entry : denied)
			{
				if (cppName == entry) return true;
			}
			return false;
		}

		bool MapPrimitive(StringView name, String& out)
		{
			struct Pair
			{
				const char* cpp;
				const char* cs;
			};
			static const Pair table[] = {
				{"bool", "bool"},
				{"char", "byte"},
				{"signed char", "sbyte"},
				{"unsigned char", "byte"},
				{"short", "short"}, {"short int", "short"}, {"signed short", "short"},
				{"unsigned short", "ushort"}, {"unsigned short int", "ushort"},
				{"int", "int"}, {"signed int", "int"},
				{"unsigned int", "uint"}, {"unsigned", "uint"},
				{"long", "int"}, {"long int", "int"}, {"signed long", "int"}, {"signed long int", "int"},
				{"unsigned long", "uint"}, {"unsigned long int", "uint"},
				{"long long", "long"}, {"signed long long", "long"}, {"__int64", "long"}, {"int64_t", "long"},
				{"unsigned long long", "ulong"}, {"unsigned __int64", "ulong"}, {"uint64_t", "ulong"},
				{"float", "float"}, {"double", "double"},
				{"int8_t", "sbyte"}, {"uint8_t", "byte"}, {"int16_t", "short"}, {"uint16_t", "ushort"},
				{"int32_t", "int"}, {"uint32_t", "uint"},
				{"wchar_t", "char"}, {"char16_t", "char"},
			};
			for (const Pair& pair : table)
			{
				if (name == pair.cpp)
				{
					out = pair.cs;
					return true;
				}
			}
			return false;
		}

		bool MapKnownType(StringView name, String& out)
		{
			if (name == "Skore::BasicStringView<char>")
			{
				out = "Skore.StringView";
				return true;
			}
			if (name == "Skore::TypeID")
			{
				out = "Skore.TypeId";
				return true;
			}
			return false;
		}

		bool IsStringView(const FieldProps& props)
		{
			return props.name == "Skore::BasicStringView<char>";
		}

		bool IsString(const FieldProps& props)
		{
			return props.name == "Skore::BasicString<char,18>";
		}

		String TrimSpaces(StringView s)
		{
			usize start = 0;
			usize end = s.Size();
			while (start < end && s[start] == ' ') ++start;
			while (end > start && s[end - 1] == ' ') --end;
			return String{s.Substr(start, end - start)};
		}

		StringView StripTypeKeyword(StringView s)
		{
			static const char* keywords[] = {"class ", "struct ", "enum "};
			for (const char* kw : keywords)
			{
				StringView k{kw};
				if (s.Size() >= k.Size())
				{
					bool match = true;
					for (usize i = 0; i < k.Size(); ++i)
					{
						if (s[i] != k[i])
						{
							match = false;
							break;
						}
					}
					if (match) return s.Substr(k.Size());
				}
			}
			return s;
		}

		struct SpanInfo
		{
			String csElement{};
			String abiElement{};
			bool   isObject = false;
		};

		bool ParseSpan(StringView name, const HashMap<TypeID, GenType>& genTypes, SpanInfo& out)
		{
			StringView prefix{"Skore::Span<"};
			if (name.Size() <= prefix.Size() + 1 || name[name.Size() - 1] != '>')
			{
				return false;
			}
			for (usize i = 0; i < prefix.Size(); ++i)
			{
				if (name[i] != prefix[i]) return false;
			}

			String inner = TrimSpaces(name.Substr(prefix.Size(), name.Size() - prefix.Size() - 1));
			bool   isPtr = false;
			if (!inner.Empty() && inner[inner.Size() - 1] == '*')
			{
				isPtr = true;
				inner = TrimSpaces(StringView{inner}.Substr(0, inner.Size() - 1));
			}
			inner = String{StripTypeKeyword(inner)};

			ReflectType* element = Reflection::FindTypeByName(inner);
			if (isPtr)
			{
				if (element)
				{
					if (auto it = genTypes.Find(element->GetProps().typeId); it && it->second.kind == CsKind::Class)
					{
						out.csElement = it->second.fullName;
						out.abiElement = "IntPtr";
						out.isObject = true;
						return true;
					}
				}
				return false;
			}

			String prim{};
			if (MapPrimitive(inner, prim))
			{
				out.csElement = prim;
				out.abiElement = prim;
				return true;
			}
			if (element)
			{
				if (auto it = genTypes.Find(element->GetProps().typeId); it && (it->second.kind == CsKind::Enum || it->second.kind == CsKind::Struct))
				{
					out.csElement = it->second.fullName;
					out.abiElement = it->second.fullName;
					return true;
				}
			}
			return false;
		}

		bool CanEmitNamedFields(const GenType& genType, const HashMap<TypeID, GenType>& genTypes)
		{
			Span<ReflectField*> fields = genType.type->GetFields();
			if (fields.Empty())
			{
				return false;
			}
			for (usize i = 0; i < fields.Size(); ++i)
			{
				const FieldProps& fp = fields[i]->GetProps();
				if (fp.isPointer || fp.isReference)
				{
					return false;
				}
				String prim{};
				if (MapPrimitive(fp.name, prim) || MapKnownType(fp.name, prim))
				{
					continue;
				}
				if (auto it = genTypes.Find(fp.typeId); it && (it->second.kind == CsKind::Enum || it->second.kind == CsKind::Struct))
				{
					continue;
				}
				return false;
			}
			return true;
		}

		bool ResolveValueName(const FieldProps& props, const HashMap<TypeID, GenType>& genTypes, Resolved& out)
		{
			if (MapPrimitive(props.name, out.name) || MapKnownType(props.name, out.name))
			{
				return true;
			}
			if (auto it = genTypes.Find(props.typeId))
			{
				if (it->second.kind == CsKind::Enum || it->second.kind == CsKind::Struct)
				{
					out.name = it->second.fullName;
					return true;
				}
			}
			return false;
		}

		bool ResolveType(const FieldProps& props, const HashMap<TypeID, GenType>& genTypes, Resolved& out)
		{
			if (props.isPointer || props.isReference)
			{
				return false;
			}
			return ResolveValueName(props, genTypes, out);
		}

		String TypeDetail(const FieldProps& props)
		{
			String detail{};
			if (props.isConst)
			{
				detail += "const ";
			}
			detail += String{props.name};
			if (props.isPointer)
			{
				detail += "*";
			}
			if (props.isReference)
			{
				detail += "&";
			}
			return detail;
		}

		bool ResolveClassName(const FieldProps& props, const HashMap<TypeID, GenType>& genTypes, String& out)
		{
			if (auto it = genTypes.Find(props.typeId); it && it->second.kind == CsKind::Class)
			{
				out = it->second.fullName;
				return true;
			}
			return false;
		}

		bool HasGeneratedBase(ReflectType* type, const HashMap<TypeID, GenType>& genTypes)
		{
			Span<TypeID> bases = type->GetBaseTypes();
			if (bases.Empty()) return false;
			auto it = genTypes.Find(bases[0]);
			return it && it->second.kind == CsKind::Class;
		}

		struct Writer
		{
			String buf{};
			i32    indent = 0;

			void Tabs()
			{
				for (i32 i = 0; i < indent; ++i)
				{
					buf += "    ";
				}
			}

			void Line(const String& line)
			{
				Tabs();
				buf += line;
				buf += "\n";
			}

			void Line(const char* line)
			{
				Tabs();
				buf += line;
				buf += "\n";
			}

			void Blank()
			{
				buf += "\n";
			}
		};

		String EnsureScopeDir(StringView outputDir, StringView scope)
		{
			String dir = outputDir;
			FileSystem::CreateDirectory(dir);
			if (scope.Empty()) return dir;

			usize start = 0;
			for (usize i = 0; i <= scope.Size(); ++i)
			{
				if (i == scope.Size() || scope[i] == '.')
				{
					if (i > start)
					{
						dir = Path::Join(dir, scope.Substr(start, i - start));
						FileSystem::CreateDirectory(dir);
					}
					start = i + 1;
				}
			}
			return dir;
		}

		String DelegateSignature(const Array<String>& paramTypes, const String& returnType)
		{
			String sig = "delegate* unmanaged[Cdecl]<IntPtr, IntPtr";
			for (const String& paramType : paramTypes)
			{
				sig += ", ";
				sig += paramType;
			}
			sig += ", ";
			sig += returnType;
			sig += ">";
			return sig;
		}

		void EmitEnum(Writer& writer, const GenType& genType)
		{
			String nsLine = "namespace ";
			nsLine += genType.ns;
			writer.Line(nsLine);
			writer.Line("{");
			writer.indent++;

			String decl = "public enum ";
			decl += genType.simpleName;
			decl += " : ";
			decl += EnumUnderlying(genType.type->GetProps().size);
			writer.Line(decl);
			writer.Line("{");
			writer.indent++;

			Array<String>       usedNames{};
			Span<ReflectValue*> values = genType.type->GetValues();
			for (usize i = 0; i < values.Size(); ++i)
			{
				ReflectValue* value = values[i];
				String        entry = Disambiguate(ToPascalCase(value->GetDesc()), usedNames);
				entry += " = ";
				entry += I64ToStr(value->GetCode());
				entry += ",";
				writer.Line(entry);
			}

			writer.indent--;
			writer.Line("}");
			writer.indent--;
			writer.Line("}");
		}

		struct FieldEmit
		{
			u32    index = 0;
			String csType{};
			String name{};
			String objClass{};
			bool   isString = false;
		};

		struct FunctionEmit
		{
			u32           index = 0;
			String        name{};
			String        returnType{};
			String        returnDelegateType{};
			String        returnPrefix{};
			String        returnSuffix{};
			Array<String> returnLines{};
			bool          isStatic = false;
			String        selfType{};
			Array<String> paramTypes{};
			Array<String> paramDelegateTypes{};
			Array<String> paramNames{};
			Array<String> paramArgs{};
			Array<String> prologue{};
		};

		Array<FunctionEmit> CollectFunctions(ReflectType*                    type,
		                                     usize                           startIndex,
		                                     bool                            valueType,
		                                     const GenType&                  genType,
		                                     const HashMap<TypeID, GenType>& genTypes,
		                                     Array<String>&                  usedNames,
		                                     Array<String>&                  warnings)
		{
			Array<FunctionEmit>    result{};
			Span<ReflectFunction*> functions = type->GetFunctions();
			for (usize i = startIndex; i < functions.Size(); ++i)
			{
				ReflectFunction* function = functions[i];

				FieldProps returnProps = function->GetReturn();

				FunctionEmit emit{};
				emit.index = static_cast<u32>(i);
				emit.isStatic = function->IsStatic();
				if (valueType && !emit.isStatic)
				{
					emit.selfType = genType.fullName;
				}

				bool   ok = true;
				String reason{};

				if (function->GetFunctionPointer() == nullptr)
				{
					ok = false;
					reason = "no function pointer";
				}

				bool returnsVoid = returnProps.name == "void" && !returnProps.isPointer && !returnProps.isReference;
				if (ok && returnsVoid)
				{
					emit.returnType = "void";
					emit.returnDelegateType = "void";
				}
				else if (ok)
				{
					Resolved resolved{};
					String   className{};
					SpanInfo span{};
					if (IsStringView(returnProps))
					{
						emit.returnType = "string";
						if (returnProps.isPointer || returnProps.isReference)
						{
							emit.returnDelegateType = "Skore.StringView*";
							emit.returnLines.EmplaceBack(String{"return __ret->ToString();"});
						}
						else
						{
							emit.returnDelegateType = "Skore.StringView";
							emit.returnLines.EmplaceBack(String{"return __ret.ToString();"});
						}
					}
					else if (!returnProps.isPointer && !returnProps.isReference && ParseSpan(returnProps.name, genTypes, span))
					{
						emit.returnDelegateType = "Skore.Span<";
						emit.returnDelegateType += span.abiElement;
						emit.returnDelegateType += ">";

						String elementType = span.csElement;
						if (span.isObject)
						{
							elementType += "?";
						}
						emit.returnType = "List<";
						emit.returnType += elementType;
						emit.returnType += ">";

						String l1 = "var __list = new List<";
						l1 += elementType;
						l1 += ">((int)__ret.Size);";
						emit.returnLines.EmplaceBack(l1);
						String l2{};
						if (span.isObject)
						{
							l2 = "for (ulong __i = 0; __i < __ret.Size; __i++) { var __e = __ret[__i]; __list.Add(__e == IntPtr.Zero ? null : new ";
							l2 += span.csElement;
							l2 += "(__e)); }";
						}
						else
						{
							l2 = "for (ulong __i = 0; __i < __ret.Size; __i++) __list.Add(__ret[__i]);";
						}
						emit.returnLines.EmplaceBack(l2);
						emit.returnLines.EmplaceBack(String{"return __list;"});
					}
					else if (ResolveValueName(returnProps, genTypes, resolved))
					{
						emit.returnType = resolved.name;
						emit.returnDelegateType = resolved.name;
						if (returnProps.isPointer || returnProps.isReference)
						{
							emit.returnDelegateType += "*";
							emit.returnPrefix = "return *";
						}
						else
						{
							emit.returnPrefix = "return ";
						}
					}
					else if ((returnProps.isPointer || returnProps.isReference) && ResolveClassName(returnProps, genTypes, className))
					{
						emit.returnType = className;
						emit.returnType += "?";
						emit.returnDelegateType = "IntPtr";
						String l = "return __ret == IntPtr.Zero ? null : new ";
						l += className;
						l += "(__ret);";
						emit.returnLines.EmplaceBack(l);
					}
					else if (ResolveClassName(returnProps, genTypes, className) && !HasGeneratedBase(genTypes.Find(returnProps.typeId)->second.type, genTypes))
					{
						ReflectType* retType = genTypes.Find(returnProps.typeId)->second.type;
						emit.returnType = className;
						emit.returnDelegateType = className;
						emit.returnDelegateType += ".__Storage";

						String l1 = "IntPtr __buf = System.Runtime.InteropServices.Marshal.AllocHGlobal(sizeof(";
						l1 += className;
						l1 += ".__Storage));";
						emit.returnLines.EmplaceBack(l1);
						String l2 = "*(";
						l2 += className;
						l2 += ".__Storage*)__buf = __ret;";
						emit.returnLines.EmplaceBack(l2);
						String l3 = "return new ";
						l3 += className;
						l3 += "(__buf, Reflection.FindTypeByName(\"";
						l3 += String{retType->GetName()};
						l3 += "\")!.Value.Handle);";
						emit.returnLines.EmplaceBack(l3);
					}
					else
					{
						ok = false;
						reason = "return type '";
						reason += TypeDetail(returnProps);
						reason += "'";
					}
				}

				Span<ReflectParam*> params = function->GetParams();
				for (usize p = 0; ok && p < params.Size(); ++p)
				{
					const FieldProps& paramProps = params[p]->GetProps();
					String            paramName = EscapeName(params[p]->GetName());
					if (paramName == "_")
					{
						paramName = "arg";
						paramName += I64ToStr(static_cast<i64>(p));
					}

					Resolved resolved{};
					String   className{};
					SpanInfo span{};
					if (IsStringView(paramProps))
					{
						String sv = "__sv";
						sv += I64ToStr(static_cast<i64>(p));
						String l1 = "int ";
						l1 += sv;
						l1 += "_len = System.Text.Encoding.UTF8.GetByteCount(";
						l1 += paramName;
						l1 += ");";
						emit.prologue.EmplaceBack(l1);
						String l2 = "byte* ";
						l2 += sv;
						l2 += "_b = stackalloc byte[";
						l2 += sv;
						l2 += "_len];";
						emit.prologue.EmplaceBack(l2);
						String l3 = "System.Text.Encoding.UTF8.GetBytes(";
						l3 += paramName;
						l3 += ", new System.Span<byte>(";
						l3 += sv;
						l3 += "_b, ";
						l3 += sv;
						l3 += "_len));";
						emit.prologue.EmplaceBack(l3);
						String l4 = "var ";
						l4 += sv;
						l4 += " = new Skore.StringView(";
						l4 += sv;
						l4 += "_b, (ulong)";
						l4 += sv;
						l4 += "_len);";
						emit.prologue.EmplaceBack(l4);

						emit.paramTypes.EmplaceBack("string");
						emit.paramNames.EmplaceBack(paramName);
						if (paramProps.isPointer || paramProps.isReference)
						{
							emit.paramDelegateTypes.EmplaceBack("Skore.StringView*");
							String arg = "&";
							arg += sv;
							emit.paramArgs.EmplaceBack(arg);
						}
						else
						{
							emit.paramDelegateTypes.EmplaceBack("Skore.StringView");
							emit.paramArgs.EmplaceBack(sv);
						}
					}
					else if (ParseSpan(paramProps.name, genTypes, span))
					{
						String sp = "__sp";
						sp += I64ToStr(static_cast<i64>(p));
						String c = "int ";
						c += sp;
						c += "_count = ";
						c += paramName;
						c += ".Count;";
						emit.prologue.EmplaceBack(c);
						String b = span.abiElement;
						b += "* ";
						b += sp;
						b += "_b = stackalloc ";
						b += span.abiElement;
						b += "[";
						b += sp;
						b += "_count];";
						emit.prologue.EmplaceBack(b);
						String f = "for (int ";
						f += sp;
						f += "_i = 0; ";
						f += sp;
						f += "_i < ";
						f += sp;
						f += "_count; ";
						f += sp;
						f += "_i++) ";
						f += sp;
						f += "_b[";
						f += sp;
						f += "_i] = ";
						f += paramName;
						f += "[";
						f += sp;
						f += "_i]";
						if (span.isObject)
						{
							f += "?.Handle ?? IntPtr.Zero";
						}
						f += ";";
						emit.prologue.EmplaceBack(f);
						String mk = "var ";
						mk += sp;
						mk += " = new Skore.Span<";
						mk += span.abiElement;
						mk += ">(";
						mk += sp;
						mk += "_b, (ulong)";
						mk += sp;
						mk += "_count);";
						emit.prologue.EmplaceBack(mk);

						String mtype = "List<";
						mtype += span.csElement;
						if (span.isObject)
						{
							mtype += "?";
						}
						mtype += ">";
						emit.paramTypes.EmplaceBack(mtype);
						emit.paramNames.EmplaceBack(paramName);
						String spanType = "Skore.Span<";
						spanType += span.abiElement;
						spanType += ">";
						if (paramProps.isPointer || paramProps.isReference)
						{
							spanType += "*";
							emit.paramDelegateTypes.EmplaceBack(spanType);
							String arg = "&";
							arg += sp;
							emit.paramArgs.EmplaceBack(arg);
						}
						else
						{
							emit.paramDelegateTypes.EmplaceBack(spanType);
							emit.paramArgs.EmplaceBack(sp);
						}
					}
					else if (ResolveValueName(paramProps, genTypes, resolved))
					{
						emit.paramTypes.EmplaceBack(resolved.name);
						emit.paramNames.EmplaceBack(paramName);
						if (paramProps.isPointer || paramProps.isReference)
						{
							String delegateType = resolved.name;
							delegateType += "*";
							emit.paramDelegateTypes.EmplaceBack(delegateType);
							String arg = "&";
							arg += paramName;
							emit.paramArgs.EmplaceBack(arg);
						}
						else
						{
							emit.paramDelegateTypes.EmplaceBack(resolved.name);
							emit.paramArgs.EmplaceBack(paramName);
						}
					}
					else if ((paramProps.isPointer || paramProps.isReference) && ResolveClassName(paramProps, genTypes, className))
					{
						emit.paramTypes.EmplaceBack(className);
						emit.paramNames.EmplaceBack(paramName);
						emit.paramDelegateTypes.EmplaceBack("IntPtr");
						String arg = paramName;
						arg += "?.Handle ?? IntPtr.Zero";
						emit.paramArgs.EmplaceBack(arg);
					}
					else
					{
						ok = false;
						reason = "param '";
						reason += String{params[p]->GetName()};
						reason += "' type '";
						reason += TypeDetail(paramProps);
						reason += "'";
					}
				}

				if (!ok)
				{
					String warn = genType.fullName;
					warn += ".";
					warn += String{function->GetSimpleName()};
					warn += ": ";
					warn += reason;
					warnings.EmplaceBack(warn);
					continue;
				}

				emit.name = Disambiguate(ToPascalCase(function->GetSimpleName()), usedNames);
				result.EmplaceBack(Traits::Move(emit));
			}
			return result;
		}

		void EmitStaticCache(Writer& writer, const GenType& genType, bool cacheFields)
		{
			writer.Line("private static readonly IntPtr[] __fns;");
			writer.Line("private static readonly IntPtr[] __fps;");
			if (cacheFields)
			{
				writer.Line("private static readonly IntPtr[] __flds;");
			}
			writer.Blank();

			String staticCtor = "static ";
			staticCtor += genType.simpleName;
			staticCtor += "()";
			writer.Line(staticCtor);
			writer.Line("{");
			writer.indent++;
			String findType = "var __rt = Reflection.FindTypeByName(\"";
			findType += String{genType.type->GetName()};
			findType += "\")!.Value;";
			writer.Line(findType);
			writer.Line("var __fn = __rt.GetFunctions();");
			writer.Line("__fns = new IntPtr[__fn.Length];");
			writer.Line("__fps = new IntPtr[__fn.Length];");
			writer.Line("for (int i = 0; i < __fn.Length; i++) { __fns[i] = __fn[i].Handle; __fps[i] = __fn[i].GetFunctionPointer(); }");
			if (cacheFields)
			{
				writer.Line("var __fl = __rt.GetFields();");
				writer.Line("__flds = new IntPtr[__fl.Length];");
				writer.Line("for (int i = 0; i < __fl.Length; i++) __flds[i] = __fl[i].Handle;");
			}
			writer.indent--;
			writer.Line("}");
		}

		void EmitMethod(Writer& writer, const FunctionEmit& function)
		{
			writer.Blank();

			String signature = "public ";
			if (function.isStatic)
			{
				signature += "static ";
			}
			signature += "unsafe ";
			signature += function.returnType;
			signature += " ";
			signature += function.name;
			signature += "(";
			for (usize p = 0; p < function.paramTypes.Size(); ++p)
			{
				if (p > 0)
				{
					signature += ", ";
				}
				signature += function.paramTypes[p];
				signature += " ";
				signature += function.paramNames[p];
			}
			signature += ")";
			writer.Line(signature);
			writer.Line("{");
			writer.indent++;

			bool valueInstance = !function.selfType.Empty();
			if (valueInstance)
			{
				String fx = "fixed (";
				fx += function.selfType;
				fx += "* __self = &this)";
				writer.Line(fx);
				writer.Line("{");
				writer.indent++;
			}

			String cast = "var __fp = (";
			cast += DelegateSignature(function.paramDelegateTypes, function.returnDelegateType);
			cast += ")__fps[";
			cast += I64ToStr(static_cast<i64>(function.index));
			cast += "];";
			writer.Line(cast);

			for (const String& line : function.prologue)
			{
				writer.Line(line);
			}

			String callExpr = "__fp(__fns[";
			callExpr += I64ToStr(static_cast<i64>(function.index));
			callExpr += "], ";
			callExpr += valueInstance ? "(IntPtr)__self" : (function.isStatic ? "IntPtr.Zero" : "Handle");
			for (usize p = 0; p < function.paramArgs.Size(); ++p)
			{
				callExpr += ", ";
				callExpr += function.paramArgs[p];
			}
			callExpr += ")";

			if (!function.returnLines.Empty())
			{
				String tmp = "var __ret = ";
				tmp += callExpr;
				tmp += ";";
				writer.Line(tmp);
				for (const String& line : function.returnLines)
				{
					writer.Line(line);
				}
			}
			else
			{
				String call = function.returnPrefix;
				call += callExpr;
				call += function.returnSuffix;
				call += ";";
				writer.Line(call);
			}

			if (valueInstance)
			{
				writer.indent--;
				writer.Line("}");
			}

			writer.indent--;
			writer.Line("}");
		}

		void EmitStruct(Writer& writer, const GenType& genType, const HashMap<TypeID, GenType>& genTypes, Array<String>& warnings)
		{
			Array<String> usedNames{};
			usedNames.EmplaceBack(genType.simpleName);

			Array<FunctionEmit> functionEmits = CollectFunctions(genType.type, 0, true, genType, genTypes, usedNames, warnings);

			bool named = CanEmitNamedFields(genType, genTypes);

			String nsLine = "namespace ";
			nsLine += genType.ns;
			writer.Line(nsLine);
			writer.Line("{");
			writer.indent++;

			writer.Line("[StructLayout(LayoutKind.Sequential)]");
			String decl = named ? "public partial struct " : "public unsafe partial struct ";
			decl += genType.simpleName;
			writer.Line(decl);
			writer.Line("{");
			writer.indent++;

			if (named)
			{
				Span<ReflectField*> fields = genType.type->GetFields();
				for (usize i = 0; i < fields.Size(); ++i)
				{
					Resolved resolved{};
					ResolveType(fields[i]->GetProps(), genTypes, resolved);
					String line = "public ";
					line += resolved.name;
					line += " ";
					line += Disambiguate(ToPascalCase(fields[i]->GetName()), usedNames);
					line += ";";
					writer.Line(line);
				}
			}
			else
			{
				String buf = "private fixed byte _data[";
				buf += I64ToStr(static_cast<i64>(genType.type->GetProps().size));
				buf += "];";
				writer.Line(buf);
			}

			if (!functionEmits.Empty())
			{
				writer.Blank();
				EmitStaticCache(writer, genType, false);
				for (const FunctionEmit& function : functionEmits)
				{
					EmitMethod(writer, function);
				}
			}

			writer.indent--;
			writer.Line("}");
			writer.indent--;
			writer.Line("}");
		}

		void EmitClass(Writer&                          writer,
		               const GenType&                   genType,
		               const HashMap<TypeID, GenType>&  genTypes,
		               Array<String>&                   warnings)
		{
			ReflectType* type = genType.type;

			bool         hasBase = false;
			String       baseFullName{};
			usize        baseFieldCount = 0;
			usize        baseFuncCount = 0;
			Span<TypeID> baseTypes = type->GetBaseTypes();
			if (!baseTypes.Empty())
			{
				if (auto it = genTypes.Find(baseTypes[0]); it && it->second.kind == CsKind::Class)
				{
					hasBase = true;
					baseFullName = it->second.fullName;
					baseFieldCount = it->second.type->GetFields().Size();
					baseFuncCount = it->second.type->GetFunctions().Size();
				}
			}

			Array<String> usedNames{};
			usedNames.EmplaceBack(genType.simpleName);
			usedNames.EmplaceBack(String{"Handle"});
			usedNames.EmplaceBack(String{"Dispose"});

			Array<FieldEmit>    fieldEmits{};
			Span<ReflectField*> fields = type->GetFields();
			for (usize i = baseFieldCount; i < fields.Size(); ++i)
			{
				ReflectField*     field = fields[i];
				const FieldProps& fp = field->GetProps();
				Resolved          resolved{};
				String            className{};
				if (ResolveType(fp, genTypes, resolved))
				{
					String propName = Disambiguate(ToPascalCase(field->GetName()), usedNames);
					fieldEmits.EmplaceBack(FieldEmit{static_cast<u32>(i), resolved.name, propName, String{}});
				}
				else if ((fp.isPointer || fp.isReference) && ResolveClassName(fp, genTypes, className))
				{
					String propName = Disambiguate(ToPascalCase(field->GetName()), usedNames);
					fieldEmits.EmplaceBack(FieldEmit{static_cast<u32>(i), className, propName, className});
				}
				else if (IsString(fp))
				{
					String propName = Disambiguate(ToPascalCase(field->GetName()), usedNames);
					fieldEmits.EmplaceBack(FieldEmit{static_cast<u32>(i), String{}, propName, String{}, true});
				}
				else
				{
					String warn = genType.fullName;
					warn += ": skipped field '";
					warn += String{field->GetName()};
					warn += "' (type '";
					warn += String{field->GetProps().name};
					warn += "')";
					warnings.EmplaceBack(warn);
				}
			}

			Array<FunctionEmit> functionEmits = CollectFunctions(type, baseFuncCount, false, genType, genTypes, usedNames, warnings);

			String nsLine = "namespace ";
			nsLine += genType.ns;
			writer.Line(nsLine);
			writer.Line("{");
			writer.indent++;

			String classDecl = "public partial class ";
			classDecl += genType.simpleName;
			if (hasBase)
			{
				classDecl += " : ";
				classDecl += baseFullName;
			}
			else
			{
				classDecl += " : IDisposable";
			}
			writer.Line(classDecl);
			writer.Line("{");
			writer.indent++;

			if (!hasBase)
			{
				writer.Line("public IntPtr Handle;");
				writer.Line("private IntPtr __owned;");
				writer.Blank();

				String storage = "internal unsafe struct __Storage { private fixed byte _data[";
				storage += I64ToStr(static_cast<i64>(type->GetProps().size));
				storage += "]; }";
				writer.Line(storage);
				writer.Blank();
			}

			String ctor = "public ";
			ctor += genType.simpleName;
			ctor += "(IntPtr handle)";
			ctor += hasBase ? " : base(handle) { }" : " { Handle = handle; }";
			writer.Line(ctor);

			if (!hasBase)
			{
				String ownedCtor = "internal ";
				ownedCtor += genType.simpleName;
				ownedCtor += "(IntPtr handle, IntPtr ownedType) { Handle = handle; __owned = ownedType; }";
				writer.Line(ownedCtor);
				writer.Blank();
				writer.Line("public void Dispose() { if (__owned != IntPtr.Zero) { new ReflectType(__owned).Destructor(Handle); System.Runtime.InteropServices.Marshal.FreeHGlobal(Handle); __owned = IntPtr.Zero; } }");
			}
			writer.Blank();

			EmitStaticCache(writer, genType, true);

			for (const FieldEmit& field : fieldEmits)
			{
				writer.Blank();
				if (field.isString)
				{
					String prop = "public string ";
					prop += field.name;
					prop += " => new ReflectField(__flds[";
					prop += I64ToStr(static_cast<i64>(field.index));
					prop += "]).Get<Skore.NativeString>(Handle).ToString();";
					writer.Line(prop);
				}
				else if (!field.objClass.Empty())
				{
					String prop = "public ";
					prop += field.objClass;
					prop += "? ";
					prop += field.name;
					writer.Line(prop);
					writer.Line("{");
					writer.indent++;
					String getter = "get { var __p = new ReflectField(__flds[";
					getter += I64ToStr(static_cast<i64>(field.index));
					getter += "]).Get<IntPtr>(Handle); return __p == IntPtr.Zero ? null : new ";
					getter += field.objClass;
					getter += "(__p); }";
					writer.Line(getter);
					String setter = "set => new ReflectField(__flds[";
					setter += I64ToStr(static_cast<i64>(field.index));
					setter += "]).Set(Handle, value?.Handle ?? IntPtr.Zero);";
					writer.Line(setter);
					writer.indent--;
					writer.Line("}");
				}
				else
				{
					String prop = "public ";
					prop += field.csType;
					prop += " ";
					prop += field.name;
					writer.Line(prop);
					writer.Line("{");
					writer.indent++;
					String getter = "get => new ReflectField(__flds[";
					getter += I64ToStr(static_cast<i64>(field.index));
					getter += "]).Get<";
					getter += field.csType;
					getter += ">(Handle);";
					writer.Line(getter);
					String setter = "set => new ReflectField(__flds[";
					setter += I64ToStr(static_cast<i64>(field.index));
					setter += "]).Set(Handle, value);";
					writer.Line(setter);
					writer.indent--;
					writer.Line("}");
				}
			}

			for (const FunctionEmit& function : functionEmits)
			{
				EmitMethod(writer, function);
			}

			writer.indent--;
			writer.Line("}");
			writer.indent--;
			writer.Line("}");
		}
	}

	void CSharpBindingGenerator::Generate(StringView outputDir)
	{
		FileSystem::Remove(String{outputDir}, false);

		HashMap<TypeID, GenType> genTypes{};
		Array<ReflectType*>      allTypes = Reflection::GetAllTypes();
		Array<String>            warnings{};
		Array<String>            skippedTypes{};

		for (ReflectType* type : allTypes)
		{
			if (IsDenied(type->GetName()))
			{
				continue;
			}

			StringView simpleName = type->GetSimpleName();
			if (!IsValidIdentifier(simpleName))
			{
				skippedTypes.EmplaceBack(String{type->GetName()});
				continue;
			}

			if (type->GetFields().Empty() && type->GetFunctions().Empty() && type->GetValues().Empty())
			{
				continue;
			}

			GenType genType{};
			genType.typeId = type->GetProps().typeId;
			genType.type = type;
			genType.scope = type->GetScope();
			genType.simpleName = EscapeName(simpleName);
			genType.ns = NamespaceFromScope(genType.scope);
			genType.fullName = genType.ns;
			genType.fullName += ".";
			genType.fullName += genType.simpleName;

			const TypeProps& props = type->GetProps();
			genType.kind = props.isEnum ? CsKind::Enum : (props.isTriviallyCopyable ? CsKind::Struct : CsKind::Class);

			genTypes.Insert(genType.typeId, genType);
		}

		u32 fileCount = 0;
		for (const auto& entry : genTypes)
		{
			const GenType& genType = entry.second;

			Writer writer{};
			writer.Line("// <auto-generated/>");
			writer.Line("#nullable enable");
			writer.Line("using System;");
			writer.Line("using System.Collections.Generic;");
			writer.Line("using System.Runtime.InteropServices;");
			writer.Blank();

			if (genType.kind == CsKind::Enum)
			{
				EmitEnum(writer, genType);
			}
			else if (genType.kind == CsKind::Struct)
			{
				EmitStruct(writer, genType, genTypes, warnings);
			}
			else
			{
				EmitClass(writer, genType, genTypes, warnings);
			}

			String dir = EnsureScopeDir(outputDir, genType.scope);
			String fileName = genType.simpleName;
			fileName += ".gen.cs";
			String filePath = Path::Join(dir, fileName);
			FileSystem::SaveFileAsString(filePath, writer.buf);
			fileCount++;
		}

		String report{};
		report += "Generated ";
		report += I64ToStr(static_cast<i64>(fileCount));
		report += " files.\n\nSkipped types (invalid identifier):\n";
		for (const String& skipped : skippedTypes)
		{
			report += "  ";
			report += skipped;
			report += "\n";
		}
		report += "\nSkipped members (unsupported signature):\n";
		for (const String& warning : warnings)
		{
			report += "  ";
			report += warning;
			report += "\n";
		}
		FileSystem::SaveFileAsString(Path::Join(outputDir, "_BindingsReport.txt"), report);

		logger.Info("generated {} C# binding files to '{}'", fileCount, outputDir);
		logger.Info("skipped {} types, {} members (see _BindingsReport.txt)", skippedTypes.Size(), warnings.Size());
	}
}
