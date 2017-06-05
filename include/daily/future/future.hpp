// ****************************************************************************
// daily/future/future.hpp
//
// An extension of std::future that provides a continuation interface that
// allows users to control where the continuation runs.
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
#include "daily/future/default_allocator.hpp"

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

    namespace execute
    {
        struct dispatch_t {} constexpr dispatch;
        struct post_t {} constexpr post;
        struct defer_t {} constexpr defer;
    };

    class future_error : public std::logic_error
    {
    public:

        future_error(future_errc err)
            : logic_error("")
            , error_(err)
        {}

        future_errc code() const
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

        // -------------------------------------------------------------------------
        // Base shared state used by all derived shared states.
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
                    continuation_->continuation_result_ready(lock);
                }
            }

            void set_finished_with_exception(
                std::exception_ptr p, 
                std::unique_lock<std::mutex>& lock)
            {
                exception_ = std::move(p);
                // Don't call set finished because we don't want to run the continuations.
                finished_ = true;
                ready_wait_.notify_all();
            }
            
            bool is_finished(std::unique_lock<std::mutex>&)
            {
                return finished_;
            }

            bool has_exception(std::unique_lock<std::mutex>&) const
            {
                return exception_ ? true : false;
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
                    continuation_->continuation_result_ready(lock);
                }
            }

            void do_wait_result(std::unique_lock<std::mutex>& lock)
            {
                if(!finished_)
                    continuation_result_requested(lock);

                do_wait(lock);
            }

            void check_exception(std::unique_lock<std::mutex>& lock)
            {
                if(exception_)
                    std::rethrow_exception(exception_);
            }
            
            void do_wait(std::unique_lock<std::mutex>& lock)
            {
                while(!finished_)
                    ready_wait_.wait(lock);
            }

            template <typename Rep, typename Period>
            future_status do_wait_for(
                std::chrono::duration<Rep, Period> const& rel_time,
                std::unique_lock<std::mutex>& lock)
            {
                ready_wait_.wait_for(rel_time, lock);
                return finished_ ? future_status::ready : future_status::timeout;
            }

            template <typename Clock, typename Duration>
            future_status do_wait_until(
                std::chrono::time_point<Clock, Duration> const& abs_time,
                std::unique_lock<std::mutex>& lock)
            {
                ready_wait_.wait_until(abs_time, lock);
                return finished_ ? future_status::ready : future_status::timeout;
            }

            // Only implemented by continuation derived shared_state types
            void continuation_result_ready(std::unique_lock<std::mutex>& lock)
            {
                handle_continuation_result_ready(lock);
            }

            void continuation_result_requested(std::unique_lock<std::mutex>& lock)
            {
                handle_continuation_result_requested(lock);
            }

        private:

            // Only implemented by continuation derived shared_state types
            virtual void handle_continuation_result_ready(std::unique_lock<std::mutex>&)
            {}

            virtual void handle_continuation_result_requested(std::unique_lock<std::mutex>& lock)
            {
                while(!finished_)
                    ready_wait_.wait(lock);
            }

            std::exception_ptr exception_;
            std::condition_variable ready_wait_;
            std::shared_ptr<future_shared_state_base> continuation_;
            bool finished_;
            bool is_valid_;
        };

        // -------------------------------------------------------------------------
        // Base shared state used by all dreived shares states -- adds the result.
        template<typename Result>
        class future_shared_state : public future_shared_state_base
        {
        public:

            typedef boost::optional<Result> storage_type;

            void set_finished_with_result(Result r, std::unique_lock<std::mutex>& lock)
            {
                result_ = std::move(r);
                set_finished(lock);
            }

            Result get(std::unique_lock<std::mutex>& lock)
            {
                set_invalid(lock);
                this->check_exception(lock);
                return *std::move(result_);
            }

            bool has_value(std::unique_lock<std::mutex>&) const
            {
                return result_;
            }

        private:

            storage_type result_;
        };

        // -------------------------------------------------------------------------
        // Base shared state used by all derived shares states -- adds the result 
        // with a void specialization.
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
                set_invalid(lock);
                this->check_exception(lock);
            }
        };

        // -------------------------------------------------------------------------
        // Base shared state used by all dreived shares states -- adds the result 
        // with a ref specialization.
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
                set_invalid(lock);
                this->check_exception(lock);
                return *result_;
            }

        private:

            storage_type result_;
        };

        // ---------------------------------------------------------------------
        // Shared state initially created by the promise. Contains the mutex
        // used to lock the system.
        template<typename Result>
        class promise_future_shared_state : public future_shared_state<Result>
        {
        public:

            std::unique_lock<std::mutex> lock() const
            {
                return std::unique_lock<std::mutex>(mutex_);
            }

        private:

            template<typename>
            friend class promise;

            mutable std::mutex mutex_;
        };

        template<typename Param, typename Return>
        struct continue_on_continuation_helper;

        template<typename Param, typename Return>
        struct continue_on_continuation_helper
        {
            template<typename Caller>
            static void call(
                Caller* caller,
                std::unique_lock<std::mutex>& lock)
            {
                // Don't call user code with the lock still obtained.
                lock.unlock();
                Return current_result = caller->continuation_(caller->parent_->get(lock));
                lock.lock();
                // cglover-aug8th_2016: Can an exception leak from here?
                caller->set_finished_with_result(std::move(current_result), lock);
            }
        };

        // ---------------------------------------------------------------------
        // Basic Continuations.
        template<typename Return>
        struct continue_on_continuation_helper<void, Return>
        {
            template<typename Caller>
            static void call(
                Caller* caller,
                std::unique_lock<std::mutex>& lock)
            {
                // Don't call user code with the lock still obtained.
                lock.unlock();
                Return current_result = caller->continuation_();
                lock.lock();
                // cglover-aug8th_2016: Can an exception leak from here?
                caller->set_finished_with_result(std::move(current_result), lock);
            }
        };

        template<typename Param>
        struct continue_on_continuation_helper<Param, void>
        {
            template<typename Caller>
            static void call(
                Caller* caller,
                std::unique_lock<std::mutex>& lock)
            {
                // Don't call user code with the lock still obtained.
                lock.unlock();
                caller->continuation_(caller->parent_->get(lock));
                lock.lock();
                // cglover-aug8th_2016: Can an exception leak from here?
                caller->set_finished_with_result(lock);
            }
        };

        template<>
        struct continue_on_continuation_helper<void, void>
        {
            template<typename Caller>
            static void call(
                Caller* caller,
                std::unique_lock<std::mutex>& lock)
            {
                // Don't call user code with the lock still obtained.
                lock.unlock();
                caller->continuation_();
                lock.lock();
                // cglover-aug8th_2016: Can an exception leak from here?
                caller->set_finished_with_result(lock);
            }
        };

        template<typename ParentResult, typename Result, typename Function>
        class continue_on_basic_shared_state : public future_shared_state<Result>
        {
        public:

            continue_on_basic_shared_state(
                future_shared_state<ParentResult>* parent,
                Function&& f)
                : parent_(std::move(parent))
                , continuation_(std::move(f))
            {}

            ~continue_on_basic_shared_state() = 0;
            
        protected:
            
            template<typename Param, typename Return>
            friend struct continue_on_continuation_helper;

            void do_continue(std::unique_lock<std::mutex>& lock)
            {
                BOOST_TRY
                {
                    continue_on_continuation_helper<ParentResult, Result>::call(this, lock);
                }
                BOOST_CATCH(...)
                {
                    lock.lock();
                    this->set_finished_with_exception(std::current_exception(), lock);
                }
                BOOST_CATCH_END
            }

            // Don't store a shared_ptr here because as long as we're alive the parent
            // must be too.
            future_shared_state<ParentResult>* parent_;
            Function continuation_;
        };

        template<typename Future, typename Result, typename Function>
        inline continue_on_basic_shared_state<Future, Result, Function>::~continue_on_basic_shared_state()
        {}

        // ---------------------------------------------------------------------
        //
        template<typename ParentResult, typename Result, typename Function>
        class continue_on_any_shared_state
            : public continue_on_basic_shared_state<ParentResult, Result, Function>
        {
        public:

            using continue_on_basic_shared_state<
                ParentResult, Result, Function
            >::continue_on_basic_shared_state;

        private:

            void handle_continuation_result_ready(std::unique_lock<std::mutex>& lock) override
            {
                this->do_continue(lock);
                this->check_exception(lock);
            }

            void handle_continuation_result_requested(std::unique_lock<std::mutex>& lock) override
            {
                this->do_continue(lock);
            }
        };

        // ---------------------------------------------------------------------
        //
        template<typename ParentResult, typename Result, typename Function>
        class continue_on_set_shared_state 
            : public continue_on_basic_shared_state<ParentResult, Result, Function>
        {
        public:

            using continue_on_basic_shared_state<
                ParentResult, Result, Function
            >::continue_on_basic_shared_state;

        private:

            void handle_continuation_result_ready(std::unique_lock<std::mutex>& lock) override
            {
                this->do_continue(lock);
                this->check_exception(lock);
            }

            void handle_continuation_result_requested(std::unique_lock<std::mutex>& lock)
            {
                this->parent_->continuation_result_requested(lock);
            }
        };

        // ---------------------------------------------------------------------
        //
        template<typename ParentResult, typename Result, typename Function>
        class continue_on_get_shared_state
            : public continue_on_basic_shared_state<ParentResult, Result, Function>
        {
        public:

            using continue_on_basic_shared_state<
                ParentResult, Result, Function
            >::continue_on_basic_shared_state;

        private:

            void handle_continuation_result_requested(std::unique_lock<std::mutex>& lock) override
            {
                this->parent_->continuation_result_requested(lock);
                this->do_continue(lock);
            }
        };

        // ---------------------------------------------------------------------
        //
        template<
            typename Result
          , typename ParentResult
          , typename Function
          , typename Allocator
        >
        auto make_continue_on_continuation(
            continue_on::any_t,
            future_shared_state<ParentResult>* parent, 
            Function&& func,
            Allocator const& alloc)
        {
            return std::allocate_shared<
                continue_on_any_shared_state<ParentResult, Result, Function>
            >(alloc, parent, std::forward<Function>(func));
        }

        template<
            typename Result
          , typename ParentResult
          , typename Function
          , typename Allocator
        >
        auto make_continue_on_continuation(
            continue_on::get_t, 
            future_shared_state<ParentResult>* parent, 
            Function&& func,
            Allocator const& alloc)
        {
            return std::allocate_shared<
                continue_on_get_shared_state<ParentResult, Result, Function>
            >(alloc, parent, std::forward<Function>(func));
        }

        template<
            typename Result
          , typename ParentResult
          , typename Function
          , typename Allocator
        >
        auto make_continue_on_continuation(
            continue_on::set_t, 
            future_shared_state<ParentResult>* parent,
            Function&& func,
            Allocator const& alloc)
        {
            return std::allocate_shared<
                continue_on_set_shared_state<ParentResult, Result, Function>
            >(alloc, parent, std::forward<Function>(func));
        }

        // ---------------------------------------------------------------------
        // Executor Continuations.
        struct submit_dispatch
        {
            template<typename Executor, typename Closure, typename Allocator>
            static void submit(Executor ex, Closure&& c, Allocator const& alloc)
            {
                ex.dispatch(std::forward<Closure>(c), alloc);
            }
        };

        struct submit_post
        {
            template<typename Executor, typename Closure, typename Allocator>
            static void submit(Executor ex, Closure&& c, Allocator const& alloc)
            {
                ex.post(std::forward<Closure>(c), alloc);
            }
        };

        struct submit_defer
        {
            template<typename Executor, typename Closure, typename Allocator>
            static void submit(Executor ex, Closure&& c, Allocator const& alloc)
            {
                ex.defer(std::forward<Closure>(c), alloc);
            }
        };

        template<typename Submiter, typename Param, typename Return>
        struct executor_continuation_helper;

        template<typename Submiter, typename Param, typename Return>
        struct executor_continuation_helper
        {
            template<typename Caller, typename Allocator>
            static void call(
                Caller* caller,
                std::unique_lock<std::mutex>& lock,
                std::shared_ptr<std::mutex>& promise_mutex,
                Allocator const& alloc)
            {
                auto closure = [caller, p = caller->parent_->get(lock), promise_mutex]
                {
                    auto result = caller->continuation_(std::move(p));
                    std::unique_lock<std::mutex> lock(*promise_mutex);
                    caller->set_finished_with_result(std::move(result), lock);
                };
                
                // Don't call user code with the lock still obtained.
                lock.unlock();
                Submiter::submit(caller->executor_, std::move(closure), alloc);
                lock.lock();
            }
        };

        template<typename Submiter, typename Return>
        struct executor_continuation_helper<Submiter, void, Return>
        {
            template<typename Caller, typename Allocator>
            static void call(
                Caller* caller,
                std::unique_lock<std::mutex>& lock,
                std::shared_ptr<std::mutex>& promise_mutex,
                Allocator const& alloc)
            {
                auto closure = [caller, promise_mutex]
                {
                    auto result = caller->continuation_();
                    std::unique_lock<std::mutex> lock(*promise_mutex);
                    caller->set_finished_with_result(std::move(result), lock);
                };
                
                // Don't call user code with the lock still obtained.
                lock.unlock();
                Submiter::submit(caller->executor_, std::move(closure), alloc);
                lock.lock();
            }
        };

        template<typename Submiter, typename Param>
        struct executor_continuation_helper<Submiter, Param, void>
        {
            template<typename Caller, typename Allocator>
            static void call(
                Caller* caller,
                std::unique_lock<std::mutex>& lock,
                std::shared_ptr<std::mutex>& promise_mutex,
                Allocator const& alloc)
            {
                auto closure = [caller, p = caller->parent_->get(lock), promise_mutex]
                {
                    caller->continuation_(std::move(p));
                    std::unique_lock<std::mutex> lock(*promise_mutex);
                    caller->set_finished_with_result(lock);
                };
                
                // Don't call user code with the lock still obtained.
                lock.unlock();
                Submiter::submit(caller->executor_, std::move(closure), alloc);
                lock.lock();
            }
        };

        template<typename Submiter>
        struct executor_continuation_helper<Submiter, void, void>
        {
            template<typename Caller, typename Allocator>
            static void call(
                Caller* caller,
                std::unique_lock<std::mutex>& lock,
                std::shared_ptr<std::mutex>& promise_mutex,
                Allocator const& alloc)
            {
                auto closure = [caller, promise_mutex]
                {
                    caller->continuation_();
                    std::unique_lock<std::mutex> lock(*promise_mutex);
                    caller->set_finished_with_result(lock);
                };
                
                // Don't call user code with the lock still obtained.
                lock.unlock();
                Submiter::submit(caller->executor_, closure, alloc);
                lock.lock();
            }
        };

        template<
              typename Submitter
            , typename Executor
            , typename ParentResult
            , typename Result
            , typename Function
            , typename Allocator
        >
        class executor_continuation_shared_state 
            : public future_shared_state<Result>
        {
        public:

            executor_continuation_shared_state(
                Executor ex,
                future_shared_state<ParentResult>* parent,
                Function&& f,
                std::shared_ptr<std::mutex> mutex,
                Allocator alloc)
                : parent_(std::move(parent))
                , executor_(ex)
                , continuation_(std::move(f))
                , promise_mutex_(std::move(mutex))
                , allocator_(alloc)
            {}
            
        private:

            template<typename, typename, typename>
            friend struct executor_continuation_helper;

            void handle_continuation_result_ready(std::unique_lock<std::mutex>& lock) override
            {
                assert(lock.mutex() == promise_mutex_.get());

                // We don't need to store a shared ptr to this state here
                // because the shared_ptr to the promise mutex will keep the
                // whole chain alive.
                executor_continuation_helper<
                    Submitter, ParentResult, Result
                >::call(this, lock, promise_mutex_, allocator_);
            }
            
            // Don't store a shared_ptr here because as long as we're alive the parent
            // must be too.
            future_shared_state<ParentResult>* parent_;
            Executor executor_;
            Function continuation_;
            std::shared_ptr<std::mutex> promise_mutex_;
            Allocator allocator_;
        };

        // ---------------------------------------------------------------------
        //
        template<
            typename Result
          , typename Executor
          , typename ParentResult
          , typename Function
          , typename Allocator
        >
        auto make_executor_continuation(
            execute::dispatch_t,
            Executor ex,
            future_shared_state<ParentResult>* parent, 
            Function&& func,
            std::shared_ptr<std::mutex> mutex,
            Allocator const& alloc)
        {
            typedef executor_continuation_shared_state<
                    submit_dispatch
                  , Executor
                  , ParentResult
                  , Result
                  , Function
                  , Allocator
                > shared_state;

            return std::allocate_shared<shared_state>(
                alloc,
                std::move(ex), 
                parent, 
                std::forward<Function>(func), 
                std::move(mutex),
                alloc);
        }

        template<
            typename Result
          , typename Executor
          , typename ParentResult
          , typename Function
          , typename Allocator
        >
        auto make_executor_continuation(
            execute::post_t, 
            Executor ex,
            future_shared_state<ParentResult>* parent, 
            Function&& func,
            std::shared_ptr<std::mutex> mutex,
            Allocator const& alloc)
        {
            typedef executor_continuation_shared_state<
                    submit_post
                  , Executor
                  , ParentResult
                  , Result
                  , Function
                  , Allocator
                > shared_state;
                
            return std::allocate_shared<shared_state>(
                alloc,
                std::move(ex), 
                parent, 
                std::forward<Function>(func), 
                std::move(mutex),
                alloc);
        }

        template<
            typename Result
          , typename Executor
          , typename ParentResult
          , typename Function
          , typename Allocator
        >
        auto make_executor_continuation(
            execute::defer_t, 
            Executor ex,
            future_shared_state<ParentResult>* parent, 
            Function&& func,
            std::shared_ptr<std::mutex> mutex,
            Allocator const& alloc)
        {
            typedef executor_continuation_shared_state<
                    submit_defer
                  , Executor
                  , ParentResult
                  , Result
                  , Function
                  , Allocator
                > shared_state;
                
            return std::allocate_shared<shared_state>(
                alloc,
                std::move(ex), 
                parent, 
                std::forward<Function>(func), 
                std::move(mutex),
                alloc);
        }
    }

    // -------------------------------------------------------------------------
    //
    template<typename Result = void>
    class promise
    {
    private:

        typedef detail::promise_future_shared_state<Result> shared_state;

    public:
        promise()
            : state_(std::make_shared<shared_state>())
        {}

        template<typename Allocator>
        promise(std::allocator_arg_t, Allocator const& alloc)
            : state_(std::allocate_shared<shared_state>(alloc))
        {}

        ~promise()
        {
            if(state_)
            {
                auto lk = state_->lock();
                if(future_obtained_ && !state_->is_finished(lk))
                {
                    BOOST_TRY
                    {
                        BOOST_THROW_EXCEPTION(
                            future_error(future_errc::broken_promise)
                        );
                    }
                    BOOST_CATCH(...)
                    {
                        BOOST_TRY
                        {
                            state_->set_finished_with_exception(std::current_exception(), lk);
                        }
                        // Don't let exceptions escape from the dtor.
                        BOOST_CATCH(...)
                        {}
                        BOOST_CATCH_END
                    }
                    BOOST_CATCH_END
                }
            }
        }

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
            return future<Result>(state_, {state_, &state_->mutex_});
        }

        // Use a vararg here to avoid having to specialize the whole class for
        // lvalue ref and void.
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
    template<typename Result = void>
    class future
    {
    private:

        typedef detail::future_shared_state<Result> shared_state;
        typedef Result result_type;

    public:

        future() noexcept
            : mutex_(nullptr)
        {}

        // move support
        future(future&& other) noexcept
            : state_(std::move(other.state_))
            , mutex_(std::move(other.mutex_))
        {}

        future& operator=(future&& other) noexcept
        {
            if(&other != this)
            {
                state_ = std::move(other.state_);
                mutex_ = std::move(other.mutex_);
            }
            return *this;
        }

        // no copy
        future(future const& other) = delete;
        future& operator=(future const& other) = delete;

        Result get()
        {
            assert(valid());
            auto lk = lock();
            state_->do_wait_result(lk);
            return state_->get(lk);
        }

        bool valid() const noexcept
        {
            if(state_)
            {
                auto lk = lock();
                return state_->is_valid(lk);
            }

            return false;
        }

        void wait() const
        {
            auto lk = lock();
            assert(state_->is_valid(lk));
            state_->do_wait(lk);
        }

        bool is_ready() const
        {
            auto lk = lock();
            return state_->is_finished(lk);
        }

        bool has_exception() const
        {
            auto lk = lock();
            return state_->has_exception(lk);
        }

        bool has_value() const
        {
            auto lk = lock();
            return state_->has_value(lk);
        }

        template <typename Rep, typename Period>
        future_status wait_for(std::chrono::duration<Rep, Period> const& rel_time) const
        {
            auto lk = lock();
            assert(state_->is_valid(lk));
            return state_->do_wait_for(rel_time, lk);
        }

        template <typename Clock, typename Duration>
        future_status wait_until(std::chrono::time_point<Clock, Duration> const& abs_time) const
        {
            auto lk = lock();
            assert(state_->is_valid(lk));
            return state_->do_wait_until(abs_time, lk);
        }

        template<typename F, typename Allocator = future_default_allocator>
        auto then(F&& f, Allocator const& alloc = Allocator())
        {
            assert(valid());
            return this->then(continue_on::any, std::forward<F>(f), alloc);
        }

        template<typename F, typename Allocator = future_default_allocator>
        auto then(
            continue_on::any_t a, F&& f,
            Allocator const& alloc = Allocator())
        {
            assert(valid());
            return continue_on_then(a, std::forward<F>(f), alloc);
        }

        template<typename F, typename Allocator = future_default_allocator>
        auto then(
            continue_on::get_t g, F&& f,
            Allocator const& alloc = Allocator())
        {
            assert(valid());
            return continue_on_then(g, std::forward<F>(f), alloc);
        }

        template<typename F, typename Allocator = future_default_allocator>
        auto then(
            continue_on::set_t s, F&& f, 
            Allocator const& alloc = Allocator())
        {
            assert(valid());
            return continue_on_then(s, std::forward<F>(f), alloc);
        }

        template<typename Executor, typename F, typename Allocator = future_default_allocator>
        auto then(
            execute::dispatch_t d, Executor& ex, F&& f,
            Allocator const& alloc = Allocator())
        {
            assert(valid());
            return executor_then(d, ex, std::forward<F>(f), alloc);
        }

        template<typename Executor, typename F, typename Allocator = future_default_allocator>
        auto then(
            execute::post_t p, Executor& ex, F&& f,
            Allocator const& alloc = Allocator())
        {
            assert(valid());
            return executor_then(p, ex, std::forward<F>(f), alloc);
        }

        template<typename Executor, typename F, typename Allocator = future_default_allocator>
        auto then(
            execute::defer_t d, Executor& ex, F&& f,
            Allocator const& alloc = Allocator())
        {
            assert(valid());
            return executor_then(d, ex, std::forward<F>(f), alloc);
        }

        future(
            std::shared_ptr<shared_state> ss, 
            std::shared_ptr<std::mutex> promise_mutex)
            : state_(std::move(ss))
            , mutex_(promise_mutex)
        {}

    private:

        template<typename Result>
        struct unwrap_nested_future
        {
            typedef future<Result> type;
        };

        template<typename Result>
        struct unwrap_nested_future<future<Result>>
        {
            typedef unwrap_nested_future<Result> type;
        };

        template<typename Function, typename Param>
        struct result_of;

        template<typename Function, typename Param>
        struct result_of
        {
            typedef typename unwrap_nested_future<
                decltype(std::declval<Function>()(std::declval<Param>()))
            >::type type;
        };

        template<typename Function>
        struct result_of<Function, void>
        {
            typedef typename unwrap_nested_future<
                decltype(std::declval<Function>()())
            >::type type;
        };  
        
        template<typename Selector, typename F, typename Allocator>
        auto continue_on_then(Selector s, F&& f, Allocator const& alloc)
        {
            typedef typename result_of<F, Result>::type ContinuationResult;

            // 'this' future is now invalidated so we move the state out
            // to indicate that.
            auto current_state = std::move(state_);
            auto continuation_state =
                detail::make_continue_on_continuation<
                    ContinuationResult>(
                        s, current_state.get(), std::forward<F>(f), alloc);

            auto lk = lock();
            current_state->set_continuation(continuation_state, lk);
            return future<ContinuationResult>(std::move(continuation_state), std::move(mutex_));
        }

        template<typename Selector, typename Executor, typename F, typename Allocator>
        auto executor_then(Selector s, Executor& ex, F&& f, Allocator const& alloc)
        {
            typedef typename result_of<F, Result>::type ContinuationResult;

            // 'this' future is now invalidated so we move the state out
            // to indicate that.
            auto current_state = std::move(state_);
            auto continuation_state = 
                detail::make_executor_continuation<
                    ContinuationResult>(
                        s, ex.get_executor(), current_state.get(), 
                        std::forward<F>(f), mutex_, alloc);

            auto lk = lock();
            current_state->set_continuation(continuation_state, lk);
            return future<ContinuationResult>(std::move(continuation_state), std::move(mutex_));
        }

        std::unique_lock<std::mutex> lock() const
        {
            return std::unique_lock<std::mutex>(*mutex_);
        }

        template<typename>
        friend class promise;

        template<typename>
        friend class future;

        template<typename, typename, typename>
        friend class detail::continue_on_basic_shared_state;

        std::shared_ptr<shared_state> state_;
        std::shared_ptr<std::mutex> mutex_;
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