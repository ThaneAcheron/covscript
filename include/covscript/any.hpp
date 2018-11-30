#pragma once
/*
* Covariant Script Any
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* Copyright (C) 2018 Michael Lee(李登淳)
* Email: mikecovlee@163.com
* Github: https://github.com/mikecovlee
*/
#include <covscript/mozart/memory.hpp>

namespace cs_impl {
// Be careful when you adjust the buffer size.
	constexpr std::size_t default_allocate_buffer_size = 64;
	template<typename T> using default_allocator_provider=std::allocator<T>;

	enum class object_status {
		normal, recycle, deposit, reachable
	};

	enum class object_authority {
		normal, protect, constant
	};

	class any_object {
	public:
		object_status status = object_status::normal;

		object_authority authority = object_authority::normal;

		any_object() = default;

		virtual ~ any_object() = default;

		virtual const std::type_info &type() const = 0;

		virtual any_object *duplicate() = 0;

		virtual bool compare(const any_object *) const = 0;

		virtual long to_integer() const = 0;

		virtual std::string to_string() const = 0;

		virtual std::size_t hash() const = 0;

		virtual void detach() = 0;

		virtual void kill() = 0;

		virtual cs::namespace_t &get_ext() const = 0;

		virtual const char *get_type_name() const = 0;
	};

	template<typename T>
	class any_obj_instance : public any_object {
	protected:
		T mDat;
	public:
		static cov::allocator<any_obj_instance<T>, default_allocate_buffer_size, default_allocator_provider> allocator;

		any_obj_instance() = default;

		template<typename...ArgsT>
		explicit any_obj_instance(ArgsT &&...args):mDat(std::forward<ArgsT>(args)...) {}

		~ any_obj_instance() override = default;

		const std::type_info &type() const override
		{
			return typeid(T);
		}

		any_object *duplicate() override
		{
			return allocator.alloc(mDat);
		}

		bool compare(const any_object *obj) const override
		{
			if (obj->type() == this->type())
				return cs_impl::compare(mDat, static_cast<const any_obj_instance<T> *>(obj)->data());
			else
				return false;
		}

		long to_integer() const override
		{
			return cs_impl::to_integer(mDat);
		}

		std::string to_string() const override
		{
			return cs_impl::to_string(mDat);
		}

		std::size_t hash() const override
		{
			return cs_impl::hash<T>(mDat);
		}

		void detach() override
		{
			cs_impl::detach(mDat);
		}

		void kill() override
		{
			allocator.free(this);
		}

		virtual cs::namespace_t &get_ext() const override
		{
			return cs_impl::get_ext<T>();
		}

		const char *get_type_name() const override
		{
			return cs_impl::get_name_of_type<T>();
		}

		T &data()
		{
			return mDat;
		}

		const T &data() const
		{
			return mDat;
		}

		void data(const T &dat)
		{
			mDat = dat;
		}
	};

	struct any_holder
	{
		any_object* object=nullptr;

		any_holder()=default;

		any_holder(any_object* obj):object(obj) {}

		any_holder(const any_holder&)=default;

		any_holder(any_holder&&) noexcept=default;

		void replace(any_object* obj)
		{
			if(object->status==object_status::deposit)
				object->status=object_status::normal;
			else
				object->kill();
			object=obj;
		}
	};

	class any final {
		struct proxy {
			std::size_t refcount = 1;
			any_object *data = nullptr;

			proxy() = delete;

			proxy(std::size_t rc, any_object *d) : refcount(rc), data(d) {}

			proxy(object_authority a, std::size_t rc, any_object *d) : refcount(rc), data(d)
			{
				d->authority=a;
			}

			~proxy()
			{
				if (data != nullptr) {
					if(data->status==object_status::deposit)
						data->status=object_status::normal;
					else
						data->kill();
				}
			}
		};

		static cov::allocator<proxy, default_allocate_buffer_size, default_allocator_provider> allocator;
		proxy *mDat = nullptr;

		proxy *duplicate() const noexcept
		{
			if (mDat != nullptr) {
				++mDat->refcount;
			}
			return mDat;
		}

		void recycle() noexcept
		{
			if (mDat != nullptr) {
				--mDat->refcount;
				if (mDat->refcount == 0) {
					allocator.free(mDat);
					mDat = nullptr;
				}
			}
		}

		explicit any(proxy *dat) : mDat(dat) {}

