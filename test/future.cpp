// ****************************************************************************
// daily/future/test/future.cpp
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
#include <boost/fusion/container/vector.hpp>
#include <boost/fusion/algorithm/iteration/for_each.hpp>
#include <boost/fusion/container/vector/convert.hpp>
#include <boost/fusion/adapted/mpl.hpp>
#include <boost/mpl/transform.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/thread/executors/thread_executor.hpp>
#include "daily/future/future.hpp"
#include <thread>

template<typename T>
struct make_future
{
    typedef daily::future<T> type;
};

template<typename T>
struct make_promise
{
    typedef daily::promise<T> type;
};

// Types for all specializations.
typedef boost::mpl::vector<
    int, 
    int&, 
    void
> all_types;

typedef boost::fusion::result_of::as_vector<
    typename boost::mpl::transform<all_types, make_future<boost::mpl::_1>>::type
>::type future_types;

typedef boost::fusion::result_of::as_vector<
    typename boost::mpl::transform<all_types, make_promise<boost::mpl::_1>>::type
>::type promise_types;

BOOST_AUTO_TEST_CASE( future_initial_state )
{
    boost::fusion::for_each(
        future_types(),
        [](auto&& f)
        {
            BOOST_TEST_CHECK(f.valid() == false);
        }
    );
}

BOOST_AUTO_TEST_CASE( promise_initial_state )
{
    promise_types promises;
    boost::fusion::for_each(
        promises,
        [](auto&& p)
        {
            auto f = p.get_future();
            BOOST_TEST_CHECK(f.valid() == true);
        }
    );
}

BOOST_AUTO_TEST_CASE( promise_future_communication )
{
    {
        typedef int T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        p.set_value(1);
        BOOST_TEST_CHECK(f.valid() == true);
        BOOST_TEST_CHECK(f.get() == 1);
        BOOST_TEST_CHECK(f.valid() == false);
    }

    {
        typedef int& T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        int result = 1;
        p.set_value(result);
        BOOST_TEST_CHECK(f.valid() == true);
        BOOST_TEST_CHECK(&f.get() == &result);
        BOOST_TEST_CHECK(f.valid() == false);
    }

    {
        typedef void T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        p.set_value();
        BOOST_TEST_CHECK(f.valid() == true);
        f.get();
        BOOST_TEST_CHECK(f.valid() == false);
    }
}

BOOST_AUTO_TEST_CASE( promise_move_semantics )
{
    {
        typedef int T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        daily::promise<T> p2 = std::move(p);
        p2.set_value(1);
        BOOST_TEST_CHECK(f.valid() == true);
        BOOST_TEST_CHECK(f.get() == 1);
        BOOST_TEST_CHECK(f.valid() == false);
    }

    {
        typedef int& T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        daily::promise<T> p2 = std::move(p);
        int result = 1;
        p2.set_value(result);
        BOOST_TEST_CHECK(f.valid() == true);
        BOOST_TEST_CHECK(&f.get() == &result);
        BOOST_TEST_CHECK(f.valid() == false);
    }

    {
        typedef void T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        daily::promise<T> p2 = std::move(p);
        p2.set_value();
        BOOST_TEST_CHECK(f.valid() == true);
        f.get();
        BOOST_TEST_CHECK(f.valid() == false);
    }
}

BOOST_AUTO_TEST_CASE( future_move_semantics )
{
    {
        typedef int T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        daily::future<T> f2 = std::move(f);
        BOOST_TEST_CHECK(f2.valid() == true);
        BOOST_TEST_CHECK(f.valid() == false);
        p.set_value(3);
        BOOST_TEST_CHECK(f2.get() == 3);
        BOOST_TEST_CHECK(f2.valid() == false);
    }

    {
        typedef int& T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        daily::future<T> f2 = std::move(f);
        BOOST_TEST_CHECK(f2.valid() == true);
        BOOST_TEST_CHECK(f.valid() == false);
        int result = 3;
        p.set_value(result);
        BOOST_TEST_CHECK(f2.get() == 3);
        BOOST_TEST_CHECK(f2.valid() == false);
    }

    {
        typedef void T;
        daily::promise<T> p;
        daily::future<T> f = p.get_future();
        daily::future<T> f2 = std::move(f);
        BOOST_TEST_CHECK(f2.valid() == true);
        BOOST_TEST_CHECK(f.valid() == false);
        p.set_value();
        f2.get();
        BOOST_TEST_CHECK(f2.valid() == false);
    }
}

BOOST_AUTO_TEST_CASE( future_continuation_noncircular )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; });
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    daily::future<short> f3 = f2.then([](int i) { return (short)(i * 2); });
}

