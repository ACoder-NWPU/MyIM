#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <set>
#include <atomic>
#include <random>
#include <cstdlib>
#include <sstream>
#include "protocol.h"
#include "tools.hpp"

using namespace boost;
using tcp_socket = boost::asio::ip::tcp::socket;
using UserPtr = std::shared_ptr<class User>;
using RoomPtr = std::shared_ptr<class Room>;

class Room
{
    private:
    Protocol::id_t id;
    std::set<UserPtr> users;
    static std::atomic<Protocol::id_t> id_count;

    public:
    auto begin(){return users.begin();}
    auto end(){return users.end();}
    auto size(){return users.size();}
    void enter(UserPtr user){users.insert(user);}
    void leave(UserPtr user){users.erase(user);}
    auto getid(){return id;}

    Room():id(++id_count)
    {}
};
std::atomic<Protocol::id_t> Room::id_count;

class User
{
    private:
    
    std::string name;
    Protocol::id_t id, roomid;
    asio::ip::tcp::socket sock;
    static std::atomic<Protocol::id_t> id_count;

    public:
    
    Protocol::id_t getid(){return id;}
    Protocol::id_t getroom(){return roomid;}
    std::string getname(){return name;}
    asio::ip::tcp::socket& getsock(){return sock;}
    void setroom(const Protocol::id_t &new_roomid){roomid=new_roomid;}
    void setname(const std::string &new_name){name=new_name;}
    bool match(const std::string &s)
    {
        return name.find(s) != std::string::npos;
    }
    User(asio::io_service& service):roomid(Protocol::null_room_id), sock(service), id(++id_count)
    {}
};
std::atomic<Protocol::id_t> User::id_count;

class Server
{
    private:

    using recv_msg_t = Protocol::Message::Client_to_Server;
    using send_msg_t = Protocol::Message::Server_to_Client;

    static const int MaxAverageSocket = 100;
    static const int recv_header_length = sizeof(recv_msg_t::header_t::type) + sizeof(recv_msg_t::header_t::body_len);
    static const int send_header_length = sizeof(send_msg_t::header_t::type) + sizeof(recv_msg_t::header_t::body_len);

    using recv_header_buf_t = std::array<char,recv_header_length>;
    using recv_body_buf_t = std::array<char,Protocol::BodyMaxLength>;
    using send_buf_t = std::array<char,Protocol::BodyMaxLength+send_header_length>;

    std::map< Protocol::id_t, UserPtr > users;
    std::map< Protocol::id_t, RoomPtr > rooms;
    asio::io_service asio_service;
    asio::ip::tcp::endpoint server_ep;
    asio::ip::tcp::acceptor acceptor;
    std::vector<std::shared_ptr<std::thread>> threads;

    void RegisterAccept()
    {
        auto new_user = std::make_shared<User>(asio_service);
        acceptor.async_accept( new_user->getsock(),  [=](const boost::system::error_code& eno){this->AcceptHandler(new_user, eno);} );
    }

    void RegisterReadHeader(UserPtr usr)
    {
        auto header_buf = std::make_shared<recv_header_buf_t>();
        auto lambda = [=](const boost::system::error_code& eno, std::size_t len){ this->ReceiveHeaderHandler(usr,header_buf,eno,len); };
        asio::async_read(usr->getsock(), buffer(*header_buf),  asio::transfer_exactly(recv_header_length), lambda );
    }

    void RegisterReadBody(UserPtr usr, const recv_msg_t::header_t& header)
    {
        auto body_buf_ptr = std::make_shared<recv_body_buf_t>();
        asio::async_read( usr->getsock(), buffer(*body_buf_ptr), asio::transfer_exactly(header.body_len),
            [=](const boost::system::error_code& eno, std::size_t len){ this->ReceiveBodyHandler(usr,body_buf_ptr,header,eno,len); } );
    }

    template <typename T>
    void RegisterSend(UserPtr usr, const T& send_buf, std::uint32_t send_len)
    {
        asio::async_write( usr->getsock(), buffer(send_buf), asio::transfer_exactly(send_len),
            [=](const boost::system::error_code& eno, std::size_t len){this->WriteHandler(eno,len);});
    }

    void AcceptHandler(UserPtr new_user, const boost::system::error_code& eno)
    {
        if(!eno)
        {
            // header = type + body_len
            users[new_user->getid()] = new_user;
            RegisterReadHeader(new_user);
            if( users.size() > MaxAverageSocket*threads.size()) // equals to (users.size()/threads.size() > MaxAverageSocket)
                threads.emplace_back( std::make_shared<std::thread>([&]{asio_service.run();}) );
        }
        RegisterAccept();
    }

