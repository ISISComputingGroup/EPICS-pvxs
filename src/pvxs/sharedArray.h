/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */
#ifndef PVXS_SHAREDVECTOR_H
#define PVXS_SHAREDVECTOR_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <algorithm>
#include <ostream>

#include <pvxs/version.h>

namespace pvxs {

class IValue;

template<typename E, class Enable = void> class shared_array;

enum class ArrayType : uint8_t {
    Null  = 0xff,
    Bool  = 0x08,
    Int8  = 0x28,
    Int16 = 0x29,
    Int32 = 0x2a,
    Int64 = 0x2b,
    UInt8 = 0x2c,
    UInt16= 0x2d,
    UInt32= 0x2e,
    UInt64= 0x2f,
    Float = 0x4a,
    Double= 0x4b,
    String= 0x68,
    Value = 0x88, // also used for 0x89 and 0x8a
};

PVXS_API
std::ostream& operator<<(std::ostream& strm, ArrayType code);

namespace detail {
template<typename T>
struct CaptureCode;

#define CASE(TYPE, CODE) \
template<> struct CaptureCode<TYPE> { static constexpr ArrayType code{ArrayType::CODE}; }
CASE(bool, Bool);
CASE(int8_t,  Int8);
CASE(int16_t, Int16);
CASE(int32_t, Int32);
CASE(int64_t, Int64);
CASE(uint8_t,  UInt8);
CASE(uint16_t, UInt16);
CASE(uint32_t, UInt32);
CASE(uint64_t, UInt64);
CASE(float, Float);
CASE(double, Double);
CASE(std::string, String);
CASE(IValue, Value);
#undef CASE

template<typename T, typename Enable=void>
struct sizeofx {
    static inline size_t op() { return sizeof(T); }
};
template<typename T>
struct sizeofx<T, typename std::enable_if<std::is_void<T>{}>::type> {
    static inline size_t op() { return 1u; } // treat void* as pointer to bytes
};

template<typename E>
struct sa_default_delete {
    void operator()(E* e) const { delete[] e; }
};

template<typename E>
struct sa_base {
protected:
    template<typename E1> friend struct sa_base;

    std::shared_ptr<E> _data;
    size_t             _size;
public:

    // shared_array()
    // shared_array(const shared_array&)
    // shared_array(shared_array&&)
    // shared_array(size_t, T)
    // shared_array(T*, size_t)
    // shared_array(T*, d, size_t)
    // shared_array(shared_ptr<T>, size_t)
    // shared_array(shared_ptr<T>, T*, size_t)

    //! empty
    constexpr sa_base() :_size(0u) {}

    // copyable
    sa_base(const sa_base&) = default;
    // movable
    inline sa_base(sa_base&& o) noexcept
        :_data(std::move(o._data)), _size(o._size)
    {
        o._size = 0;
    }
    sa_base& operator=(const sa_base&) =default;
    inline sa_base& operator=(sa_base&& o) noexcept
    {
        _data = std::move(o._data);
        _size = o._size;
        o._size = 0;
        return *this;
    }

    // use existing alloc with delete[]
    template<typename A>
    sa_base(A* a, size_t len)
        :_data(a, sa_default_delete<E>()),_size(len)
    {}

    // use existing alloc w/ custom deletor
    template<typename B>
    sa_base(E* a, B b, size_t len)
        :_data(a, b),_size(len)
    {}

    // build around existing shared_ptr
    sa_base(const std::shared_ptr<E>& a, size_t len)
        :_data(a),_size(len)
    {}

    // alias existing shared_ptr
    template<typename A>
    sa_base(const std::shared_ptr<A>& a, E* b, size_t len)
        :_data(a, b),_size(len)
    {}

    void clear() noexcept {
        _data.reset();
        _size = 0;
    }

    void swap(sa_base& o) noexcept {
        std::swap(_data, o._data);
        std::swap(_size, o._data);
    }

    inline size_t size() const { return _size; }
    inline bool empty() const noexcept { return _size==0; }

    inline bool unique() const noexcept { return !_data || _data.use_count()<=1; }

