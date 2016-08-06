// ****************************************************************************
// daily/future/test/allocator_support.cpp
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

#include <cstddef>
#include <memory>
#include <atomic>

static std::atomic<std::size_t> num_allocations(0);
static std::atomic<std::size_t> num_frees(0);
void * operator new(size_t size)
{
	++num_allocations;
	return malloc(size);
}
void * operator new[](size_t size)
{
	++num_allocations;
	return malloc(size);
}
void operator delete(void * ptr) noexcept
{
	if (ptr)
	{
		++num_frees;
		free(ptr);
	}
}
void operator delete[](void * ptr) noexcept
{
	if (ptr)
	{
		++num_frees;
		free(ptr);
	}
}
struct CheckAllocations
{
    std::size_t count_;
    CheckAllocations()
        : count_(num_allocations)
    {}

    ~CheckAllocations()
    {
        //BOOST_TEST_CHECK(count_ == num_allocations);
    }
};

namespace {

    struct Buffer
    {
		Buffer()
			: head_(memory_)
		{}
		
        char memory_[10 * 1024 * 1024];
        char* head_;
    };

    template <class T>
    struct LinearAllocator
    {
        typedef T value_type;

        LinearAllocator()
            : buffer_(std::make_shared<Buffer>())
        {}

        template <class U> 
        LinearAllocator(LinearAllocator<U> const& other)
            : buffer_(other.buffer_)
        {}

        T* allocate(std::size_t n)
        {
            T* ret_val = reinterpret_cast<T*>(buffer_->head_);
            buffer_->head_ += ((sizeof(T) * n) + 7) & ~7;
            assert(buffer_->head_ <= std::end(buffer_->memory_));
			return ret_val;

        }
        void deallocate(T* p, std::size_t n)
        {
            // no-op
        }

		template< class U > struct rebind { typedef LinearAllocator<U> other; };

    private:

		template <typename>
		friend struct LinearAllocator;

        std::shared_ptr<Buffer> buffer_;
    };
}

template<typename Function, typename Allocator>
struct alloced_handler
{
	typedef Allocator alloc_type;
	
	alloced_handler(Function f, Allocator alloc)
		: function_(std::move(f))
		, allocator_(std::move(alloc))
	{}

	template<typename... Args>
	auto operator()(Args... args)
	{
		return function_(std::forward<Args>(args)...);
	}

	Function function_;
	Allocator allocator_;
};

template<typename Function, typename Allocator> 
alloced_handler<Function, Allocator> make_alloced_handler(Function&& f, Allocator a)
{
	return alloced_handler<Function, typename std::decay<Allocator>::type>(std::forward<Function>(f), a);
}

float get_one()
{
	return 1.f;
}

BOOST_AUTO_TEST_CASE( future_use_future_basic )
{
	std::experimental::thread_pool pool;
	LinearAllocator<char> alloc;
    CheckAllocations check;
	auto f = std::experimental::dispatch(
		pool,
		make_alloced_handler(get_one, alloc),
		daily::use_future_t<LinearAllocator<char>>(alloc)
	);

	BOOST_TEST_CHECK(f.valid() == true);
	auto f2 = f.then(
        daily::continue_on::get, 
        make_alloced_handler([](float f) { return f * 2; }, alloc),
        alloc
    );

	BOOST_TEST_CHECK(f.valid() == false);
	BOOST_TEST_CHECK(f2.get() == 2.f);
}

BOOST_AUTO_TEST_CASE( future_use_future_throw )
{
	std::experimental::thread_pool pool;
	LinearAllocator<char> alloc;
    CheckAllocations check;

	auto f = std::experimental::dispatch(
		pool,
		get_one,
		daily::use_future_t<LinearAllocator<char>>(alloc)
	);

	BOOST_TEST_CHECK(f.valid() == true);
	auto f2 = f.then(
		daily::continue_on::get, 
		make_alloced_handler([](float f)
		{
			throw std::logic_error("");
			return f * 2; 
        }, alloc),
		alloc
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
	LinearAllocator<char> alloc;
    CheckAllocations check;
    
	auto f = std::experimental::dispatch(
		pool,
		make_alloced_handler(get_one, alloc),
		daily::use_future_t<LinearAllocator<char>>(alloc)
	);

	BOOST_TEST_CHECK(f.valid() == true);
	bool has_run = false;
	f.wait();
	auto f2 = f.then(
		daily::execute::dispatch,
		looper,
		make_alloced_handler([&has_run](float f) 
		{ 
			has_run = true;
			return f * 2.f;
		}, alloc),
       	alloc
	);

	BOOST_TEST_CHECK(f.valid() == false);
	BOOST_TEST_CHECK(has_run == false);
	looper.run();
	BOOST_TEST_CHECK(has_run == true);
	BOOST_TEST_CHECK(f2.get() == 2.f);
}