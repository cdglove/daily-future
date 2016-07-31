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
#include <boost/throw_exception.hpp>
#include <boost/core/no_exceptions_support.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>

// -----------------------------------------------------------------------------
//
namespace daily
{
	// -------------------------------------------------------------------------
	//
	template<typename Result>
	class future;

	template<typename Result>
	class promise;

	// -------------------------------------------------------------------------
	//
	enum class future_status
	{
		ready,
		timeout,
		//deferred
	};

	enum class future_errc
	{
	    broken_promise,
	    future_already_retrieved,
	    promise_already_satisfied,
	    no_state,
	};

	namespace continue_on
	{
		struct any_t {} constexpr any;
		struct get_t {} constexpr get;
		struct set_t {} constexpr set;
	};

	class future_error : public std::logic_error
	{
	public:

		future_error(future_errc err)
			: logic_error("")
			, error_(err)
		{}

		future_errc code()
		{
			return error_;
		}

	private:

		future_errc error_;
	};

	// -------------------------------------------------------------------------
	//
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

		class future_shared_state_base
		{
		public:
			// No copying or moving, pointer semantic only.
			future_shared_state_base(future_shared_state_base const&) = delete;
			future_shared_state_base& operator=(future_shared_state_base const&) = delete;
			future_shared_state_base(future_shared_state_base&&) = delete;
			future_shared_state_base& operator=(future_shared_state_base&&) = delete;

			virtual ~future_shared_state_base()
			{}

			future_shared_state_base()
				: finished_(false)
				, is_valid_(true)
			{}	

			void set_finished(std::unique_lock<std::mutex>& lock)
			{
				finished_ = true;
				ready_wait_.notify_all();
				if(continuation_)
				{
					auto child_lock = continuation_->lock();
					continuation_->continuation_result_ready(lock, child_lock);
				}
			}

			void set_finished_with_exception(std::exception_ptr p, 
											 std::unique_lock<std::mutex>& lock)
			{
				exception_ = std::move(p);
				set_finished(lock);
			}
			
			bool is_finished(std::unique_lock<std::mutex>&)
			{
				return finished_;
			}

			void set_invalid(std::unique_lock<std::mutex>&)
			{
				is_valid_ = false;
			}

			bool is_valid(std::unique_lock<std::mutex>&) const
			{
				return is_valid_;
			}

			void set_continuation(
				std::shared_ptr<future_shared_state_base> continuation, 
				std::unique_lock<std::mutex>& lock)
			{
				continuation_ = std::move(continuation);
				if(finished_)
				{
					continuation_result_requested(lock);
				}
			}

			void do_wait(std::unique_lock<std::mutex>& lock)
			{
				if(!finished_)
					continuation_result_requested(lock);

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

			// Only implemented by continuation derived shared_state types
			void continuation_result_ready(
				std::unique_lock<std::mutex>& parent_lock, 
				std::unique_lock<std::mutex>& this_lock)
			{
				handle_continuation_result_ready(parent_lock, this_lock);
			}

			void continuation_result_requested(std::unique_lock<std::mutex>& this_lock)
			{
				handle_continuation_result_requested(this_lock);
			}

		private:

			// Only implemented by continuation derived shared_state types
			virtual void handle_continuation_result_ready(
				std::unique_lock<std::mutex>&, 
				std::unique_lock<std::mutex>&)
			{}

			virtual void handle_continuation_result_requested(
				std::unique_lock<std::mutex>&)
			{}

			mutable std::mutex mutex_;
			std::exception_ptr exception_;
			std::condition_variable ready_wait_;
			std::shared_ptr<future_shared_state_base> continuation_;
			bool finished_;
			bool is_valid_;
		};

		template<typename Result>
		class future_shared_state : public future_shared_state_base
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
				set_invalid(lock);
				return *std::move(result_);
			}

		private:

