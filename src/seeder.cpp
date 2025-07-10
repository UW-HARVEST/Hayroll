// Reads Maki's .cpp2c invocation summary file and generates the instrumentation task
// You should first run Maki to get the .cpp2c file

// Overall process: xxxInfo -> Tag -> InstrumentationTask -> awk command
// xxxInfo: Direct mapping from Maki's .ccp2c format.
// Tag: Selected info from xxxInfo to be inserted into C code.
// InstrumentationTask: What to insert (serialized and escaped Tag) and where to insert, in a format that is easy to convert to awk commands.

#include <iostream>

#include "Util.hpp"
#include "Seeder.hpp"

int main(const int argc, const char* argv[])
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    // WIP

    return 0;
}
