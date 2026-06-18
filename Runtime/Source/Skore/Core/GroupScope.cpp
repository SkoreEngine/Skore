#include "Skore/Core/GroupScope.hpp"

#include "Skore/Core/Array.hpp"

namespace Skore
{
	static Array<String> groupStack;

	void PushGroup(StringView name)
	{
		groupStack.EmplaceBack(name);
	}

	void PopGroup()
	{
		groupStack.PopBack();
	}

	String GetCurrentScope()
	{
		String result;
		for (int i = 0; i < groupStack.Size(); ++i)
		{
			result += groupStack[i];
			if (i != groupStack.Size() - 1)
			{
				result += ".";
			}
		}
		return result;
	}
}
