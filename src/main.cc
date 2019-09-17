#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>

#include "http.h"
#include "version.h"

using namespace std;

static const int MIN_THREADS = 4;

int main(int argc, char **argv) {
    // set up logging
    boost::log::register_simple_formatter_factory< boost::log::trivial::severity_level, char >("Severity");
    boost::log::add_common_attributes();
    boost::log::add_console_log(
        cout,
        boost::log::keywords::format = "[%TimeStamp%] [%Severity%] %Message%",
        boost::log::keywords::auto_flush = true
    );

    // XXX set based on some config (-v or so), but how? circular deps...
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::debug
    );

    BOOST_LOG_TRIVIAL(info) << "DUCKBOARD " << duckboard_version();

    unsigned int cores = boost::thread::hardware_concurrency();
    if (cores < MIN_THREADS) {
        // sometimes this can't be determined, so we pick a safe value
        cores = MIN_THREADS;
    }
    BOOST_LOG_TRIVIAL(debug) << "running with " << cores << " threads...";
    boost::asio::io_service ios(cores);
    boost::asio::signal_set sigs(ios);

    // set up and main subcomponents
    // XXX port from config or cmdline
    http_server srv(ios, 8080); 
  
    // XXX debug stuff only for now
    srv.register_handler("/test", [](http_request &&req, http_server::callback_func cb) {
        BOOST_LOG_TRIVIAL(debug) << "## handling request " << req.method() << " " << req.target();
        http_response res;
        res.status(201);
        res.body("Woohoo!\n\n");
        cb(move(res));
    });

    sigs.add(SIGINT);
    sigs.add(SIGTERM);
    sigs.add(SIGQUIT);
    sigs.async_wait(
        [&sigs, &srv](boost::system::error_code ec, int signo) {
            // XXX does this not need to look at ec and re-wait to deal with spurious wakeups?
            BOOST_LOG_TRIVIAL(debug) << "signal caught, shutting down...";
            // stop main subcomponents and clean up
            srv.shutdown();
        }
    );

    // start subcomponents
    srv.start();

    // set up thread pool
    boost::thread_group threadpool;
    for (unsigned int i = 0; i < (cores-1); ++i) {
        threadpool.create_thread(
            [&ios]() { 
                ios.run(); 
            }
        );
    }
    // main thread joins as well, this will block until no more work in ios
    ios.run();

    // wait for other threads to shut down
    threadpool.join_all();
    BOOST_LOG_TRIVIAL(info) << "clean shutdown";

    return 0;
}
