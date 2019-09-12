#ifndef SERVER_H
#define SERVER_H

#include <boost/asio.hpp>

class server {
    public:
        explicit server(boost::asio::io_service &ios);
        virtual ~server();

        void start();
        void stop();

    private:
        boost::asio::io_service &ios_;
        
};

#endif /* SERVER_H */
