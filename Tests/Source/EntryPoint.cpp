
#define DOCTEST_CONFIG_IMPLEMENT
#include "Skore/App.hpp"
#include "doctest.h"
#include "Skore/Core/Logger.hpp"

using namespace Skore;

doctest::Context context;

int main(int argc, char** argv)
{

	Logger::SetDefaultLevel(LogLevel::Debug);

	context.applyCommandLine(argc, argv);
	context.setOption("no-breaks", true);

	int res = context.run();

	return res;
}
