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
			char* msg = py_formatexc();
			logger.Error("Error executing script: {}", msg);
		}
	}


	void PkPyScriptingEngineInit()
	{
		py_initialize();
		//py_exec("print('Hello world!')", "<string>", EXEC_MODE, NULL);
		Resources::FindType<PkPyScriptResource>()->RegisterEvent(PkPyScriptResourceChange, nullptr);

	}

	void PkPyScriptingEngineShutdown()
	{
		Resources::FindType<PkPyScriptResource>()->UnregisterEvent(PkPyScriptResourceChange, nullptr);
		py_finalize();
	}


}
