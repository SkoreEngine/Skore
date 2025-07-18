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

#include "pocketpy.h"
#include "PkPyScriptingEngine.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	static py_GlobalRef skoreModule;
	static Logger& logger = Logger::GetLogger("Skore::PkPyScriptingEngine");

	static HashMap<TypeID, py_Type> types;

	void PkPyScriptResourceChange(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		PkPyScriptingEngine::Execute(newValue.GetString(PkPyScriptResource::Source), newValue.GetString(PkPyScriptResource::Name));
	}

	void PkPyScriptingEngine::Execute(StringView file, StringView name)
	{
		bool ok = py_exec(file.CStr(), name.CStr(), EXEC_MODE, NULL);
		if (!ok)
		{
			logger.Error("Error executing script: {}", py_formatexc());
			return;
		}

		logger.Debug("Script {} executed successfully", name);
	}

	struct FunctionUserData
	{
		ReflectFunction* func;
		ReflectType* retType;
	};

	struct PointerRef
	{
		VoidPtr pointer;
	};

	static bool CallFunction(int argc, py_Ref argv)
	{
		auto func = py_inspect_currentfunction();
		FunctionUserData* userData = static_cast<FunctionUserData*>(py_touserdata(py_getslot(func, 0)));

		bool staticFunc = userData->func->IsStatic();
		Span<ReflectParam*> params = userData->func->GetParams();

		if (argc - (staticFunc ? 0 : 1) != params.Size())
		{
			py_exception(tp_RuntimeError, "Wrong number of arguments for function");
			return false;
		}

		FieldProps retInfo = userData->func->GetReturn();

		struct InvokeCallbackUsetData
		{
			bool staticFunc;
			py_Ref argv;
		};

		InvokeCallbackUsetData invokeUserData = {};
		invokeUserData.staticFunc = staticFunc;
		invokeUserData.argv = argv;

		VoidPtr instance = nullptr;
		if (!staticFunc)
		{
			PointerRef& ref = *static_cast<PointerRef*>(py_touserdata(py_arg(0)));
			instance = ref.pointer;
		}

		//TODO - what if larger?
		char retBuffer[128] = {};
		if (userData->retType && !retInfo.isPointer && !retInfo.isReference && retInfo.typeId != TypeInfo<void>::ID())
		{
		//	userData->retType->GetDefaultConstructor()->Construct(retBuffer, nullptr);
		}

		userData->func->InvokeCallback(instance, retBuffer, [](VoidPtr param, usize index, const FieldProps& props, VoidPtr userData)
		{
			InvokeCallbackUsetData* invokeUserData = static_cast<InvokeCallbackUsetData*>(userData);

			if (!invokeUserData->staticFunc)
			{
				index = index + 1;
			}

			py_Ref argv = invokeUserData->argv;

			if (props.isEnum)
			{
				i64 value = py_toint(py_arg(index));

				if (props.size == sizeof(i8))
				{
					i8 vlToCopy = static_cast<i8>(value);
					memcpy(param, &vlToCopy, sizeof(i64));
				}
				else if (props.size == sizeof(i16))
				{
					i16 vlToCopy = static_cast<i16>(value);
					memcpy(param, &vlToCopy, sizeof(i64));
				}
				else if (props.size == sizeof(i32))
				{
					i32 vlToCopy = static_cast<i32>(value);
					memcpy(param, &vlToCopy, sizeof(i64));
				}
				else if (props.size == sizeof(i64))
				{
					memcpy(param, &value, sizeof(i64));
				}
			}

			if (props.typeId == TypeInfo<StringView>::ID())
			{
				i32 size = 0;
				auto str = py_tostrn(py_arg(index), &size);
				new (PlaceHolder(), param) StringView(str, size);
			}
		},
		&invokeUserData);

		if (retInfo.typeId != TypeInfo<void>::ID())
		{
			if (auto it = types.Find(retInfo.typeId))
			{
				if (retInfo.isPointer || retInfo.isReference)
				{
					PointerRef& ref = *static_cast<PointerRef*>(py_newobject(py_retval(), it->second, 0, sizeof(PointerRef)));
					memcpy(&ref.pointer, retBuffer, retInfo.size);
					return true;
				}
			}
		}

		py_newnone(py_retval());
		return true;
	}

	void PkPyScriptingEngineInit()
	{
		py_initialize();

		Resources::FindType<PkPyScriptResource>()->RegisterEvent(PkPyScriptResourceChange, nullptr);
		skoreModule = py_newmodule("skore");

		for (ReflectType* type : Reflection::GetAllTypes())
		{
			Span<ReflectFunction*> funcs = type->GetFunctions();
			if (!funcs.Empty())
			{
				py_Type pkPyType = py_newtype(type->GetSimpleName().CStr(), tp_object, skoreModule, nullptr);
				types.Insert(type->GetProps().typeId, pkPyType);

				for (ReflectFunction* func: type->GetFunctions())
				{
					String sig = func->GetName();
					sig += "(";

					if (!func->IsStatic())
					{
						sig += "self,";
					}

					for (ReflectParam* param : func->GetParams())
					{
						sig += param->GetName();
						sig += ",";
					}

					if (sig[sig.Size() - 1] == ',')
					{
						sig.Resize(sig.Size() - 1);
					}

					sig += ")";

					ReflectType* retType = nullptr;
					FieldProps retInfo = func->GetReturn();
					if (retInfo.typeId != TypeInfo<void>::ID())
					{
						retType = Reflection::FindTypeById(retInfo.typeId);
					}

					//TODO: is there a better way of doing it?
					char buffer[1000] = {};
					py_Ref pkPyFunc = reinterpret_cast<py_Ref>(buffer);
					py_newfunction(pkPyFunc, sig.CStr(), CallFunction, nullptr, 1);

					auto slot = py_getslot(pkPyFunc, 0);
					void* data = py_newobject(slot, 0, 0, sizeof(FunctionUserData));
					new (data) FunctionUserData{func, retType};

					py_setdict(py_tpobject(pkPyType), py_name(func->GetName().CStr()), pkPyFunc);
				}
			}
		}
	}

	void PkPyScriptingEngineShutdown()
	{
		Resources::FindType<PkPyScriptResource>()->UnregisterEvent(PkPyScriptResourceChange, nullptr);
		py_finalize();
	}
}
