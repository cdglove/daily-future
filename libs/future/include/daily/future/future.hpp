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

#include <boost/optional.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>

// -----------------------------------------------------------------------------
//
namespace daily
{
	template<typename Result>
	class future;

	template<typename Result>
	class promise;

	enum class future_status
	{
		ready,
		timeout,
		//deferred
	};

	namespace detail
	{
		class reverse_lock
		{
			std::unique_lock<std::mutex>& lock_;

		public:

			reverse_lock(std::unique_lock<std::mutex>& lk)
				: lock_(lk)
			{
				lock_.unlock();
			}

			~reverse_lock()
			{
				if (!lock_.owns_lock())
				{
					lock_.lock();
				}
			}

			void lock()
			{
				if (!lock_.owns_lock())
				{
					lock_.lock();
				}
			}

			reverse_lock& operator=(reverse_lock const&) = delete;
			reverse_lock(reverse_lock const&) = delete;
		};

		class shared_future_state_base
		{
		public:
			// No copying or moving, pointer semantic only.
			shared_future_state_base(shared_future_state_base const&) = delete;
			shared_future_state_base& operator=(shared_future_state_base const&) = delete;
			shared_future_state_base(shared_future_state_base&&) = delete;
			shared_future_state_base& operator=(shared_future_state_base&&) = delete;

			shared_future_state_base()
				: finished_(false)
				, is_valid_(true)
			{}	

			void set_finished(std::unique_lock<std::mutex>&)
			{
				finished_ = true;
				ready_wait_.notify_all();
			}

			void set_finished_with_exception(std::exception_ptr p, std::unique_lock<std::mutex>& lock)
			{
				exception_ = std::move(p);
				set_finished(lock);
			}

			void set_invalid(std::unique_lock<std::mutex>&)
			{
				is_valid_ = false;
			}

			bool is_valid(std::unique_lock<std::mutex>&) const
			{
				return is_valid_;
			}

			void do_wait(std::unique_lock<std::mutex>& lock)
			{
				while(!finished_)
					ready_wait_.wait(lock);

			}

			template <typename Rep, typename Period>
			future_status do_wait_for(std::chrono::duration<Rep, Period> const& rel_time)
			{
				ready_wait_.wait_for(rel_time, lock);
				return finished_ ? future_status::ready : future_status::timeout;
			}

			template <typename Clock, typename Duration>
			future_status do_wait_until(std::chrono::time_point<Clock, Duration> const& abs_time)
			{
				ready_wait_.wait_until(abs_time, lock);
				return finished_ ? future_status::ready : future_status::timeout;
			}

			std::unique_lock<std::mutex> lock() const
			{
				return std::unique_lock<std::mutex>(mutex_);
			}

		private:

			mutable std::mutex mutex_;
			std::exception_ptr exception_;
			std::condition_variable ready_wait_;
			bool finished_;
			bool is_valid_;
		};

		template<typename Result>
		class shared_future_state : public shared_future_state_base
		{
		public:

			typedef boost::optional<Result> storage_type;

			void set_finished_with_result(Result&& r, std::unique_lock<std::mutex>& lock)
			{
				result_ = std::move(r);
				set_finished(lock);
			}

			Result get(std::unique_lock<std::mutex>& lock)
			{
				do_wait(lock);
				set_invalid(lock);
				return *std::move(result_);
			}

		private:

			storage_type result_;
		};

		// ---------------------------------------------------------------------
		//
		template<>
		class shared_future_state<void> : public shared_future_state_base
		{
		public:

			typedef void storage_tyoe;

			void set_finished_with_result(std::unique_lock<std::mutex>& lock)
			{
				set_finished(lock);
			}

			void get(std::unique_lock<std::mutex>& lock)
			{
				do_wait(lock);
				set_invalid(lock);
			}
		};

		// ---------------------------------------------------------------------
		//
		template<typename Result>
		class shared_future_state<Result&> : public shared_future_state_base
		{
		public:

