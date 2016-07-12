#define BOOST_TEST_MODULE Future
#include <boost/test/unit_test.hpp>
#include "daily/future/future.hpp"

BOOST_AUTO_TEST_CASE( future_initial_state )
{
	daily::future<int> f;
	BOOST_TEST_CHECK(f.valid() == false);
	daily::promise<int> p;
	f = p.get_future();
	BOOST_TEST_CHECK(f.valid() == true);
}

BOOST_AUTO_TEST_CASE( promise_future_communication )
{
	daily::promise<int> p;
	daily::future<int> f = p.get_future();
	p.set_value(1);
	BOOST_TEST_CHECK(f.valid() == true);
	BOOST_TEST_CHECK(f.get() == 1);
}

BOOST_AUTO_TEST_CASE( promise_move_semantics )
{
	daily::promise<int> p;
	daily::future<int> f = p.get_future();
	daily::promise<int> p2 = std::move(p);
	p2.set_value(2);
	BOOST_TEST_CHECK(f.get() == 2);
}

BOOST_AUTO_TEST_CASE( future_move_semantics )
{
	daily::promise<int> p;
	daily::future<int> f = p.get_future();
	daily::future<int> f2 = std::move(f);
	p.set_value(3);
	BOOST_TEST_CHECK(f2.get() == 3);
	BOOST_TEST_CHECK(f.valid() == false);
}

BOOST_AUTO_TEST_CASE( future_invalidation )
{
	daily::promise<int> p;
	daily::future<int> f = p.get_future();
	p.set_value(4);
	BOOST_TEST_CHECK(f.get() == 4);
	BOOST_TEST_CHECK(f.valid() == false);
}

BOOST_AUTO_TEST_CASE( void_future )
{
	daily::promise<void> p;
	daily::future<void> f = p.get_future();
	p.set_value();
	f.get();
	BOOST_TEST_CHECK(f.valid() == false);
}

BOOST_AUTO_TEST_CASE( ref_future )
{
	daily::promise<int&> p;
	daily::future<int&> f = p.get_future();
	int x = 5;
	p.set_value(x);
	int y = f.get();
	BOOST_TEST_CHECK(x == y);
}

BOOST_AUTO_TEST_CASE( packaged_task )
{
	daily::packaged_task<int(int)> pt([](int i) { return i * 2; });
	daily::future<int> f = pt.get_future();
	pt(5);
	BOOST_TEST_CHECK(f.get() == 10);
} 

BOOST_AUTO_TEST_CASE( future_continuation )
{
	// daily::packaged_task<int(int)> pt([](int i) { return i * 2; });
	// daily::future<int> f = pt.get_future();
	// daily::future<float> f2 = f.then([](daily::future<int> i)
	// {
	// 	return i.get() * 2.f;
	// });
	// BOOST_TEST_CHECK(f.valid() == false);

	// daily::future<std::string> f3 = f2.then([](daily::future<float> f)
	// {
	// 	return std::to_string(f.get());
	// });

	// pt(5);

	// BOOST_TEST_CHECK(f3.get() == "20");
} 