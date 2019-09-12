#include "server.h"

#include <boost/log/trivial.hpp>

using namespace std;
using namespace boost::asio;
using namespace boost::posix_time;

server::server(io_service &ios) :
    ios_(ios) {
}

server::~server() {
}

void server::start() {
}

void server::stop() {
}