BOOST_AUTO_TEST_CASE( future_continuation_void )
{
    daily::promise<void> p;
    daily::future<void> f = p.get_future();
    daily::future<int> f2 = f.then([] { return 2; });
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    daily::future<short> f3 = f2.then([](int i) { return (short)(i * 2); });
}

BOOST_AUTO_TEST_CASE( future_continuations_consistent)
{
    bool ran = false;
    daily::promise<char>().get_future().then([&](char){ ran = true; });
    BOOST_TEST_CHECK(!ran);
    daily::promise<void>().get_future().then([&](){ ran = true; });
    BOOST_TEST_CHECK(!ran);
    daily::promise<int&>().get_future().then([&](int&){ ran = true; });
    BOOST_TEST_CHECK (!ran);
}

BOOST_AUTO_TEST_CASE( future_any_continuation )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; });
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    daily::future<short> f3 = f2.then([](int i) { return (short)(i * 2); });
    p.set_value(1.f);
    BOOST_TEST_CHECK(f3.get() == 4);
    f3 = daily::future<short>();
}

BOOST_AUTO_TEST_CASE( future_get_continuation )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; });
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool continued = false;
    daily::future<short> f3 = f2.then(daily::continue_on::get, 
        [&continued](int i)
        {
            continued = true;
            return (short)(i * 2); 
        }
    );
    p.set_value(1.f);
    BOOST_TEST_CHECK(continued == false);
    BOOST_TEST_CHECK(f3.get() == 4);
    BOOST_TEST_CHECK(continued == true);
}

BOOST_AUTO_TEST_CASE( future_get_chain_continuation )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then(daily::continue_on::get, 
        [](float f) { return (int)f * 2; });

    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool continued = false;
    daily::future<short> f3 = f2.then(daily::continue_on::get, 
        [&continued](int i)
        {
            continued = true;
            return (short)(i * 2); 
        }
    );
    p.set_value(1.f);
    BOOST_TEST_CHECK(continued == false);
    BOOST_TEST_CHECK(f3.get() == 4);
    BOOST_TEST_CHECK(continued == true);
}

BOOST_AUTO_TEST_CASE( future_set_continuation )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; });
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool continued = false;
    daily::future<short> f3 = f2.then(daily::continue_on::set, 
        [&continued](int i)
        {
            continued = true;
            return (short)(i * 2); 
        }
    );
    p.set_value(1.f);
    BOOST_TEST_CHECK(continued == true);
    BOOST_TEST_CHECK(f3.get() == 4);
}

BOOST_AUTO_TEST_CASE( future_set_chain_continuation )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then(daily::continue_on::set, 
        [](float f) { return (int)f * 2; });

    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool continued = false;
    daily::future<short> f3 = f2.then(daily::continue_on::set, 
        [&continued](int i)
        {
            continued = true;
            return (short)(i * 2); 
        }
    );
    p.set_value(1.f);
    BOOST_TEST_CHECK(continued == true);
    BOOST_TEST_CHECK(f3.get() == 4);
}

BOOST_AUTO_TEST_CASE( future_get_set_chain_continuation )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    bool get_ran = false;
    daily::future<int> f2 = f.then(
        daily::continue_on::get, 
        [&get_ran](float f) 
        {
            get_ran = true;
            return (int)f * 2; 
        }
    );

    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool set_ran = false;
    daily::future<short> f3 = f2.then(
        daily::continue_on::set, 
        [&set_ran](int i)
        {
            set_ran = true;
            return (short)(i * 2); 
        }
    );
    BOOST_TEST_CHECK(get_ran == false);
    p.set_value(1.f);
    BOOST_TEST_CHECK(get_ran == false);
    BOOST_TEST_CHECK(f3.get() == 4);
    BOOST_TEST_CHECK(get_ran == true);
    BOOST_TEST_CHECK(set_ran == true);
}

BOOST_AUTO_TEST_CASE( future_set_get_chain_continuation )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    bool set_ran = false;
    daily::future<int> f2 = f.then(
        daily::continue_on::set, 
        [&set_ran](float f) 
        {
            set_ran = true;
            return (int)f * 2; 
        }
    );

    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool get_ran = false;
    daily::future<short> f3 = f2.then(
        daily::continue_on::get, 
        [&get_ran](int i)
        {
            get_ran = true;
            return (short)(i * 2); 
        }
    );
    BOOST_TEST_CHECK(set_ran == false);
    p.set_value(1.f);
    BOOST_TEST_CHECK(set_ran == true);
    BOOST_TEST_CHECK(get_ran == false);
    BOOST_TEST_CHECK(f3.get() == 4);
    BOOST_TEST_CHECK(get_ran == true);
}

