#include <boost/asio.hpp>

using namespace boost;

namespace Tools
{
    template <typename T>
    void to_network(const T& x, char* begin)
    {
        auto szT = sizeof(T);
        assert(szT==1 or szT==2 or szT==4);

        T* p = reinterpret_cast<T*>(begin);
        *p = x;

        if(szT==2)*p = static_cast<T>( asio::detail::socket_ops::host_to_network_short(x) );
        else *p = static_cast<T>( asio::detail::socket_ops::host_to_network_long(x) );
    }

    template <typename T>
    T from_network(char* begin)
    {
        auto szT = sizeof(T);
        assert(szT==1 or szT==2 or szT==4);

        T x = (*reinterpret_cast<T*>(begin));
        if(szT==2)return static_cast<T>( asio::detail::socket_ops::network_to_host_short(x) );
        else return static_cast<T>( asio::detail::socket_ops::network_to_host_long(x) );
    }
}