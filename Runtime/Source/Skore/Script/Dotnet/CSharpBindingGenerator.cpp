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

		bool IsFullyBlittable(TypeID typeId, const HashMap<TypeID, GenType>& genTypes, HashMap<TypeID, i32>& cache)
		{
			if (auto it = cache.Find(typeId))
			{
				return it->second == 1;
			}

			auto git = genTypes.Find(typeId);
			if (!git || git->second.kind != CsKind::Struct)
			{
				return false;
			}

			cache[typeId] = 2;

			Span<ReflectField*> fields = git->second.type->GetFields();
			bool                result = !fields.Empty();
			for (usize i = 0; result && i < fields.Size(); ++i)
			{
				const FieldProps& fp = fields[i]->GetProps();
				if (fp.isPointer || fp.isReference)
				{
					result = false;
					break;
				}
				String prim{};
				if (MapPrimitive(fp.name, prim))
				{
					continue;
				}
				if (auto fit = genTypes.Find(fp.typeId))
				{
					if (fit->second.kind == CsKind::Enum)
					{
						continue;
					}
					if (fit->second.kind == CsKind::Struct && IsFullyBlittable(fp.typeId, genTypes, cache))
					{
						continue;
					}
				}
				result = false;
			}

			cache[typeId] = result ? 1 : 0;
			return result;
		}

		bool ResolveType(const FieldProps& props, const HashMap<TypeID, GenType>& genTypes, Resolved& out)
		{
			if (props.isPointer || props.isReference)
			{
				return false;
			}
			if (MapPrimitive(props.name, out.name))
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

		void EmitStruct(Writer& writer, const GenType& genType, const HashMap<TypeID, GenType>& genTypes)
		{
			String nsLine = "namespace ";
			nsLine += genType.ns;
			writer.Line(nsLine);
			writer.Line("{");
			writer.indent++;

			writer.Line("[StructLayout(LayoutKind.Sequential)]");
			String decl = "public struct ";
			decl += genType.simpleName;
			writer.Line(decl);
			writer.Line("{");
			writer.indent++;

			Array<String> usedNames{};
			usedNames.EmplaceBack(genType.simpleName);

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
		};

		struct FunctionEmit
		{
			u32           index = 0;
			String        name{};
			String        returnType{};
			bool          isStatic = false;
			bool          returnsVoid = false;
			Array<String> paramTypes{};
			Array<String> paramNames{};
		};

		void EmitClass(Writer&                          writer,
		               const GenType&                   genType,
		               const HashMap<TypeID, GenType>&  genTypes,
		               Array<String>&                   warnings)
		{
			ReflectType* type = genType.type;

			Array<String> usedNames{};
			usedNames.EmplaceBack(genType.simpleName);

			Array<FieldEmit>    fieldEmits{};
			Span<ReflectField*> fields = type->GetFields();
			for (usize i = 0; i < fields.Size(); ++i)
			{
				ReflectField* field = fields[i];
				Resolved      resolved{};
				if (ResolveType(field->GetProps(), genTypes, resolved))
				{
					String propName = Disambiguate(ToPascalCase(field->GetName()), usedNames);
					fieldEmits.EmplaceBack(FieldEmit{static_cast<u32>(i), resolved.name, propName});
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

			Array<FunctionEmit>    functionEmits{};
			Span<ReflectFunction*> functions = type->GetFunctions();
			for (usize i = 0; i < functions.Size(); ++i)
			{
				ReflectFunction* function = functions[i];
				FieldProps       returnProps = function->GetReturn();

				FunctionEmit emit{};
				emit.index = static_cast<u32>(i);
				emit.isStatic = function->IsStatic();
				emit.returnsVoid = returnProps.name == "void" && !returnProps.isPointer && !returnProps.isReference;

				bool ok = function->GetFunctionPointer() != nullptr;

				if (ok && !emit.returnsVoid)
				{
					Resolved resolved{};
					if (ResolveType(returnProps, genTypes, resolved))
					{
						emit.returnType = resolved.name;
					}
					else
					{
						ok = false;
					}
				}
				else
				{
					emit.returnType = "void";
				}

				Span<ReflectParam*> params = function->GetParams();
				for (usize p = 0; ok && p < params.Size(); ++p)
				{
					Resolved resolved{};
					if (ResolveType(params[p]->GetProps(), genTypes, resolved))
					{
						emit.paramTypes.EmplaceBack(resolved.name);
						String paramName = EscapeName(params[p]->GetName());
						if (paramName == "_")
						{
							paramName = "arg";
							paramName += I64ToStr(static_cast<i64>(p));
						}
						emit.paramNames.EmplaceBack(paramName);
					}
					else
					{
						ok = false;
					}
				}

				if (!ok)
				{
					String warn = genType.fullName;
					warn += ": skipped function '";
					warn += String{function->GetName()};
					warn += "' (unsupported signature)";
					warnings.EmplaceBack(warn);
					continue;
				}

				emit.name = Disambiguate(ToPascalCase(function->GetSimpleName()), usedNames);

				functionEmits.EmplaceBack(Traits::Move(emit));
			}

			String nsLine = "namespace ";
			nsLine += genType.ns;
			writer.Line(nsLine);
			writer.Line("{");
			writer.indent++;

			String classDecl = "public unsafe partial class ";
			classDecl += genType.simpleName;
			writer.Line(classDecl);
			writer.Line("{");
			writer.indent++;

			writer.Line("public IntPtr Handle;");
			writer.Blank();

			String ctor = "public ";
			ctor += genType.simpleName;
			ctor += "(IntPtr handle) { Handle = handle; }";
			writer.Line(ctor);
			writer.Blank();

			writer.Line("private static readonly IntPtr[] __fns;");
			writer.Line("private static readonly IntPtr[] __fps;");
			writer.Line("private static readonly IntPtr[] __flds;");
			writer.Blank();

			String staticCtor = "static ";
			staticCtor += genType.simpleName;
			staticCtor += "()";
			writer.Line(staticCtor);
			writer.Line("{");
			writer.indent++;
			String findType = "var __rt = Reflection.FindTypeByName(\"";
			findType += String{type->GetName()};
			findType += "\")!.Value;";
			writer.Line(findType);
			writer.Line("var __fn = __rt.GetFunctions();");
			writer.Line("__fns = new IntPtr[__fn.Length];");
			writer.Line("__fps = new IntPtr[__fn.Length];");
			writer.Line("for (int i = 0; i < __fn.Length; i++) { __fns[i] = __fn[i].Handle; __fps[i] = __fn[i].GetFunctionPointer(); }");
			writer.Line("var __fl = __rt.GetFields();");
			writer.Line("__flds = new IntPtr[__fl.Length];");
			writer.Line("for (int i = 0; i < __fl.Length; i++) __flds[i] = __fl[i].Handle;");
			writer.indent--;
			writer.Line("}");

			for (const FieldEmit& field : fieldEmits)
			{
				writer.Blank();
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

			for (const FunctionEmit& function : functionEmits)
			{
				writer.Blank();

				String signature = "public ";
				if (function.isStatic)
				{
					signature += "static ";
				}
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

				String cast = "var __fp = (";
				cast += DelegateSignature(function.paramTypes, function.returnType);
				cast += ")__fps[";
				cast += I64ToStr(static_cast<i64>(function.index));
				cast += "];";
				writer.Line(cast);

				String call{};
				if (!function.returnsVoid)
				{
					call += "return ";
				}
				call += "__fp(__fns[";
				call += I64ToStr(static_cast<i64>(function.index));
				call += "], ";
				call += function.isStatic ? "IntPtr.Zero" : "Handle";
				for (usize p = 0; p < function.paramNames.Size(); ++p)
				{
					call += ", ";
					call += function.paramNames[p];
				}
				call += ");";
				writer.Line(call);

				writer.indent--;
				writer.Line("}");
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

		HashMap<TypeID, i32> blitCache{};
		Array<TypeID>        demote{};
		for (const auto& entry : genTypes)
		{
			if (entry.second.kind == CsKind::Struct && !IsFullyBlittable(entry.second.typeId, genTypes, blitCache))
			{
				demote.EmplaceBack(entry.second.typeId);
			}
		}
		for (TypeID id : demote)
		{
			if (auto it = genTypes.Find(id))
			{
				it->second.kind = CsKind::Class;
			}
		}

		u32 fileCount = 0;
		for (const auto& entry : genTypes)
		{
			const GenType& genType = entry.second;

			Writer writer{};
			writer.Line("// <auto-generated/>");
			writer.Line("using System;");
			writer.Line("using System.Runtime.InteropServices;");
			writer.Blank();

			if (genType.kind == CsKind::Enum)
			{
				EmitEnum(writer, genType);
			}
			else if (genType.kind == CsKind::Struct)
			{
				EmitStruct(writer, genType, genTypes);
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