    E* data() const noexcept { return _data.get(); }

    const std::shared_ptr<E>& dataPtr() const { return _data; }
};

} // namespace detail

/** std::vector-like contigious array of items passed by reference.
 *
 * shared_array comes in const and non-const, as well as void and non-void variants.
 *
 * A non-const array is allocated and filled, then last non-const reference is exchanged for new const reference.
 * This const reference can then be safely shared between various threads.
 *
 * @code
 *   shared_array<uint32_t> arr({1, 2, 3});
 *   assert(arr.size()==3);
 *   shared_ptr<const uint32_t> constarr(arr.freeze());
 *   assert(arr.size()==0);
 *   assert(constarr.size()==3);
 * @endcode
 *
 * The void / non-void variants allow arrays to be moved without explicit typing.
 * However, the void variant preserves the original TypeCode.
 *
 * @code
 *   shared_array<uint32_t> arr({1, 2, 3});
 *   assert(arr.size()==3);
 *   shared_array<void> voidarr(arr.castTo<void>());
 *   assert(arr.size()==0);
 *   assert(voidarr.size()==3*sizeof(uint32_t)); // void size() bytes
 * @endcode
 */
template<typename E, class Enable>
class shared_array : public detail::sa_base<E> {
    static_assert (!std::is_void<E>::value, "non-void specialization");

    template<typename E1, class Enable1> friend class shared_array;

    typedef detail::sa_base<E> base_t;
    typedef typename std::remove_const<E>::type _E_non_const;
public:
    typedef E value_type;
    typedef E& reference;
    typedef typename std::add_const<E>::type& const_reference;
    typedef E* pointer;
    typedef typename std::add_const<E>::type* const_pointer;
    typedef E* iterator;
    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef typename std::add_const<E>::type* const_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
    typedef std::ptrdiff_t difference_type;
    typedef size_t size_type;

    typedef E element_type;

    constexpr shared_array() noexcept :base_t() {}

    //! allocate new array and populate from initializer list
    template<typename A>
    shared_array(std::initializer_list<A> L)
        :base_t(new _E_non_const[L.size()], L.size())
    {
        auto raw = const_cast<_E_non_const*>(this->data());
        std::copy(L.begin(), L.end(), raw);
    }

    //! @brief Allocate (with new[]) a new vector of size c
    explicit shared_array(size_t c)
        :base_t(new _E_non_const[c], c)
    {}

    //! @brief Allocate (with new[]) a new vector of size c and fill with value e
    template<typename V>
    shared_array(size_t c, V e)
        :base_t(new _E_non_const[c], c)
    {
        std::fill_n((_E_non_const*)this->_data.get(), this->_size, e);
    }

    //! use existing alloc with delete[]
    shared_array(E* a, size_t len)
        :base_t(a, len)
    {}

    //! use existing alloc w/ custom deletor
    template<typename B>
    shared_array(E* a, B b, size_t len)
        :base_t(a, b, len)
    {}

    //! build around existing shared_ptr
    shared_array(const std::shared_ptr<E>& a, size_t len)
        :base_t(a, len)
    {}

    //! alias existing shared_array
    template<typename A>
    shared_array(const std::shared_ptr<A>& a, E* b, size_t len)
        :base_t(a, b, len)
    {}

    size_t max_size() const noexcept {return ((size_t)-1)/sizeof(E);}

    inline void reserve(size_t i) {}

    //! Extend size.  Implies make_unique()
    void resize(size_t i) {
        if(!this->unique() || i!=this->_size) {
            shared_array o(i);
            std::copy_n(this->begin(), std::min(this->size(), i), o.begin());
            this->swap(o);
        }
    }

    //! Ensure exclusive ownership of array data
    inline void make_unique() {
        this->resize(this->size());
    }

private:
    /* Hack alert.
     * For reasons of simplicity and efficiency, we want to use raw pointers for iteration.
     * However, shared_ptr::get() isn't defined when !_data, although practically it gives NULL.
     * Unfortunately, many of the MSVC (<= VS 2010) STL methods assert() that iterators are never NULL.
     * So we fudge here by abusing 'this' so that our iterators are always !NULL.
     */
    inline E* base_ptr() const {
#if defined(_MSC_VER) && _MSC_VER<=1600
        return this->_size ? this->_data.get() : (E*)(this-1);
#else
        return this->_data.get();
#endif
    }
public:
    // STL iterators