			storage_type result_;
		};

		// ---------------------------------------------------------------------
		//
		template<>
		class future_shared_state<void> : public future_shared_state_base
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
		class future_shared_state<Result&> : public future_shared_state_base
		{
		public:

			typedef Result* storage_type;

			future_shared_state()
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

		// ---------------------------------------------------------------------
		// Basic Continuations.
		template<typename Future, typename Result, typename Function>
		class continue_on_basic_shared_state : public future_shared_state<Result>
		{
		public:

			continue_on_basic_shared_state(Future&& parent, Function&& f)
				: parent_(std::move(parent))
				, continuation_(std::move(f))
			{}

			~continue_on_basic_shared_state() = 0;
			
		protected:

			future_shared_state<typename Future::result_type>* get_parent_state()
			{
				return parent_.state_.get();
			}

			void do_continue(
				typename Future::result_type&& result_to_forward, 
				std::unique_lock<std::mutex>& this_lock)
			{
				BOOST_TRY
				{
					Result current_result = continuation_(std::move(result_to_forward));
					this->set_finished_with_result(std::move(current_result), this_lock);
				}
				BOOST_CATCH(...)
				{
					this->set_finished_with_exception(std::current_exception(), this_lock);
				}
				BOOST_CATCH_END
			}

			Future parent_;
			Function continuation_;
		};

		template<typename Future, typename Result, typename Function>
		inline continue_on_basic_shared_state<Future, Result, Function>::~continue_on_basic_shared_state()
		{}

		// ---------------------------------------------------------------------
		//
		template<typename Future, typename Result, typename Function>
		class continue_on_any_shared_state
			: public continue_on_basic_shared_state<Future, Result, Function>
		{
		public:

			using continue_on_basic_shared_state<
				Future, Result, Function
			>::continue_on_basic_shared_state;

		private:

			void handle_continuation_result_ready(
				std::unique_lock<std::mutex>& parent_lock, 
				std::unique_lock<std::mutex>& this_lock) override
			{
				auto parent_state = this->get_parent_state();
				this->do_continue(parent_state->get(parent_lock), this_lock);
			}

			void handle_continuation_result_requested(std::unique_lock<std::mutex>& this_lock) override
			{
				auto parent_state = this->get_parent_state();
				auto parent_lock = parent_state->lock();
				this->do_continue(parent_state->get(parent_lock), this_lock);
			}
		};

		// ---------------------------------------------------------------------
		//
		template<typename Future, typename Result, typename Function>
		class continue_on_set_shared_state 
			: public continue_on_basic_shared_state<Future, Result, Function>
		{
		public:

			using continue_on_basic_shared_state<
				Future, Result, Function
			>::continue_on_basic_shared_state;

		private:

			void handle_continuation_result_ready(
				std::unique_lock<std::mutex>& parent_lock, 
				std::unique_lock<std::mutex>& this_lock) override
			{
				auto parent_state = this->get_parent_state();
				this->do_continue(parent_state->get(parent_lock), this_lock);
			}

			void handle_continuation_result_requested(
				std::unique_lock<std::mutex>& this_lock)
			{
				auto parent_state = this->get_parent_state();
				auto parent_lock = parent_state->lock();
				parent_state->continuation_result_requested(parent_lock);
			}
		};

		// ---------------------------------------------------------------------
		//
		template<typename Future, typename Result, typename Function>
		class continue_on_get_shared_state
			: public continue_on_basic_shared_state<Future, Result, Function>
		{
		public:

			using continue_on_basic_shared_state<
				Future, Result, Function
			>::continue_on_basic_shared_state;

		private:

			void handle_continuation_result_requested(
				std::unique_lock<std::mutex>& this_lock) override
			{
				auto parent_state = this->get_parent_state();
				auto parent_lock = parent_state->lock();
				parent_state->continuation_result_requested(parent_lock);
				this->do_continue(parent_state->get(parent_lock), this_lock);
			}
		};

		// ---------------------------------------------------------------------
		//
		template<typename Result, typename Future, typename Function>
		auto make_continue_on_continuation(continue_on::any_t, Future future, Function&& func)
		{
			return std::make_shared<
				continue_on_any_shared_state<Future, Result, Function>
			>(std::move(future), std::forward<Function>(func));
		}

		template<typename Result, typename Future, typename Function>
		auto make_continue_on_continuation(continue_on::get_t, Future future, Function&& func)
		{
			return std::make_shared<
				continue_on_get_shared_state<Future, Result, Function>
			>(std::move(future), std::forward<Function>(func));
		}

		template<typename Result, typename Future, typename Function>
		auto make_continue_on_continuation(continue_on::set_t, Future future, Function&& func)
		{
			return std::make_shared<
				continue_on_set_shared_state<Future, Result, Function>
			>(std::move(future), std::forward<Function>(func));
		}
	}

	template<typename Result>
	class promise
	{
	private:

		typedef detail::future_shared_state<Result> shared_state;

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
			if(!state_)
			{
				BOOST_THROW_EXCEPTION(future_error(future_errc::no_state));
			}

			if(future_obtained_)
			{
				BOOST_THROW_EXCEPTION(future_error(future_errc::future_already_retrieved));
			}

			future_obtained_ = true;
			return future<Result>(state_);
		}

		// Use a vararg here to avoid having to specialize the whole class for
		// lvalue ref anf void.
		template<typename... R>
		void set_value(R&&... value)
		{
			static_assert(sizeof...(value) < 2, "set_value must be called with exactly 0 or 1 argument");

			auto lk = state_->lock();
			if(state_->is_finished(lk))
			{
				BOOST_THROW_EXCEPTION(future_error(future_errc::promise_already_satisfied));
			}

			state_->set_finished_with_result(std::forward<R>(value)..., lk);
		}

		template<typename... R>
		void set_value_at_thread_exit(R&&... value)
		{
			auto lk = state_->lock();
			if(state_->is_finished(lk))
			{
				BOOST_THROW_EXCEPTION(future_error(future_errc::promise_already_satisfied));
			}

			assert(false && "cglover-todo");
		}

		void set_exception(std::exception_ptr p)
		{
			auto lk = state_->lock();
			if(state_->is_finished(lk))
			{
				BOOST_THROW_EXCEPTION(future_error(future_errc::promise_already_satisfied));
			}

			state_->set_finished_with_exception(std::move(p), lk);
		}

		void set_exception_at_thread_exit(std::exception_ptr p)
		{
			auto lk = state_->lock();
			if(state_->is_finished(lk))
			{
				BOOST_THROW_EXCEPTION(future_error(future_errc::promise_already_satisfied));
			}

			assert(false && "cglover-todo");
		}

	private:

		std::shared_ptr<shared_state> state_;
		bool future_obtained_ = false;
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

		typedef detail::future_shared_state<Result> shared_state;
		typedef Result result_type;

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
			state_->do_wait(lk);
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

		template<typename F>
		auto then(F&& f)
		{
			return this->then(continue_on::any, std::forward<F>(f));
		}

		template<typename F>
		auto then(continue_on::any_t a, F&& f)
		{
			return continue_on_then(a, std::forward<F>(f));
		}

		template<typename F>
		auto then(continue_on::get_t g, F&& f)
		{
			return continue_on_then(g, std::forward<F>(f));
		}

		template<typename F>
		auto then(continue_on::set_t s, F&& f)
		{
			return continue_on_then(s, std::forward<F>(f));
		}

	private:

		template<typename Selector, typename F>
		auto continue_on_then(Selector s, F&& f)
		{
			typedef decltype(f(std::declval<Result>())) ContinuationResult;

			// Copy the current state shared_ptr to keep it around afer the move.
			auto current_state = state_;
			auto continuation_state = detail::make_continue_on_continuation<
											ContinuationResult
									>(s, std::move(*this), std::forward<F>(f));

			auto lk = current_state->lock();
			current_state->set_continuation(continuation_state, lk);
			return future<ContinuationResult>(continuation_state);
		}

		template<typename>
		friend class promise;

		template<typename>
		friend class future;

		template<typename, typename, typename>
		friend class detail::continue_on_basic_shared_state;

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