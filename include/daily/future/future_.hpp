#pragma once

#include <cassert>

namespace daily {

	template<typename T>
	class future;

	template<typename T>
	class promise
	{
	public:

		promise()
			: future_(nullptr)
		{}

		~promise()
		{
			update_future(nullptr);
		}
		
		void set_value(T&& t)
		{
			if(future_)
			{
				future_->set_value(std::move(t));
			}
		}

		future<T> get_future()
		{
			assert(!future_);
			return future<T>(this);
		}

	private:

		template<typename>
		friend class future;

		void update_future(promise* p);

		future<T>* future_;
	};

	template<typename T>
	class future
	{
	public:

		future()
			: promise_(nullptr)
			, value_(nullptr)
		{}

		~future()
		{
			update_promise(nullptr);
		}

		future(future&& other)
			: promise_(other.promise_)
			, value_(nullptr)
		{
			move_from(other);
		}

		future& operator=(future&& other)
		{
			move_from(other);
			return *this;
		}

		bool valid()
		{
			return value_ ? true : false;
		}

		T get()
		{
			T ret_val = std::move(*value_);
			maybe_destroy_value();
			return std::move(ret_val);
		}

		future(future const&) = delete;
		future& operator=(future const&) = delete;

	private:

		void move_from(future& other)
		{
			maybe_destroy_value();

			promise_ = other.promise_;
			other.promise_ = nullptr;

			if(other.value_)
			{
				set_value(std::move(*other.value_));
				other.value_ = nullptr;
			}
			
			update_promise(this);
		}

		void update_promise(future* ptr)
		{
			if(promise_)
			{
				promise_->future_ = ptr;
			}
		}

		template<typename>
		friend class promise;

		future(promise<T>* p)
			: promise_(p)
			, value_(nullptr)
		{
			update_promise(this);
		}

		void set_value(T&& v)
		{
			assert(!value_);
			value_ = new(&value_storage_) T(std::move(v));
		}

			
		void maybe_destroy_value()
		{	
			if(value_)
			{
				value_->~T();
				value_ = nullptr;
			}
		}

		promise<T>* promise_;
		std::aligned_storage_t<sizeof(T), alignof(T)> value_storage_;
		T* value_;
	};

	template<typename T>
	void promise<T>::update_future(promise* p)
	{
		if(future_)
		{
			future_->promise_ = p;
		}
	}
}