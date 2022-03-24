#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <memory>
#include <algorithm>

using namespace boost::asio;

namespace Protocol
{
    const std::string server_ip = "127.0.0.1";
    const int server_port = 5000;
    const int MaxTotalUsers = 10000;
    const int MaxUsersPerRoom = 100;
    const int NameMaxLength = 50;
    const int TextMaxLength = 1000;
    const int PrintMaxLength = 1000;
    const int BodyMaxLength = std::max(TextMaxLength, PrintMaxLength);
    const std::uint32_t null_room_id = 0;
    const int MaxUsersShowPerLine = 5;

    using id_t = std::uint32_t;

    namespace Message
    {
        /*
        command (client)            meaning
        name                        show my name
        rename xxx                  change my name to xxx
        rooms                       list all rooms
        users                       list all users
        enter room_id               enter the room with id room_id
        ::leave (in chatting mod)   leave current room
        find username               find the user with name "username"
        newroom                     create a new room and enter it
        randroom                    randomly enter a room
        .... (in chatting mod)      send some text to the current room
        ::roomid (in chatting mod)  query the current room id ( no need internet )
        */

       /*
       A message from Client to Server should contain:
       
       type (4bytes)
       body_len (4bytes)
       body (with length not fixed)

       The body_len shows the length of body (in bytes)
       */
        struct Client_to_Server
        {
            struct header_t
            {
                enum type_t : std::uint32_t
                {
                    rename,
                    rooms,
                    users,
                    enter,
                    leave,
                    find,
                    newroom,
                    randroom,
                    text
                }type;
                std::uint32_t body_len;
            }header;
            // char body[BodyMaxLength];
        };

        /*
       A message from Server to Client should contain:
       
       type (4bytes)
       body_len (4bytes)
       body (with length not fixed)

       The body_len shows the length of body (in bytes)
       */
        struct Server_to_Client
        {
            struct header_t
            {
                enum type_t : std::uint32_t
                {
                    print,  // to print something immidiately on screen
                    roomchange  // to inform the client to change a room
                }type;
                std::uint32_t body_len;
            }header;
            // char body[BodyMaxLength];
        };
    }
}

#endif // PROTOCOL_H