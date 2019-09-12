#include "server.h"

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

beast::string_view mime_type(beast::string_view path) {
    using beast::iequals;
    auto const ext = [&path] {
        auto const pos = path.rfind(".");
        if (pos == beast::string_view::npos) {
            return beast::string_view{};
        }
        return path.substr(pos);
    }();

    if (iequals(ext, ".htm"))  return "text/html";
    if (iequals(ext, ".html")) return "text/html";
    if (iequals(ext, ".php"))  return "text/html";
    if (iequals(ext, ".css"))  return "text/css";
    if (iequals(ext, ".txt"))  return "text/plain";
    if (iequals(ext, ".js"))   return "application/javascript";
    if (iequals(ext, ".json")) return "application/json";
    if (iequals(ext, ".xml"))  return "application/xml";
    if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
    if (iequals(ext, ".flv"))  return "video/x-flv";
    if (iequals(ext, ".png"))  return "image/png";
    if (iequals(ext, ".jpe"))  return "image/jpeg";
    if (iequals(ext, ".jpeg")) return "image/jpeg";
    if (iequals(ext, ".jpg"))  return "image/jpeg";
    if (iequals(ext, ".gif"))  return "image/gif";
    if (iequals(ext, ".bmp"))  return "image/bmp";
    if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
    if (iequals(ext, ".tiff")) return "image/tiff";
    if (iequals(ext, ".tif"))  return "image/tiff";
    if (iequals(ext, ".svg"))  return "image/svg+xml";
    if (iequals(ext, ".svgz")) return "image/svg+xml";
    return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
string path_cat(beast::string_view base, beast::string_view path) {
    if (base.empty()) {
        return string(path);
    }
    string result(base);
    char constexpr path_separator = '/';
    if (result.back() == path_separator) {
        result.resize(result.size() - 1);
    }
    result.append(path.data(), path.size());
    return result;
}

// XXX replace this template madness and the "generic lamda" below with
// something simpler that can be passed into a router
//
// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<class Body, class Allocator, class Send> 
void handle_request(beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>> &&req,
    Send&& send) {
    // Returns a bad request response
    auto const bad_request = [&req](beast::string_view why) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = string(why);
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    auto const not_found = [&req](beast::string_view target) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const server_error = [&req](beast::string_view what) {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + string(what) + "'";
        res.prepare_payload();
        return res;
    };

    // Make sure we can handle the method
    if (req.method() != http::verb::get && req.method() != http::verb::head) {
        return send(bad_request("Unknown HTTP-method"));
    }

    // Request path must be absolute and not contain "..".
    if (req.target().empty() || req.target()[0] != '/' || req.target().find("..") != beast::string_view::npos) {
        return send(bad_request("Illegal request-target"));
    }

    // Build the path to the requested file
    string path = path_cat(doc_root, req.target());
    if (req.target().back() == '/') {
        path.append("index.html");
    }

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if (ec == beast::errc::no_such_file_or_directory) {
        return send(not_found(req.target()));
    }

    // Handle an unknown error
    if (ec) {
        return send(server_error(ec.message()));
    }

    // Cache the size since we need it after the move
    auto const size = body.size();

    // Respond to HEAD request
    if (req.method() == http::verb::head) {
        http::response<http::empty_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return send(move(res));
    }

    // Respond to GET request
    http::response<http::file_body> res{
        piecewise_construct,
        make_tuple(move(body)),
        make_tuple(http::status::ok, req.version())};
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return send(move(res));
}

class session : public enable_shared_from_this<session> {
    // This is the C++11 equivalent of a generic lambda.
    // The function object is used to send an HTTP message.
    struct send_lambda {
        session& self_;

        explicit send_lambda(session& self) :
                self_(self) {
        }

        template<bool isRequest, class Body, class Fields>
        void operator()(http::message<isRequest, Body, Fields>&& msg) const {
            // The lifetime of the message has to extend
            // for the duration of the async operation so
            // we use a shared_ptr to manage it.
            auto sp = make_shared<http::message<isRequest, Body, Fields>>(move(msg));

            // Store a type-erased version of the shared
            // pointer in the class to keep it alive.
            self_.res_ = sp;

            // Write the response
            http::async_write(
                self_.stream_,
                *sp,
                beast::bind_front_handler(
                    &session::on_write,
                    self_.shared_from_this(),
                    sp->need_eof()));
        }
    };

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    shared_ptr<void> res_;
    send_lambda lambda_;

public:
    // Take ownership of the stream
    session(ip::tcp::socket &&socket) :
            stream_(move(socket)),
            lambda_(*this) {
    
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

        // Send the response
        // XXX what's the doc_root first arg for?
        handle_request("", move(req_), lambda_);
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

class server::listener : public enable_shared_from_this<listener> {
    private:
        io_service& ios_;
        ip::tcp::acceptor acceptor_;
        bool shutdown_;

    public:
        listener(io_service &ios, ip::tcp::endpoint endpoint) :
                ios_(ios),
                acceptor_(make_strand(ios)),
                shutdown_(false) {
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
                make_shared<session>(move(socket))->run();
            }

            // Accept another connection
            do_accept();
        }
};

server::server(io_service &ios, unsigned short port) :
    ios_(ios),
    port_(port) {
}

server::~server() {
}

void server::start() {
    listeners_.push_back(make_shared<server::listener>(ios_, ip::tcp::endpoint{ip::make_address("::1"), port_}));
    listeners_.push_back(make_shared<server::listener>(ios_, ip::tcp::endpoint{ip::make_address("0.0.0.0"), port_}));
    for (auto l : listeners_) {
        l->start();
    }
}

void server::shutdown() {
    BOOST_LOG_TRIVIAL(info) << "shutting down all HTTP listeners...";
    for (auto l : listeners_) {
        l->shutdown();
    }
}