    //! begin iteration
    inline iterator begin() const noexcept{return this->base_ptr();}
    inline const_iterator cbegin() const noexcept{return begin();}

    //! end iteration
    inline iterator end() const noexcept{return this->base_ptr()+this->_size;}
    inline const_iterator cend() const noexcept{return end();}

    inline reverse_iterator rbegin() const noexcept{return reverse_iterator(end());}
    inline const_reverse_iterator crbegin() const noexcept{return rbegin();}

    inline reverse_iterator rend() const noexcept{return reverse_iterator(begin());}
    inline const_reverse_iterator crend() const noexcept{return rend();}

    inline reference front() const noexcept{return (*this)[0];}
    inline reference back() const noexcept{return (*this)[this->m_count-1];}

    //! @brief Member access
    //! @pre !empty() && i<size()
    //! Use sa.data() instead of &sa[0]
    inline reference operator[](size_t i) const noexcept {return this->_data.get()[i];}

    //! @brief Member access
    //! @throws std::out_of_range if empty() || i>=size().
    reference at(size_t i) const
    {
        if(i>this->_size)
            throw std::out_of_range("Index out of bounds");
        return (*this)[i];
    }

    //! Cast to const, consuming this
    //! @pre unique()==true
    //! @post empty()==true
    //! @throws std::logic_error if !unique()
    shared_array<typename std::add_const<E>::type>
    freeze() {
        if(!this->unique())
            throw std::logic_error("Can't freeze non-unique shared_array");

        // alias w/ implied cast to const.
        shared_array<typename std::add_const<E>::type> ret(this->_data, this->_data.get(), this->_size);

        // c++20 provides a move()-able alternative to the aliasing constructor.
        // until this stops being the future, we consume the src ref. and
        // inc. + dec. the ref counter...
        this->clear();
        return ret;
    }

    //! static_cast<TO>() to non-void, preserving const-ness
    template<typename TO, typename std::enable_if<!std::is_void<TO>{} && (std::is_const<E>{} == std::is_const<TO>{}), int>::type =0>
    shared_array<TO>
    castTo() const {
        auto alen = this->_size*sizeof(E)/sizeof(TO);
        return shared_array<TO>(this->_data, static_cast<TO*>(this->_data.get()), alen);
    }

