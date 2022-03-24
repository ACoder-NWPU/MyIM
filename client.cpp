#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include "protocol.h"
#include "tools.hpp"

using namespace boost;
using namespace boost::asio;
using namespace boost::system;

struct UserInfo
{
    std::string name;
    Protocol::id_t roomid = Protocol::null_room_id;
};

class Client
{
    private:

    using recv_msg_t = Protocol::Message::Server_to_Client;
    using recv_header_t = Protocol::Message::Server_to_Client::header_t;
    using send_msg_t = Protocol::Message::Client_to_Server;
    using send_header_t = Protocol::Message::Client_to_Server::header_t;

    static const int recv_header_length = sizeof(recv_header_t::type) + sizeof(recv_header_t::body_len);
    static const int send_buf_length = sizeof(send_header_t::type) + Protocol::BodyMaxLength;

    using recv_header_buf_t = std::array<char,recv_header_length>;
    using recv_body_buf_t = std::array<char,Protocol::BodyMaxLength>;
    using send_buf_t = std::array<char,send_buf_length>;

    io_service service;
    ip::tcp::endpoint server_ep;
    ip::tcp::socket sock;
    UserInfo info;

    void Print(const std::string& s)
    {
        std::cout << s << std::flush;
        if(!s.empty() and s[s.size()-1]!='\n')std::cout << std::endl;
    }

    void RegisterReadHeader()
    {
        auto header_buf = std::make_shared<recv_header_buf_t>();
        async_read(sock, buffer(*header_buf), transfer_exactly(recv_header_length), [=](const error_code& e, std::size_t recv_len){this->ReceiveHeaderHandler(header_buf,e,recv_len);});
    }

    void RegisterReadBody(const recv_header_t &header)
    {
        auto body_buf = std::make_shared<recv_body_buf_t>();
        async_read(sock, buffer(*body_buf), transfer_exactly(header.body_len), [=](const error_code& e, std::size_t recv_len){this->ReceiveBodyHandler(body_buf,header,e,recv_len);} );
    }

    void RegisterWrite(const send_header_t::type_t& type)
    {
        auto send_buf = std::make_shared<send_buf_t>();
        Tools::to_network(type,send_buf->begin());
        async_write(sock, buffer(*send_buf), transfer_exactly(sizeof(type)+sizeof(send_header_t::body_len)),
            [=](const error_code& e, std::size_t trans_len){this->WriteHandler(e,trans_len);} );
    }

    void RegisterWrite(const send_header_t::type_t& type, const std::uint32_t& int_arg)
    {
        auto send_buf = std::make_shared<send_buf_t>();

        // header
        Tools::to_network(type,send_buf->begin());
        decltype(send_header_t::body_len) body_len = sizeof(int_arg);
        Tools::to_network(static_cast<std::uint32_t>(sizeof(int_arg)),send_buf->begin()+sizeof(type));

        // body
        Tools::to_network(int_arg, send_buf->begin()+sizeof(type)+sizeof(int_arg));

        async_write(sock, buffer(*send_buf), transfer_exactly(sizeof(type)+sizeof(body_len)+body_len),
            [=](const error_code& e, std::size_t trans_len){this->WriteHandler(e,trans_len);} );
    }

    void RegisterWrite(const send_header_t::type_t& type, const std::string& str_arg)
    {
        auto send_buf = std::make_shared<send_buf_t>();
        Tools::to_network(type,send_buf->begin());

        decltype(send_header_t::body_len) body_len;

        const std::string& str = str_arg;
        body_len = str.size()+1;
        for(int i=0;i<str_arg.size();i++)
            send_buf->at(sizeof(type)+sizeof(body_len)+i) = str[i];

        if(body_len>Protocol::BodyMaxLength)
        {
            std::cerr << "Body too long!" << std::endl;
            return;
        }

        Tools::to_network(body_len,send_buf->begin()+sizeof(type));

        async_write(sock, buffer(*send_buf), transfer_exactly(sizeof(type)+sizeof(body_len)+body_len),
            [=](const error_code& e, std::size_t trans_len){this->WriteHandler(e,trans_len);} );
    }

    void ReceiveHeaderHandler(std::shared_ptr<recv_header_buf_t> header_buf, const error_code& e, std::size_t recv_len)
    {
        if(!e)
        {
            recv_header_t header;
            header.type = Tools::from_network<decltype(header.type)>(header_buf->begin());
            header.body_len = Tools::from_network<decltype(header.body_len)>(header_buf->begin()+sizeof(header.type));
            
            switch(header.type)
            {
                case recv_header_t::print:
                case recv_header_t::roomchange:
                    break;
                default:
                {
                    std::cerr << "Undefined type " << header.type << std::endl;
                    return;
                }
            }

            if(header.body_len==0)RegisterReadHeader();
            else RegisterReadBody(header);
        }
        else std::cerr << "Error!" << std::endl;
    }

    void ReceiveBodyHandler(std::shared_ptr<recv_body_buf_t> body_buf, recv_header_t header, const error_code& e, std::size_t recv_len)
    {
        switch (header.type)
        {
            case recv_header_t::print:
            {
                Print(body_buf->begin());
                break;
            }
            case recv_header_t::roomchange:
            {
                info.roomid = Tools::from_network<decltype(info.roomid)>(body_buf->begin());
                break;
            }
            default:
            {
                std::cerr << "Undefined type " << header.type << std::endl;
                break;
            }
        }
        RegisterReadHeader();
    }