	public:
		void swap(any &obj, bool raw = false)
		{
			if (this->mDat != nullptr && obj.mDat != nullptr && raw) {
				if (this->mDat->data->authority != object_authority::normal || obj.mDat->data->authority != object_authority::normal)
					throw cs::lang_error("Swap two variable which has limits of authority.");
				any_object *tmp = this->mDat->data;
				this->mDat->data = obj.mDat->data;
				obj.mDat->data = tmp;
			}
			else {
				proxy *tmp = this->mDat;
				this->mDat = obj.mDat;
				obj.mDat = tmp;
			}
		}

		void swap(any &&obj, bool raw = false)
		{
			if (this->mDat != nullptr && obj.mDat != nullptr && raw) {
				if (this->mDat->data->authority != object_authority::normal || obj.mDat->data->authority != object_authority::normal)
					throw cs::lang_error("Swap two variable which has limits of authority.");
				any_object *tmp = this->mDat->data;
				this->mDat->data = obj.mDat->data;
				obj.mDat->data = tmp;
			}
			else {
				proxy *tmp = this->mDat;
				this->mDat = obj.mDat;
				obj.mDat = tmp;
			}
		}

		void clone()
		{
			if (mDat != nullptr) {
				proxy *dat = allocator.alloc(1, mDat->data->duplicate());
				recycle();
				mDat = dat;
			}
		}

		void try_move() const
		{
			if (mDat != nullptr && mDat->refcount == 1&&mDat->data->status==object_status::normal) {
				mDat->data->authority = object_authority::normal;
				mDat->data->status = object_status::recycle;
			}
		}

		bool usable() const noexcept
		{
			return mDat != nullptr;
		}

		template<typename T, typename...ArgsT>
		static any make(ArgsT &&...args)
		{
			return any(allocator.alloc(1, any_obj_instance<T>::allocator.alloc(std::forward<ArgsT>(args)...)));
		}

		template<typename T, typename...ArgsT>
		static any make_protect(ArgsT &&...args)
		{
			return any(allocator.alloc(object_authority::protect, 1, any_obj_instance<T>::allocator.alloc(std::forward<ArgsT>(args)...)));
		}

		template<typename T, typename...ArgsT>
		static any make_constant(ArgsT &&...args)
		{
			return any(allocator.alloc(object_authority::constant, 1, any_obj_instance<T>::allocator.alloc(std::forward<ArgsT>(args)...)));
		}

		constexpr any() = default;

		any(const any_holder& holder):mDat(allocator.alloc(1, holder.object))
		{
			holder.object->status=object_status::deposit;
		}

		template<typename T>
		any(const T &dat):mDat(allocator.alloc(1, any_obj_instance<T>::allocator.alloc(dat))) {}

		any(const any &v) : mDat(v.duplicate()) {}

		any(any &&v) noexcept
		{
			swap(std::forward<any>(v));
		}

		~any()
		{
			recycle();
		}

		any_object* share_object() const
		{
			if (this->mDat != nullptr)
			{
				this->mDat->data->status=object_status::deposit;
				return this->mDat->data;
			}else
				throw cs::internal_error("Share null object from variable.");
		}

		const std::type_info &type() const
		{
			return this->mDat != nullptr ? this->mDat->data->type() : typeid(void);
		}

		long to_integer() const
		{
			if (this->mDat == nullptr)
				return 0;
			return this->mDat->data->to_integer();
		}

		std::string to_string() const
		{
			if (this->mDat == nullptr)
				return "Null";
			return this->mDat->data->to_string();
		}

		std::size_t hash() const
		{
			if (this->mDat == nullptr)
				return cs_impl::hash<void *>(nullptr);
			return this->mDat->data->hash();
		}

		void detach() const
		{
			if (this->mDat != nullptr)
				this->mDat->data->detach();
		}

		cs::namespace_t &get_ext() const
		{
			if (this->mDat == nullptr)
				throw cs::runtime_error("Target type does not support extensions.");
			return this->mDat->data->get_ext();
		}

		std::string get_type_name() const
		{
			if (this->mDat == nullptr)
				return cxx_demangle(get_name_of_type<void>());
			else
				return cxx_demangle(this->mDat->data->get_type_name());
		}

		bool is_same(const any_holder &obj) const
		{
			return this->mDat->data == obj.object;
		}

		bool is_same(const any &obj) const
		{
			return this->mDat->data == obj.mDat->data;
		}

		bool is_rvalue() const
		{
			return this->mDat != nullptr && this->mDat->data->status==object_status::recycle;
		}

		void mark_as_rvalue(bool value) const
		{
			if (this->mDat != nullptr&&this->mDat->data->status!=object_status::deposit)
				this->mDat->data->status=value?object_status::recycle:object_status::normal;
		}

