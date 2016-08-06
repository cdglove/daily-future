// ****************************************************************************
// daily/future/test/use_future.cpp
//
// Copyright Chris Glover 2016
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt
//
// ****************************************************************************
#define BOOST_TEST_MODULE Future
#include <boost/test/unit_test.hpp>

#include <experimental/executor>
#include <experimental/thread_pool>
#include <experimental/loop_scheduler>

#include "daily/future/use_future.hpp"

float get_one()
{
    return 1.f;
}

BOOST_AUTO_TEST_CASE( future_use_future_basic )
{
    std::experimental::thread_pool pool;
    auto f = std::experimental::dispatch(
        pool,
        get_one,
        daily::use_future
    );

    BOOST_TEST_CHECK(f.valid() == true);
    auto f2 = f.then(daily::continue_on::get, [](float f) { return f * 2; });
    BOOST_TEST_CHECK(f.valid() == false);
    BOOST_TEST_CHECK(f2.get() == 2.f);
}

BOOST_AUTO_TEST_CASE( future_use_future_throw )
{
    std::experimental::thread_pool pool;
    auto f = std::experimental::dispatch(
        pool,
        get_one,
        daily::use_future
    );

    BOOST_TEST_CHECK(f.valid() == true);
    auto f2 = f.then(
        daily::continue_on::get, 
        [](float f)
        {
            throw std::logic_error("");
            return f * 2; 
        }
    );
    BOOST_TEST_CHECK(f.valid() == false);
    
    bool exception_caught = false;
    try
    {
        f2.get();
    }
    catch(std::logic_error&)
    {
        exception_caught = true;
    }
    BOOST_TEST_CHECK(exception_caught == true);
    BOOST_TEST_CHECK(f2.valid() == false);
}

BOOST_AUTO_TEST_CASE( future_use_future_executor )
{
    std::experimental::thread_pool pool;
    std::experimental::loop_scheduler looper;
    auto f = std::experimental::dispatch(
        pool,
        get_one,
        daily::use_future
    );

    BOOST_TEST_CHECK(f.valid() == true);
    bool has_run = false;
    f.wait();
    auto f2 = f.then(
        daily::execute::dispatch,
        looper,
        [&has_run](float f) 
        { 
            has_run = true;
            return f * 2.f;
        }
    );

    BOOST_TEST_CHECK(f.valid() == false);
    BOOST_TEST_CHECK(has_run == false);
    looper.run();
    BOOST_TEST_CHECK(has_run == true);
    BOOST_TEST_CHECK(f2.get() == 2.f);
}

BOOST_AUTO_TEST_CASE( future_use_future_stress )
{
    std::experimental::thread_pool pool;
    int count = 10000;
    std::atomic<float> result(10000);
    while(count--)
    {
        auto f = std::experimental::dispatch(
            pool,
            get_one,
            daily::use_future
        );

        f.then(
            daily::execute::dispatch,
            pool,
            [&result](float f) 
            { 
                auto current = result.load();
                while (!result.compare_exchange_weak(current, current - f))
                    ;
            }
        );
    }

    pool.join();
    BOOST_TEST_CHECK(result.load() == 0.f);
}