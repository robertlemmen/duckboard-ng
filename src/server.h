#ifndef SERVER_H
#define SERVER_H

#include <boost/asio.hpp>

class server {
    private:
        class listener;

    public:
        explicit server(boost::asio::io_service &ios);
        virtual ~server();

        void start();
        void stop();

    private:
        boost::asio::io_service &ios_;
        std::shared_ptr<listener> listener_;
        
};

#endif /* SERVER_H */
