#ifndef SERVER_H
#define SERVER_H

#include <list>
#include <boost/asio.hpp>

#include <boost/beast/http.hpp>

template<class T>
class request_wrapper {
    private:
        std::unique_ptr<T> wrapped_;
};

using basic_request = request_wrapper<boost::beast::http::request<boost::beast::http::string_body>>;

// XXX rename to httpd
class server {
    public:
        // XXX need a callback type and a way to register for a path prefix
        // XXX also need a callback type for that thing to send the response
        // async

        explicit server(boost::asio::io_service &ios, unsigned short port);
        virtual ~server();

        void start();
        void shutdown();

    private:
        class listener;

        boost::asio::io_service &ios_;
        unsigned short port_;
        std::list<std::shared_ptr<listener>> listeners_;
};

#endif /* SERVER_H */
