#include "http.h"

#include <iostream>
#include <utility>

#include <boost/log/trivial.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

using namespace std;
using namespace boost::asio;
using namespace boost::posix_time;
namespace beast = boost::beast;
namespace http = beast::http;

void fail(beast::error_code ec, char const* what) {
    // XXX this is stupid, throw weexceptions or log and recover
    cerr << what << ": " << ec.message() << "\n";
}

class session : public enable_shared_from_this<session> {
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    shared_ptr<void> res_;
    list<pair<string,http_server::handler_func>> &handlers_;

public:
    // Take ownership of the stream
    session(ip::tcp::socket &&socket, list<pair<string,http_server::handler_func>> &handlers) :
            stream_(move(socket)),
            handlers_(handlers) {
    
    }

    // Start the asynchronous operation
    void run() {
        do_read();
    }

    void do_read() {
        // Make the request empty before reading,
        // otherwise the operation behavior is undefined.
        req_ = {};

        // Set the timeout.
        stream_.expires_after(std::chrono::seconds(30));

        // Read a request
        http::async_read(stream_, buffer_, req_,
            beast::bind_front_handler(
                &session::on_read,
                shared_from_this()));
    }

    void on_read(beast::error_code ec, size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        // This means they closed the connection
        if (ec == http::error::end_of_stream) {
            return do_close();
        }

        if (ec) {
            return fail(ec, "read");
        }

        // go through the handlers
        unsigned int last_length = 0;
        http_server::handler_func selected_handler_func;
        for (auto it : handlers_) {
            if (req_.target().starts_with(it.first)) {
                // a viable candidate
                if (it.first.length() >= last_length) {
                    // better than the last one we had
                    last_length = it.first.length();
                    selected_handler_func = it.second;
                }
            }            
        }
        if (selected_handler_func) {
            http_request oreq;
            oreq.method(string_view(req_.method_string().data(), req_.method_string().size()));
            oreq.target(string_view(req_.target().data(), req_.target().size()));
            selected_handler_func(move(oreq), [this](http_response &&res) {
                auto sp = make_shared<http::response<http::string_body>>();
                sp->keep_alive(req_.keep_alive());
                sp->result(res.status());
                sp->body() = res.body();
                sp->content_length(res.body().length());
                res_ = sp;
                http::async_write(
                    stream_,
                    *sp,
                    beast::bind_front_handler(
                        &session::on_write,
                        shared_from_this(),
                        sp->need_eof()));
            });
        }
        else {
            // XXX some sort of 404. if we refactor the if branch above into a
            // function that takes the http_server::response, then we could have free
            // functions creating typical responses (404, 500..) and use that
            // here
        }
    }

    void on_write(bool close, beast::error_code ec, size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            return fail(ec, "write");
        }

        if (close) {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return do_close();
        }

        // We're done with the response so delete it
        res_ = nullptr;

        // Read another request
        do_read();
    }

    void do_close() {
        // Send a TCP shutdown
        beast::error_code ec;
        stream_.socket().shutdown(ip::tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }
};

class http_server::listener : public enable_shared_from_this<listener> {
    private:
        io_service& ios_;
        ip::tcp::acceptor acceptor_;
        bool shutdown_;
        list<pair<string,handler_func>> &handlers_;

    public:
        listener(io_service &ios, ip::tcp::endpoint endpoint, list<pair<string,handler_func>> &handlers) :
                ios_(ios),
                acceptor_(make_strand(ios)),
                shutdown_(false),
                handlers_(handlers) {
            beast::error_code ec;

            BOOST_LOG_TRIVIAL(info) << "starting HTTP listener on " << endpoint << "...";

            // Open the acceptor
            acceptor_.open(endpoint.protocol(), ec);
            if (ec) {
                fail(ec, "open");
                return;
            }

            // Allow address reuse
            acceptor_.set_option(socket_base::reuse_address(true), ec);
            if (ec) {
                fail(ec, "set_option");
                return;
            }

            // Bind to the server address
            acceptor_.bind(endpoint, ec);
            if (ec) {
                fail(ec, "bind");
                return;
            }

            // Start listening for connections
            acceptor_.listen(socket_base::max_listen_connections, ec);
            if (ec) {
                fail(ec, "listen");
                return;
            }
        }

        void start() {
            do_accept();
        }

        void shutdown() {
            shutdown_ = true;
            acceptor_.close();
        }

    private:
        void do_accept() {
            // The new connection gets its own strand
            acceptor_.async_accept(
                make_strand(ios_),
                beast::bind_front_handler(
                    &listener::on_accept,
                    shared_from_this()));
        }

        void on_accept(beast::error_code ec, ip::tcp::socket socket) {
            if (shutdown_) {
                return;
            }

            if (ec) {
                fail(ec, "accept");
            }
            else {
                // Create the session and run it
                make_shared<session>(move(socket), handlers_)->run();
            }

            // Accept another connection
            do_accept();
        }
};

http_server::http_server(io_service &ios, unsigned short port) :
    ios_(ios),
    port_(port) {
}

http_server::~http_server() {
}

void http_server::start() {
    listeners_.push_back(make_shared<http_server::listener>(ios_, ip::tcp::endpoint{ip::make_address("::1"), port_}, handlers_));
    listeners_.push_back(make_shared<http_server::listener>(ios_, ip::tcp::endpoint{ip::make_address("0.0.0.0"), port_}, handlers_));
    for (auto l : listeners_) {
        l->start();
    }
}

void http_server::shutdown() {
    BOOST_LOG_TRIVIAL(info) << "shutting down all HTTP listeners...";
    for (auto l : listeners_) {
        l->shutdown();
    }
}

void http_server::register_handler(const string &prefix, handler_func handler) {
    handlers_.emplace_back(prefix, handler);
}