BOOST_AUTO_TEST_CASE( future_multithread )
{
    boost::executors::thread_executor pool;
    int repeat = 100;
    while(repeat--)
    {
        daily::promise<float> p;
        daily::future<float> f = p.get_future();
        pool.submit([&p, repeat] { p.set_value((float)repeat); });
        BOOST_TEST_CHECK(f.get() == repeat);
    }	
}

BOOST_AUTO_TEST_CASE( future_multithread_get_continuation )
{
    boost::executors::thread_executor pool;
    int repeat = 100;
    while(repeat--)
    {
        daily::promise<int> p;
        daily::future<int> f = p.get_future();
        pool.submit([&p, repeat] { p.set_value(repeat); });
        auto f2 = f.then(daily::continue_on::get, [](int v) { return v * 2; });
        BOOST_TEST_CHECK(f2.get() == (2 * repeat));
    }	
}

BOOST_AUTO_TEST_CASE( promise_future_get_throws )
{
    // We don't need to test each specialization here because
    // the execption mechanism is in the common implementation.
    typedef int T;
    daily::promise<T> p;
    daily::future<T> f = p.get_future();

    try 
    {
        throw std::logic_error("");
    } 
    catch(std::logic_error&)
    {
        p.set_exception(std::current_exception());
    }

    BOOST_TEST_CHECK(f.valid() == true);

    bool exception_caught = false;
    try
    {
        f.get();
    }
    catch(std::logic_error&)
    {
        exception_caught = true;
    }
    BOOST_TEST_CHECK(exception_caught == true);
    BOOST_TEST_CHECK(f.valid() == false);
}

BOOST_AUTO_TEST_CASE( promise_future_get_continuation_throws )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; });
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    daily::future<short> f3 = f2.then(
        daily::continue_on::get, 
        [](int i)
        {
            throw std::logic_error("");
            return (short)(i * 2); 
        }
    );
    p.set_value(1.f);

    bool exception_caught = false;
    try
    {
        f3.get();
    }
    catch(std::logic_error&)
    {
        exception_caught = true;
    }
    BOOST_TEST_CHECK(exception_caught == true);
}

BOOST_AUTO_TEST_CASE( promise_future_set_continuation_throws )
{
    daily::promise<float> p;
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then(daily::continue_on::set, 
        [](float f) 
        { 
            throw std::logic_error("");
            return (int)f * 2; 
        }
    );

    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    daily::future<short> f3 = f2.then(daily::continue_on::get, 
        [](int i)
        {
            return (short)(i * 2); 
        }
    );

    bool exception_caught = false;
    try
    {
        p.set_value(1.f);
    }
    catch(std::logic_error&)
    {
        exception_caught = true;
    }
    BOOST_TEST_CHECK(exception_caught == true);
}

BOOST_AUTO_TEST_CASE(future_wait)
{
    daily::promise<int> promise;
    daily::future<int> future = promise.get_future();
    std::thread run_delayed([&]
    {
        promise.set_value(5);
    });
    BOOST_TEST_CHECK(5 == future.get());
    run_delayed.join();
}

// The following tests were lifted from 
// https://github.com/skarupke/compile_time/blob/master/await/then_future.cpp
// as a few edge cases I missed.
BOOST_AUTO_TEST_CASE(discard_future)
{
    daily::promise<void> promise;
    bool ran = false;
    promise.get_future().then([&ran](){ ran = true; });
    promise.set_value();
    BOOST_TEST_CHECK(ran == true);
}

BOOST_AUTO_TEST_CASE(discard_promise)
{
    daily::future<void> future = daily::promise<void>().get_future();
    bool thrown = false;
    try
    {
        future.get();
    }
    catch(const daily::future_error& error)
    {
        thrown = true;
        BOOST_TEST_CHECK((int)daily::future_errc::broken_promise == (int)error.code());
    }
    BOOST_TEST_CHECK(thrown == true);
}

BOOST_AUTO_TEST_CASE(discard_both)
{
    daily::promise<void>().get_future().then([](){});
}

BOOST_AUTO_TEST_CASE(set_before_get)
{
    daily::promise<void> promise;
    promise.set_value();
    bool ran = false;
    promise.get_future().then([&ran](){ ran = true; });
    BOOST_TEST_CHECK(ran == true);
}

struct MovableFunctor
{
    MovableFunctor()
        : a(new int(5))
    {}

    int operator()()
    {
        return *a;
    }

    std::unique_ptr<int> a;
};

// this test is here mainly to ensure that the code
// compiles with functors that are movable only
BOOST_AUTO_TEST_CASE(daily_future_movable)
{
    daily::promise<void> promise;
    daily::future<int> future = promise.get_future().then(MovableFunctor());
    promise.set_value();
    BOOST_TEST_CHECK(5 == future.get());
}