			typedef Result* storage_type;

			shared_future_state()
				: result_(nullptr)
			{}

			void set_finished_with_result(Result& r, std::unique_lock<std::mutex>& lock)
			{
				result_ = &r;
				set_finished(lock);
			}

			Result& get(std::unique_lock<std::mutex>& lock)
			{
				do_wait(lock);
				set_invalid(lock);
				return *result_;
			}

		private:

			storage_type result_;
		};
	}

	template<typename Result>
	class promise
	{
	private:

		typedef detail::shared_future_state<Result> shared_state;

	public:
		promise()
			: state_(std::make_shared<shared_state>())
		{}

		template<typename Alloc>
		promise(std::allocator_arg_t, Alloc const& alloc)
			: state_(std::allocate_shared<shared_state>(alloc))
		{}

		// move support
		promise(promise&& other) noexcept
			: state_(nullptr)
		{
			std::swap(state_, other.state_);
		}

		promise& operator=(promise&& other) noexcept
		{
			state_ = std::move(other.state_);
			return *this;
		}

		// no copy
		promise(promise const& other) = delete;
		promise& operator=(promise const& rhs) = delete;

		void swap(promise& other) noexcept
		{
			std::swap(state_, other.state_);
		}
		
		future<Result> get_future()
		{
			return future<Result>(state_);
		}

		// Use a vararg here to avoid having to specialize the whole class for
		// lvalue ref anf void.
		template<typename... R>
		void set_value(R&&... value)
		{
			static_assert(sizeof...(value) < 2, "set_value must be called with exactly 0 or 1 argument");
			auto lk = state_->lock();
			state_->set_finished_with_result(std::forward<R>(value)..., lk);
		}

		template<typename... R>
		void set_value_at_thread_exit(R&&... value)
		{
			assert(false && "cglover-todo");
		}

		void set_exception(std::exception_ptr p)
		{
			state_->set_finished_with_exception(std::move(p), state_->lock());
		}

		void set_exception_at_thread_exit(std::exception_ptr p)
		{
			assert(false && "cglover-todo");
		}

	private:

		std::shared_ptr<shared_state> state_;
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
	private:

		typedef detail::shared_future_state<Result> shared_state;

	public:

		future() noexcept
		{}

		// move support
		future(future&& other) noexcept
			: state_(std::move(other.state_))
		{}

		future& operator=(future&& other) noexcept
		{
			state_ = std::move(other.state_);
			return *this;
		}

		// no copy
		future(future const& other) = delete;
		future& operator=(future const& other) = delete;

		Result get()
		{
			auto lk = state_->lock();
			return state_->get(lk);
		}

		bool valid() const noexcept
		{
			if(state_)
			{
				auto lk = state_->lock();
				return state_->is_valid(lk);
			}

			return false;
		}

		void wait() const
		{
			auto lk = state_.lock();
			return state_.wait(lk);
		}

		template <typename Rep, typename Period>
		future_status wait_for(std::chrono::duration<Rep, Period> const& rel_time) const
		{
			auto lk = state_->lock();
			return state_.wait_for(rel_time, lk);
		}

		template <typename Clock, typename Duration>
		future_status wait_until(std::chrono::time_point<Clock, Duration> const& abs_time) const
		{
			auto lk = state_->lock();
			return state_.wait_until(abs_time, lk);
		}

	private:

		template<typename>
		friend class promise;

		explicit future(std::shared_ptr<shared_state> ss)
			: state_(std::move(ss))
		{}

		std::shared_ptr<shared_state> state_;
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
	struct uses_allocator<daily::promise<Result>, Alloc> : std::true_type
	{};

	template<typename Signature, typename Alloc>
	struct uses_allocator<daily::packaged_task<Signature>, Alloc> : std::true_type
	{};
}

#endif // DAILY_FUTURE_FUTURE_HPP_