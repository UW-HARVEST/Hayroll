#ifndef HAYROLL_UTIL_HPP
#define HAYROLL_UTIL_HPP

namespace Hayroll
{

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace Hayroll

#endif // HAYROLL_UTIL_HPP
