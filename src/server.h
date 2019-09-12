#ifndef SERVER_H
#define SERVER_H

#include <list>
#include <boost/asio.hpp>

class server {
    private:
        class listener;

    public:
        explicit server(boost::asio::io_service &ios, unsigned short port);
        virtual ~server();

        void start();
        void shutdown();

    private:
        boost::asio::io_service &ios_;
        unsigned short port_;
        std::list<std::shared_ptr<listener>> listeners_;
};

#endif /* SERVER_H */
