#ifndef HAYROLL_UTIL_HPP
#define HAYROLL_UTIL_HPP

#include <string>
#include <vector>

namespace Hayroll
{

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

class StringBuilder
{
public:
    StringBuilder() : bufferSize(0) {}

    void append(std::string && str)
    {
        bufferOwnershipCache.emplace_back(std::move(str));
        buffer.emplace_back(bufferOwnershipCache.back());
        bufferSize += bufferOwnershipCache.back().size();
    }

    void append(std::string_view str)
    {
        buffer.emplace_back(str);
        bufferSize += str.size();
    }

    void append(const char * str)
    {
        buffer.emplace_back(str);
        bufferSize += std::char_traits<char>::length(str);
    }

    std::string str() const
    {
        std::string result;
        result.reserve(bufferSize + 32);
        for (std::string_view segment : buffer)
        {
            result.append(segment);
        }
        return result;
    }
private:
    std::vector<std::string> bufferOwnershipCache;
    std::vector<std::string_view> buffer;
    size_t bufferSize;
};

} // namespace Hayroll

#endif // HAYROLL_UTIL_HPP