    //! static_cast<TO>() to void, preserving const-ness
    template<typename TO, typename std::enable_if<std::is_void<TO>{} && (std::is_const<E>{} == std::is_const<TO>{}), int>::type =0>
    shared_array<TO>
    castTo() const {
        auto alen = this->_size*sizeof(E);
        return shared_array<TO>(this->_data, this->_data.get(), alen); // implied cast to void*
    }
};


template<typename E>
class shared_array<E, typename std::enable_if<std::is_void<E>{}>::type >
    : public detail::sa_base<E>
{
    static_assert (std::is_void<E>::value, "void specialization");

    template<typename E1, class Enable1> friend class shared_array;

    typedef detail::sa_base<E> base_t;
    typedef typename std::remove_const<E>::type _E_non_const;

    ArrayType _type;
public:
    typedef E value_type;
    typedef E* pointer;
    typedef std::ptrdiff_t difference_type;
    typedef size_t size_type;

    //! empty array, untyped
    constexpr shared_array() noexcept :base_t(), _type(ArrayType::Null) {}
    //! empty array, typed
    constexpr explicit shared_array(ArrayType code) noexcept :base_t(), _type(code) {}
    //! copy
    shared_array(const shared_array& o) = default;
    //! move
    inline shared_array(shared_array&& o) noexcept
        :base_t(std::move(o))
        ,_type(o._type)
    {
        o._type = ArrayType::Null;
    }
    //! assign
    shared_array& operator=(const shared_array&) =default;
    //! move
    inline shared_array& operator=(shared_array&& o) noexcept
    {
        base_t::operator=(std::move(o));
        _type = o._type;
        o._type = ArrayType::Null;
        return *this;
    }

    //! use existing alloc with delete[]
    shared_array(E* a, size_t len)
        :base_t(a, len)
        ,_type(detail::CaptureCode<typename std::remove_cv<E>::type>::code)
    {}

    //! use existing alloc w/ custom deletor
    template<typename B>
    shared_array(E* a, B b, size_t len)
        :base_t(a, b, len)
        ,_type(detail::CaptureCode<typename std::remove_cv<E>::type>::code)
    {}

    //! build around existing shared_ptr and length
    shared_array(const std::shared_ptr<E>& a, size_t len)
        :base_t(a, len)
        ,_type(detail::CaptureCode<typename std::remove_cv<E>::type>::code)
    {}

    //! alias existing shared_ptr and length
    template<typename A>
    shared_array(const std::shared_ptr<A>& a, E* b, size_t len)
        :base_t(a, b, len)
        ,_type(detail::CaptureCode<typename std::remove_cv<A>::type>::code)
    {}

private:
    template<typename A>
    shared_array(const std::shared_ptr<A>& a, E* b, size_t len, ArrayType code)
        :base_t(a, b, len)
        ,_type(code)
    {}
public:

    //! clear data and become untyped
    void clear() noexcept {
        base_t::clear();
        _type = ArrayType::Null;
    }

    //! exchange
    void swap(shared_array& o) noexcept {
        base_t::swap(o);
        std::swap(_type, o._type);
    }

    size_t max_size() const noexcept{return (size_t)-1;}

    inline ArrayType original_type() const { return _type; }

    shared_array<typename std::add_const<E>::type>
    freeze() {
        if(!this->unique())
            throw std::logic_error("Can't freeze non-unique shared_array");

        // alias w/ implied cast to const.
        shared_array<typename std::add_const<E>::type> ret(this->_data, this->_data.get(), this->_size, this->_type);

        // c++20 provides a move()-able alternative to the aliasing constructor.
        // until this stops being the future, we consume the src ref. and
        // inc. + dec. the ref counter...
        this->clear();
        return ret;
    }

    // static_cast<TO>() to non-void, preserving const-ness
    template<typename TO, typename std::enable_if<!std::is_void<TO>{} && (std::is_const<E>{} == std::is_const<TO>{}), int>::type =0>
    shared_array<TO>
    castTo() const {
        auto alen = this->_size/sizeof(TO);
        return shared_array<TO>(this->_data, static_cast<TO*>(this->_data.get()), alen);
    }

    // static_cast<TO>() to void, preserving const-ness
    template<typename TO, typename std::enable_if<std::is_void<TO>{} && (std::is_const<E>{} == std::is_const<TO>{}), int>::type =0>
    shared_array<TO>
    castTo() const {
        // in reality this is either void -> void, or const void -> const void
        // aka. simple copy
        return *this;
    }
};

// non-const -> const
template <typename SRC>
static inline
shared_array<typename std::add_const<typename SRC::value_type>::type>
freeze(SRC&& src)
{
    return src.freeze();
}

// change type, while keeping same const
template<typename TO, typename FROM>
static inline
shared_array<TO>
shared_array_static_cast(const shared_array<FROM>& src)
{
    return src.template castTo<TO>();
}

template<typename E, typename std::enable_if<!std::is_void<E>{}, int>::type =0>
std::ostream& operator<<(std::ostream& strm, const shared_array<E>& arr)
{
    strm<<'{'<<arr.size()<<"}[";
    for(size_t i=0; i<arr.size(); i++) {
        if(i>10) {
            strm<<"...";
            break;
        }
        strm<<arr[i];
        if(i+1<arr.size())
            strm<<", ";
    }
    strm<<']';
    return strm;
}

PVXS_API
std::ostream& operator<<(std::ostream& strm, const shared_array<const void>& arr);

PVXS_API
std::ostream& operator<<(std::ostream& strm, const shared_array<void>& arr);

} // namespace pvxs

#endif // PVXS_SHAREDVECTOR_H