		bool is_protect() const
		{
			return this->mDat != nullptr && this->mDat->data->authority != object_authority::normal;
		}

		bool is_constant() const
		{
			return this->mDat != nullptr && this->mDat->data->authority == object_authority::constant;
		}

		void protect()
		{
			if (this->mDat != nullptr) {
				if (this->mDat->data->authority != object_authority::normal)
					throw cs::internal_error("Downgrade object authority.");
				this->mDat->data->authority = object_authority::protect;
			}
		}

		void constant()
		{
			if (this->mDat != nullptr) {
				this->mDat->data->authority = object_authority::constant;
			}
		}

		any &operator=(const any &var)
		{
			if (&var != this) {
				recycle();
				mDat = var.duplicate();
			}
			return *this;
		}

		any &operator=(any &&var) noexcept
		{
			if (&var != this)
				swap(std::forward<any>(var));
			return *this;
		}

		bool compare(const any &var) const
		{
			return usable() && var.usable() ? this->mDat->data->compare(var.mDat->data) : !usable() && !var.usable();
		}

		bool operator==(const any &var) const
		{
			return this->compare(var);
		}

		bool operator!=(const any &var) const
		{
			return !this->compare(var);
		}

		template<typename T>
		T &val(bool raw = false)
		{
			if (typeid(T) != this->type())
				throw cov::error("E0006");
			if (this->mDat == nullptr)
				throw cov::error("E0005");
			if (this->mDat->data->authority == object_authority::constant)
				throw cov::error("E000K");
			if (!raw)
				clone();
			return static_cast<any_obj_instance<T> *>(this->mDat->data)->data();
		}

		template<typename T>
		const T &val(bool raw = false) const
		{
			if (typeid(T) != this->type())
				throw cov::error("E0006");
			if (this->mDat == nullptr)
				throw cov::error("E0005");
			return static_cast<const any_obj_instance<T> *>(this->mDat->data)->data();
		}

		template<typename T>
		const T &const_val() const
		{
			if (typeid(T) != this->type())
				throw cov::error("E0006");
			if (this->mDat == nullptr)
				throw cov::error("E0005");
			return static_cast<const any_obj_instance<T> *>(this->mDat->data)->data();
		}

		template<typename T>
		explicit operator const T &() const
		{
			return this->const_val<T>();
		}

		void assign(const any &obj, bool raw = false)
		{
			if (&obj != this && obj.mDat != mDat) {
				if (mDat != nullptr && obj.mDat != nullptr && raw) {
					if (this->mDat->data->authority != object_authority::normal || obj.mDat->data->authority != object_authority::normal)
						throw cov::error("E000J");
					mDat->data->kill();
					mDat->data = obj.mDat->data->duplicate();
				}
				else {
					recycle();
					if (obj.mDat != nullptr)
						mDat = allocator.alloc(1, obj.mDat->data->duplicate());
					else
						mDat = nullptr;
				}
			}
		}

		template<typename T>
		void assign(const T &dat, bool raw = false)
		{
			if (mDat != nullptr && raw) {
				if (this->mDat->data->authority != object_authority::normal)
					throw cov::error("E000J");
				mDat->data->kill();
				mDat->data = any_obj_instance<T>::allocator.alloc(dat);
			}
			else {
				recycle();
				mDat = allocator.alloc(1, any_obj_instance<T>::allocator.alloc(dat));
			}
		}

		template<typename T>
		any &operator=(const T &dat)
		{
			assign(dat);
			return *this;
		}
	};

	template<>
	std::string to_string<std::string>(const std::string &str)
	{
		return str;
	}

	template<>
	std::string to_string<bool>(const bool &v)
	{
		if (v)
			return "true";
		else
			return "false";
	}

	template<int N>
	class any_obj_instance<char[N]> : public any_obj_instance<std::string> {
	public:
		using any_obj_instance<std::string>::any_obj_instance;
	};

	template<>
	class any_obj_instance<std::type_info> : public any_obj_instance<std::type_index> {
	public:
		using any_obj_instance<std::type_index>::any_obj_instance;
	};

	template<typename T> cov::allocator<any::holder<T>, default_allocate_buffer_size, default_allocator_provider> any::holder<T>::allocator;
}

std::ostream &operator<<(std::ostream &, const cs_impl::any &);

namespace std {
	template<>
	struct hash<cs_impl::any> {
		std::size_t operator()(const cs_impl::any &val) const
		{
			return val.hash();
		}
	};
}
