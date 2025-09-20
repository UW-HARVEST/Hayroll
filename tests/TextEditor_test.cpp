#include <iostream>

#include <spdlog/spdlog.h>

#include "TextEditor.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    std::string initialText = "Line-1\nLine-2\nLine-3";
    std::string expected = "AAAL   -1\nLine-2 BBB\nLiCCC3\n\n DDD\n";
    TextEditor editor(initialText);
    editor.insert(1, 1, "AAA");
    editor.insert(2, 8, "BBB");
    editor.modify(3, 3, "CCC");
    editor.modify(5, 2, "DDD"); // Modify "ine" to "DDD"
    editor.erase(1, 2, 1, 5); // Erase "ine-" to spaces
    std::string result = editor.commit();

    if (result == expected)
    {
        std::cout << "Test passed!" << std::endl;
    }
    else
    {
        std::cout << "Test failed!" << std::endl;
        std::cout << "Expected: \n" << expected << std::endl;
        std::cout << "Got: \n" << result << std::endl;
        return 1;
    }

    return 0;
}
