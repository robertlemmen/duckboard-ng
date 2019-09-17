#ifndef HTTP_H
#define HTTP_H

#include <list>
#include <boost/asio.hpp>

class http_message {
    // XXX headers
    public:
        const std::string& body() const { return body_; }
        void body(const std::string &b) { body_ = b; }
    private:
        std::string body_;
};

class http_request : public http_message {
    public:
        const std::string_view& method() const { return method_; }
        void method(std::string_view m) { method_ = move(m); }
        const std::string_view& target() const { return target_; }
        void target(std::string_view t) { target_ = move(t); }
    private:
        std::string_view method_;
        std::string_view target_;
};

class http_response : public http_message {
    public:
        unsigned int status() const { return status_; }
        void status(unsigned int s) { status_ = s; }
    private:
        unsigned int status_;
};

class http_server {
    public:
        typedef std::function<void(http_response&&)> callback_func;
        typedef std::function<void(http_request&&, callback_func)> handler_func;

        explicit http_server(boost::asio::io_service &ios, unsigned short port);
        virtual ~http_server();

        // this is only a very simple multiplexing, typically you only want 
        // one or very few handlers registered through this, but those handlers
        // could very well be routers that do more complex request matching
        void register_handler(const std::string &prefix, handler_func);

        void start();
        void shutdown();

    private:
        class listener;

        boost::asio::io_service &ios_;
        unsigned short port_;
        std::list<std::shared_ptr<listener>> listeners_;
        std::list<std::pair<std::string,handler_func>> handlers_;
};

#endif /* HTTP_H */
