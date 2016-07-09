// ****************************************************************************
// daily/future/future.hpp
//
// An extension of std::future that allows continuation via ::dispatch, 
// ::post and ::defer.
// 
// Copyright Chris Glover 2016
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt
//
// ****************************************************************************
#pragma once
#ifndef DAILY_FUTURE_FUTURE_HPP_
#define DAILY_FUTURE_FUTURE_HPP_

#include <future>
#include <functional>

// -----------------------------------------------------------------------------
//
namespace daily
{
	template<typename Result>
	class future;

	template<typename Result>
	class promise
	{
	public:
		promise()
		{}

		template<typename Alloc>
		promise(std::allocator_arg_t, Alloc const& alloc)
			: promise_(std::allocator_arg, alloc)
		{}

		// move support
		promise(promise&& other) noexcept
			: promise_(std::move(other.promise_))
		{}

		promise& operator=(promise&& other) noexcept
		{
			promise_ = std::move(other.promise_);
			return *this;
		}

		// no copy
		promise(promise const& other) = delete;
		promise& operator=(promise const& rhs) = delete;

		void swap(promise& other) noexcept
		{
			std::swap(promise_, other.promise_);
		}
		

		future<Result> get_future()
		{
			return future<Result>(promise_.get_future());
		}

		// Use a vararg here to avoid having to specialize the whole class for
		// lvalue ref anf void.
		template<typename... R>
		void set_value(R&&... value)
		{
			static_assert(sizeof...(value) < 2, "set_value must be called with exactly 0 or 1 argument");
			promise_.set_value(std::forward<R>(value)...);
		}

		template<typename... R>
		void set_value_at_thread_exit(R&&... value)
		{
			static_assert(sizeof...(value) < 2, "set_value must be called with exactly 0 or 1 argument");
			promise_.set_value_at_thread_exit(std::forward<R>(value)...);
		}

		void set_exception(std::exception_ptr p)
		{
			promise_.set_exception(std::move(p));
		}

		void set_exception_at_thread_exit(std::exception_ptr p)
		{
			promise_.set_exception_at_thread_exit(std::move(p));
		}

	private:

		std::promise<Result> promise_;
	};

	template<typename Result>
	void swap(promise<Result>& lhs, promise<Result>& rhs) noexcept
	{
		lhs.swap(rhs);
	}

	// -------------------------------------------------------------------------
	//
	template<typename Result>
	class future
	{
	public:

		future() noexcept
		{}

		// move support
		future(future&& other) noexcept
			: future_(std::move(other.future_))
		{}

		future& operator=(future&& other) noexcept
		{
			future_ = std::move(other.future_);
			return *this;
		}

		// no copy
		future(future const& other) = delete;
		future& operator=(future const& other) = delete;

		Result get()
		{
			return future_.get();
		}

		bool valid() const noexcept
		{
			return future_.valid();
		}

	    void wait() const
	    {
	    	return future_.wait();
	    }

	    template <typename Rep, typename Period>
	    std::future_status wait_for(std::chrono::duration<Rep, Period> const& rel_time) const
	    {
	    	return future_.wait_for(rel_time);
	    }

	    template <typename Clock, typename Duration>
	    std::future_status wait_until(std::chrono::time_point<Clock, Duration> const& abs_time) const
	    {
	    	return future_.wait_until(abs_time);
	    }

	private:

		template<typename>
		friend class promise;
		explicit future(std::future<Result>&& f)
			: future_(std::move(f))
		{}

		std::future<Result> future_;
	};

	// -------------------------------------------------------------------------
	//
	template<typename> class packaged_task; // undefined 

	template<class Result, typename... Args>
	class packaged_task<Result(Args...)>
	{
	 public:

	    packaged_task() noexcept
	    {}

	    template <class F>
	    explicit packaged_task(F&& f)
	    	: func_(std::forward<F>(f))
	    {}

	    template <class F, class Allocator>
	    packaged_task(std::allocator_arg_t, Allocator const& a, F&& f)
	    	: promise_(std::allocator_arg, a)
	    	, func_(std::allocator_arg, a, std::forward<F>(f))
	    {}
	 
	    // no copy
	    packaged_task(packaged_task const&) = delete;
	    packaged_task& operator=(packaged_task const&) = delete;
	 
	    // move support
	    packaged_task(packaged_task&& other) noexcept
	    	: promise_(std::move(other.promise_))
	    	, func_(std::move(other.func_))
	    {}

	    packaged_task& operator=(packaged_task&& rhs) noexcept
	    {
	    	promise_ = std::move(rhs.promise_);
	    	func_ = std::move(rhs.func_);
	    }
	 
	    void swap(packaged_task& other) noexcept
	    {
	    	swap(promise_, other.promise_);
	    	std::swap(func_, other.func_);
	    }

	    bool valid() const noexcept
	    {
	    	return promise_.valid();
	    }
	 
	    // result retrieval
	    future<Result> get_future()
	    {
	    	return promise_.get_future();
	    }
	 
	    // execution
	    void operator()(Args... args)
	    {
	    	promise_.set_value(func_(std::forward<Args>(args)...));
	    }

	    void make_ready_at_thread_exit(Args... args)
	    {
	    	promise_.set_value_at_thread_exit(func_(std::forward<Args>(args)...));
	    }
	 
	    void reset()
	    {
	    	promise_.reset();
	    	func_.clear();
	    }

	private:

		promise<Result> promise_;
		std::function<Result(Args...)> func_;
	};

	template<typename Result, typename... Args>
	void swap(
		packaged_task<Result(Args...)>& lhs, 
        packaged_task<Result(Args...)>& rhs )
	{
		lhs.swap(rhs);
	}
};

// -----------------------------------------------------------------------------
//
namespace std
{
	template<typename Result, typename Alloc>
	struct uses_allocator<daily::promise<Result>, Alloc> 
		: std::uses_allocator<std::promise<Result>, Alloc> 
	{};

	template<typename Signature, typename Alloc>
	struct uses_allocator<daily::packaged_task<Signature>, Alloc> 
		: std::true_type
	{};
}

#endif // DAILY_FUTURE_FUTURE_HPP_