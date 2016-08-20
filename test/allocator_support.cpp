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
#include "daily/future/future.hpp"

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
        BOOST_TEST_CHECK(count_ == num_allocations);
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

BOOST_AUTO_TEST_CASE( future_alloc_promise )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    typedef int T;
    daily::promise<T> p(std::allocator_arg, alloc);
    daily::future<T> f = p.get_future();
    p.set_value(1);
    BOOST_TEST_CHECK(f.valid() == true);
    BOOST_TEST_CHECK(f.get() == 1);
    BOOST_TEST_CHECK(f.valid() == false);
}

BOOST_AUTO_TEST_CASE( future_alloc_continuation_noncircular )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<float> p(std::allocator_arg, alloc);
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; }, alloc);
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    daily::future<short> f3 = f2.then([](int i) { return (short)(i * 2); }, alloc);
}

BOOST_AUTO_TEST_CASE( future_alloc_continuation_void )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<void> p(std::allocator_arg, alloc);
    daily::future<void> f = p.get_future();
    daily::future<int> f2 = f.then([] { return 2; }, alloc);
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    daily::future<short> f3 = f2.then([](int i) { return (short)(i * 2); }, alloc);
}

BOOST_AUTO_TEST_CASE( future_alloc_continuations_consistent)
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    bool ran = false;
    daily::promise<char>(std::allocator_arg, alloc).get_future().then([&](char){ ran = true; }, alloc);
    BOOST_TEST_CHECK(!ran);
    daily::promise<void>(std::allocator_arg, alloc).get_future().then([&](){ ran = true; }, alloc);
    BOOST_TEST_CHECK(!ran);
    daily::promise<int&>(std::allocator_arg, alloc).get_future().then([&](int&){ ran = true; }, alloc);
    BOOST_TEST_CHECK (!ran);
}

BOOST_AUTO_TEST_CASE( future_alloc_any_continuation )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<float> p(std::allocator_arg, alloc);
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; }, alloc);
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    daily::future<short> f3 = f2.then([](int i) { return (short)(i * 2); }, alloc);
    p.set_value(1.f);
    BOOST_TEST_CHECK(f3.get() == 4);
    f3 = daily::future<short>();
}

BOOST_AUTO_TEST_CASE( future_alloc_get_continuation )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<float> p(std::allocator_arg, alloc);
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; }, alloc);
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool continued = false;
    daily::future<short> f3 = f2.then(daily::continue_on::get, 
        [&continued](int i)
        {
            continued = true;
            return (short)(i * 2); 
        },
        alloc
    );
    p.set_value(1.f);
    BOOST_TEST_CHECK(continued == false);
    BOOST_TEST_CHECK(f3.get() == 4);
    BOOST_TEST_CHECK(continued == true);
}

BOOST_AUTO_TEST_CASE( future_alloc_get_chain_continuation )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<float> p(std::allocator_arg, alloc);
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then(daily::continue_on::get, 
        [](float f) { return (int)f * 2; },
        alloc
    );

    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool continued = false;
    daily::future<short> f3 = f2.then(daily::continue_on::get, 
        [&continued](int i)
        {
            continued = true;
            return (short)(i * 2); 
        },
        alloc
    );
    p.set_value(1.f);
    BOOST_TEST_CHECK(continued == false);
    BOOST_TEST_CHECK(f3.get() == 4);
    BOOST_TEST_CHECK(continued == true);
}

BOOST_AUTO_TEST_CASE( future_alloc_set_continuation )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<float> p(std::allocator_arg, alloc);
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then([](float f) { return (int)f * 2; }, alloc);
    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool continued = false;
    daily::future<short> f3 = f2.then(daily::continue_on::set, 
        [&continued](int i)
        {
            continued = true;
            return (short)(i * 2); 
        },
        alloc
    );
    p.set_value(1.f);
    BOOST_TEST_CHECK(continued == true);
    BOOST_TEST_CHECK(f3.get() == 4);
}

BOOST_AUTO_TEST_CASE( future_alloc_set_chain_continuation )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<float> p(std::allocator_arg, alloc);
    daily::future<float> f = p.get_future();
    daily::future<int> f2 = f.then(daily::continue_on::set, 
        [](float f) { return (int)f * 2; }, alloc);

    BOOST_TEST_CHECK(f2.valid() == true);
    BOOST_TEST_CHECK(f.valid() == false);
    bool continued = false;
    daily::future<short> f3 = f2.then(daily::continue_on::set, 
        [&continued](int i)
        {
            continued = true;
            return (short)(i * 2); 
        },
        alloc
    );
    p.set_value(1.f);
    BOOST_TEST_CHECK(continued == true);
    BOOST_TEST_CHECK(f3.get() == 4);
}

BOOST_AUTO_TEST_CASE( future_alloc_get_set_chain_continuation )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<float> p(std::allocator_arg, alloc);
    daily::future<float> f = p.get_future();
    bool get_ran = false;
    daily::future<int> f2 = f.then(
        daily::continue_on::get, 
        [&get_ran](float f) 
        {
            get_ran = true;
            return (int)f * 2; 
        },
        alloc
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
        },
        alloc
    );
    BOOST_TEST_CHECK(get_ran == false);
    p.set_value(1.f);
    BOOST_TEST_CHECK(get_ran == false);
    BOOST_TEST_CHECK(f3.get() == 4);
    BOOST_TEST_CHECK(get_ran == true);
    BOOST_TEST_CHECK(set_ran == true);
}

BOOST_AUTO_TEST_CASE( future_alloc_set_get_chain_continuation )
{
    LinearAllocator<char> alloc;
    CheckAllocations check;
    daily::promise<float> p(std::allocator_arg, alloc);
    daily::future<float> f = p.get_future();
    bool set_ran = false;
    daily::future<int> f2 = f.then(
        daily::continue_on::set, 
        [&set_ran](float f) 
        {
            set_ran = true;
            return (int)f * 2; 
        },
        alloc
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
        },
        alloc
    );
    BOOST_TEST_CHECK(set_ran == false);
    p.set_value(1.f);
    BOOST_TEST_CHECK(set_ran == true);
    BOOST_TEST_CHECK(get_ran == false);
    BOOST_TEST_CHECK(f3.get() == 4);
    BOOST_TEST_CHECK(get_ran == true);
}