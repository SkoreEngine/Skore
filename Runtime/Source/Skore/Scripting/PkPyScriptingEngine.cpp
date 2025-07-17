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
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore: PkPyScriptingEngine");

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

	int add(int a, int b) {
		return a + b;
	}

	// auto data = py_touserdata(py_getslot(func.ptr(), 0));

	py_Type loggerType;

	// 1. Define a wrapper function with the signature `py_CFunction`.
	bool py_add(int argc, py_Ref argv)
	{
		// 2. Check the number of arguments.
		PY_CHECK_ARGC(2);
		// 3. Check the type of arguments.
		PY_CHECK_ARG_TYPE(0, tp_int);
		PY_CHECK_ARG_TYPE(1, tp_int);
		// 4. Convert the arguments into C types.
		int _0 = py_toint(py_arg(0));
		int _1 = py_toint(py_arg(1));
		// 5. Call the original function.
		int res = add(_0, _1);
		// 6. Set the return value.
		py_newint(py_retval(), res);
		// 7. Return `true`.
		return true;
	}

	bool GetLogger(int argc, py_Ref argv)
	{
		void* object = py_newobject(py_retval(), loggerType, 1, 0);
		//object = Logger::GetLogger("Skore::Test");
		return true;
	}


	bool PrintInfo(int argc, py_Ref argv)
	{
		void* userData = py_touserdata(py_arg(0));
		Logger::GetLogger("Skore::Test").Info("{}", py_tostr(py_arg(1)));
		py_newnone(py_retval());
		return true;
	}


	void PkPyScriptingEngineInit()
	{
		py_initialize();
		Resources::FindType<PkPyScriptResource>()->RegisterEvent(PkPyScriptResourceChange, nullptr);

		py_GlobalRef mod = py_newmodule("skore");

		loggerType = py_newtype("Logger", tp_object, mod, nullptr);
		py_bindstaticmethod(loggerType, "get_logger", GetLogger);
		py_bindmethod(loggerType, "info", PrintInfo);


	}

	void PkPyScriptingEngineShutdown()
	{
		Resources::FindType<PkPyScriptResource>()->UnregisterEvent(PkPyScriptResourceChange, nullptr);
		py_finalize();
	}
}