    void WriteHandler(const error_code& e, std::size_t trans_len)
    {
        if(e)
        {
            std::cerr << "send fail" << std::endl;
        }
    }

    public:

    Client():
        server_ep(ip::address::from_string(Protocol::server_ip),Protocol::server_port),
        sock(service)
    {}

    bool Connect()
    {
        error_code e;
        sock.connect(server_ep,e);
        return !e;
    }

    void NetworkLoop()
    {
        RegisterReadHeader();
        service.run();
    }

    bool chatting()
    {
        return info.roomid != Protocol::null_room_id;
    }

    operator bool()
    {
        return sock.is_open();
    }

    std::string getname()
    {
        return info.name;
    }

    void setname(const std::string& new_name)
    {
        info.name = new_name;
    }

    Protocol::id_t getroomid()
    {
        return info.roomid;
    }

    void remote_exec(const send_header_t::type_t& type)
    {
        RegisterWrite(type);
    }

    template <typename T>
    void remote_exec(const send_header_t::type_t& type, const T& arg)
    {
        RegisterWrite(type,arg);
    }
};

int main()
{
    Client client;
    std::thread th;
    std::stringstream ss;
    std::string new_name, order;
    std::string usage = 
    "usage:\n"
    "name: show your name\n"
    "rename xxx: change your name to xxx\n"
    "rooms: list all rooms\n"
    "users: list all users\n"
    "enter room_id: enter the room with id room_id (and enter chatting mod)\n"
    "::leave (in chatting mod): leave current room\n"
    "find xxx: find the user with name xxx\n"
    "newroom: create a new room and enter it\n"
    "randroom: randomly enter a room\n"
    ".... (in chatting mod): send some text to the current room\n"
    "::roomid (in chatting mod): query your current room id\n"
    "exit: exit the program\n";
    
    typedef Protocol::Message::Client_to_Server::header_t::type_t command_t;
    while(true)
    {
        auto success = client.Connect();
        if(!success)
        {
            std::cerr << "Failed to connect to server." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        else
        {
            std::cerr << "Connected to server successfully." << std::endl;
            th = std::thread([&]{client.NetworkLoop();});
            break;
        }
    }
    std::cout << "Input your name: ";
    std::cin >> new_name;
    client.remote_exec(command_t::rename, new_name);
    client.setname(new_name);
    std::cout << "Welcome, " << new_name << '\n' << std::endl;

    std::cout << usage << std::endl;

    while(true)
    {
        if(!client)
        {
            std::cout << "Connection lost." << std::endl;
        }
        if(client.chatting())
        {
            std::cout << "(chatting mod) ";
        }
        else
        {
            std::cout << ">> ";
        }

        std::getline(std::cin,order);
        if(!std::cin)
        {
            std::cerr << "IO error!" << std::endl;
            break;
        }

        if(client.chatting())
        {
            if(order == "::leave")
            {
                client.remote_exec(command_t::leave);
            }
            else if(order == "::roomid")
            {
                std::cout << "roomid=" << client.getroomid() << std::endl;
            }
            else
            {
                client.remote_exec(command_t::text, order);
            }
        }
        else
        {
            if(order == "name")
            {
                std::cout << client.getname() << std::endl;
            }
            else if(order.substr(0,std::string("rename").size()) == "rename")
            {
                std::string _, new_name;
                ss.clear();
                ss << order;
                ss >> _ >> new_name;
                if(new_name.size() == 0 or new_name.size()>Protocol::NameMaxLength)
                {
                    std::cerr << "The length of name is too short or too long." << std::endl;
                }
                else
                {
                    client.remote_exec(command_t::rename, new_name );
                    client.setname(new_name);
                }
            }
            else if(order=="rooms")
            {
                client.remote_exec(command_t::rooms);
            }
            else if(order=="users")
            {
                client.remote_exec(command_t::users);
            }
            else if( order.substr(0,std::string("enter").size()) == "enter" )
            {
                std::string _;
                Protocol::id_t roomid;
                ss.clear();
                ss << order;
                ss >> _ >> roomid;
                client.remote_exec(command_t::enter,roomid);
            }
            else if(order.substr(0,std::string("find").size()) == "find")
            {
                std::string _, name;
                ss.clear();
                ss << order;
                ss >> _ >> name;
                if(new_name.size() == 0 or new_name.size()>Protocol::NameMaxLength)
                {
                    std::cerr << "The length of name is too short or too long." << std::endl;
                }
                else client.remote_exec(command_t::find, new_name );
            }
            else if(order.substr(0,std::string("enter").size()) == "enter")
            {
                std::string _, roomid;
                ss.clear();
                ss << order;
                ss >> _ >> roomid;
                client.remote_exec(command_t::enter,roomid);
            }
            else if(order=="newroom")
            {
                client.remote_exec(command_t::newroom);
            }
            else if(order=="randroom")
            {
                client.remote_exec(command_t::randroom);
            }
            else if(order=="exit")
            {
                break;
            }
            else
            {
                std::string _;
                ss.clear();
                ss << order;
                ss >> _;
                std::cout << "Unknown command: " << _ << std::endl << usage << std::endl;
            }
        }
    }

    return 0;
}