    void ReceiveHeaderHandler(UserPtr usr, std::shared_ptr<recv_header_buf_t> header_buf, const boost::system::error_code& eno, std::size_t recv_len)
    {
        if(eno)
        {
            std::cerr << "usr= " << usr->getname() << " errno: " << eno << std::endl;
            usr->getsock().close();
        }

        recv_msg_t::header_t header;
        header.type = Tools::from_network<decltype(header.type)>(header_buf->begin());
        header.body_len = Tools::from_network<decltype(header.body_len)>(header_buf->begin()+sizeof(header.type));
        std::shared_ptr<recv_body_buf_t> body_buf_ptr;
        switch (header.type)
        {
            case recv_msg_t::header_t::rename:
            case recv_msg_t::header_t::find:
            case recv_msg_t::header_t::text:
            case recv_msg_t::header_t::enter:
                break;

            case recv_msg_t::header_t::rooms:
            {
                std::string send_string;
                for(auto& pr:rooms)
                {
                    std::string append_str;
                    auto &r = *pr.second;
                    append_str = "Room" + boost::lexical_cast<std::string>(pr.first) + ":\n\tUsers:";
                    int res = Protocol::MaxUsersPerRoom;
                    for(auto u:r)
                    {
                        append_str += " " + u->getname();
                        if(--res==0)break;
                    }
                    if(r.size()>Protocol::MaxUsersShowPerLine)append_str += " ...";
                    if(append_str.size() + send_string.size() >= Protocol::BodyMaxLength)break;
                    send_string += append_str;
                }
                SendPrint(usr,send_string);
                break;
            }
            
            case recv_msg_t::header_t::users:
            {
                std::string send_string;
                for(auto& pr:users)
                {
                    std::string append_str;
                    auto &u = *pr.second;
                    append_str = "User " + u.getname();
                    if(u.getroom()!=Protocol::null_room_id)
                        append_str += "(in room " + lexical_cast<std::string>(u.getroom()) + ")";
                    append_str += '\n';
                    if(append_str.size() + send_string.size() >= Protocol::BodyMaxLength)break;
                    send_string += append_str;
                }
                SendPrint(usr,send_string);
                break;
            }
            
            case recv_msg_t::header_t::leave:
                if(usr->getroom()!=Protocol::null_room_id)
                {
                    rooms[usr->getroom()]->leave(usr);
                    usr->setroom(Protocol::null_room_id);
                    InformRoom(usr);
                }
                break;
            
            case recv_msg_t::header_t::newroom:
            {
                auto new_room = std::make_shared<Room>();
                rooms[new_room->getid()] = new_room;
                new_room->enter(usr);
                usr->setroom(new_room->getid());
                InformRoom(usr);
                break;
            }
            
            case recv_msg_t::header_t::randroom:
            {
                if(rooms.size()==0)break;
                Protocol::id_t roomid = (rand()%rooms.size())+1;
                rooms[roomid]->enter(usr);
                usr->setroom(roomid);
                InformRoom(usr);
                break;
            }
            
            default:
                std::cerr << "usr=" << usr->getname() << " undefined header.type=" << header.type << std::endl;
                usr->getsock().close();
        }
        if(header.body_len > 0)RegisterReadBody(usr,header);
        else RegisterReadHeader(usr);
    }

    void ReceiveBodyHandler(UserPtr usr, std::shared_ptr<recv_body_buf_t> buf, recv_msg_t::header_t header, const boost::system::error_code& eno, std::size_t recv_len)
    {
        if(eno)
        {
            std::cerr << "usr= " << usr->getname() << " errno: " << eno << std::endl;
            usr->getsock().close();
        }

        if(header.body_len != recv_len)
        {
            std::cerr << "usr= " << usr->getname() << " header.body_len != recv_len!" << std::endl;
            usr->getsock().close();
        }
        
        if(header.type == recv_msg_t::header_t::rename)
        {
            usr->setname((*buf).begin());
        }
        else if(header.type == recv_msg_t::header_t::find)
        {
            std::string name((*buf).begin());
            std::string send_string;
            for(auto u:users)
            {
                if(u.second->match(name))
                {
                    std::string add = "User " + u.second->getname();
                    if(u.second->getroom()!=Protocol::null_room_id)
                        add += "(in room" + lexical_cast<std::string>(u.second->getroom()) + ")";
                    add += "\n";
                    if(send_string.size()+add.size() >= Protocol::BodyMaxLength)break;
                    else send_string += add;
                }
            }
            SendPrint(usr,send_string);
        }
        else if(header.type == recv_msg_t::header_t::text)
        {
            auto room = *(rooms[usr->getroom()]);
            for(auto u:room)
            {
                SendPrint(u,usr->getname()+" say: ");
                SendPrint(u,buf->begin());
            }
        }
        else if(header.type == recv_msg_t::header_t::enter)
        {
            Protocol::id_t roomid = asio::detail::socket_ops::network_to_host_long( *reinterpret_cast<std::uint32_t*>( (*buf).begin() ) );
            usr->setroom(roomid);
            rooms[roomid]->enter(usr);
            InformRoom(usr);
        }
        else
        {
            std::cerr << "usr=" << usr->getname() << " undefined header.type=" << header.type << std::endl;
            usr->getsock().close();
        }

        RegisterReadHeader(usr);
    }

