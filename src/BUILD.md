Building
========

As usual for C++, the build is a bit more involved than desirable. This document
tries to document some of this, please amend as required.

Dependencies
------------

Some of these may be less strict than stated here, but this is what it has been
tested with. Please extend version ranges if you discover working sombinations.

* gcc-g++ 8.3.0
* boost 1.71.0 

### Building a local boost

At the time of this writing, boost 1.71.0 was not yet available on my OS, and
older versions did not have a working beast http server component. So this is
currently using a local version of boost and builds against that. The way I
build boost is (all in src, this directory):

    wget https://dl.bintray.com/boostorg/release/1.71.0/source/boost_1_71_0.tar.bz2
    tar xvfj boost_1_71_0.tar.bz2 
    cd boost_1_71_0/
    mkdir ../olib
    ./bootstrap.sh --prefix=../olib
    ./b2 install

The makefile for this software itself will then pick up boost headers and
libraries from the olib directory.