    void WriteHandler(const boost::system::error_code& eno, std::size_t trans_len)
    {
    }

    void SendPrint(UserPtr usr, const std::string &str)
    {
        std::shared_ptr<send_buf_t> send_buf = std::make_shared<send_buf_t>();
        send_msg_t::header_t header;

        // header
        header.type = send_msg_t::header_t::print;
        header.body_len = str.size()+1; // with '\0'
        Tools::to_network(header.type,send_buf->begin());
        Tools::to_network(header.body_len,send_buf->begin()+sizeof(header.type));

        // body
        for(int i=0;i<str.size();i++)
            send_buf->at(sizeof(header.type)+sizeof(header.body_len)+i) = str[i];
        send_buf->at(sizeof(header.type)+sizeof(header.body_len)+str.size()) = '\0';

        RegisterSend(usr, *send_buf, sizeof(header.type)+sizeof(header.body_len)+header.body_len );
    }

    void SendNoBody(UserPtr usr, send_msg_t::header_t::type_t type)
    {
        send_msg_t::header_t header;
        auto send_buf = std::make_shared<std::array<char,send_header_length>>();
        
        header.type = type;
        header.body_len = 0;
        Tools::to_network(header.type,send_buf->begin());
        Tools::to_network(header.body_len,send_buf->begin()+sizeof(header.type));

        RegisterSend(usr, *send_buf, sizeof(header.type)+sizeof(header.body_len) );
    }

    void InformRoom(UserPtr usr)
    {
        std::shared_ptr<send_buf_t> send_buf = std::make_shared<send_buf_t>();
        send_msg_t::header_t header;

        // header
        header.type = send_msg_t::header_t::roomchange;
        header.body_len = sizeof(Protocol::id_t);
        Tools::to_network(header.type,send_buf->begin());
        Tools::to_network(header.body_len,send_buf->begin()+sizeof(header.type));

        // body
        Tools::to_network(usr->getroom(), send_buf->begin()+sizeof(header.type)+sizeof(header.body_len));

        RegisterSend(usr, *send_buf, sizeof(header.type)+sizeof(header.body_len)+header.body_len );
    }

    public:

    void Launch()
    {
        RegisterAccept();
        threads.emplace_back( std::make_shared<std::thread>([&]{asio_service.run();}) );
    }

    void Close()
    {
        asio_service.stop();
        for(auto& t:threads)t->join();
        threads.clear();
        users.clear();
        rooms.clear();
    }

    std::string ShowUsers(int limit = 20)
    {
        std::stringstream ss;
        for(auto &pr: users)
        {
            ss << "User" << pr.first << " " << pr.second->getname()
                << "(" << pr.second->getsock().remote_endpoint().address().to_string() << ":" << pr.second->getsock().remote_endpoint().port() << ")"
                << std::endl;
        }
        return ss.str();
    }

    std::string ShowRooms()
    {
        std::stringstream ss;
        for(auto& pr: rooms)
        {
            ss << "Room" << pr.first << ":\n";
            for(auto u:*pr.second)
            {
                ss << u->getname() << " ";
            }
            ss << std::endl;
        }
        return ss.str();
    }

    Server() : server_ep(asio::ip::address::from_string(Protocol::server_ip),Protocol::server_port), acceptor(asio_service, server_ep)
    {}

};

int main()
{
    Server server;
    server.Launch();
    std::string usage = "usage:\n"
    "\tquit: close the serve and quit\n"
    "\trooms: show all rooms\n"
    "\tusers: show all users\n";
    std::cout << usage << ">> " << std::flush;
    std::string s;
    while(std::getline(std::cin,s))
    {
        if(s=="quit")
        {
            server.Close();
            break;
        }
        else if(s=="rooms")
        {
            std::cout << server.ShowRooms() << std::flush;
        }
        else if(s=="users")
        {
            std::cout << server.ShowUsers() << std::flush;
        }
        else
        {
            std::cout << usage << std::flush;
        }
        std::cout << ">> " << std::flush;
    }
}