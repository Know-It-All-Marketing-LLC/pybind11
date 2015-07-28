/*
    pybind/cast.h: Partial template specializations to cast between
    C++ and Python types

    Copyright (c) 2015 Wenzel Jakob <wenzel@inf.ethz.ch>

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#pragma once

#include <pybind/pytypes.h>
#include <pybind/typeid.h>
#include <map>
#include <array>

NAMESPACE_BEGIN(pybind)
NAMESPACE_BEGIN(detail)

/// Generic type caster for objects stored on the heap
template <typename type> class type_caster {
public:
    typedef instance<type> instance_type;

    static std::string name() { return type_id<type>(); }

    type_caster() {
        auto const& registered_types = get_internals().registered_types;
        auto it = registered_types.find(type_id<type>());
        if (it != registered_types.end())
            typeinfo = &it->second;
    }

    bool load(PyObject *src, bool convert) {
        if (src == nullptr || typeinfo == nullptr)
            return false;
        if (PyType_IsSubtype(Py_TYPE(src), typeinfo->type)) {
            value = ((instance_type *) src)->value;
            return true;
        }
        if (convert) {
            for (auto &converter : typeinfo->implicit_conversions) {
                temp = object(converter(src, typeinfo->type), false);
                if (load(temp.ptr(), false))
                    return true;
            }
        }
        return false;
    }

    static PyObject *cast(const type &src, return_value_policy policy, PyObject *parent) {
        if (policy == return_value_policy::automatic)
            policy = return_value_policy::copy;
        return cast(&src, policy, parent);
    }

    static PyObject *cast(const type *_src, return_value_policy policy, PyObject *parent) {
        type *src = const_cast<type *>(_src);
        if (src == nullptr) {
            Py_INCREF(Py_None);
            return Py_None;
        }
        // avoid an issue with internal references matching their parent's address
        bool dont_cache = policy == return_value_policy::reference_internal &&
                          parent && ((instance<void> *) parent)->value == (void *) src;
        auto& internals = get_internals();
        auto it_instance = internals.registered_instances.find(src);
        if (it_instance != internals.registered_instances.end() && !dont_cache) {
            PyObject *inst = it_instance->second;
            Py_INCREF(inst);
            return inst;
        }
        auto it = internals.registered_types.find(type_id<type>());
        if (it == internals.registered_types.end()) {
            std::string msg = std::string("Unregistered type : ") + type_id<type>();
            PyErr_SetString(PyExc_TypeError, msg.c_str());
            return nullptr;
        }
        auto &type_info = it->second;
        instance_type *inst = (instance_type *) PyType_GenericAlloc(type_info.type, 0);
        inst->value = src;
        inst->owned = true;
        inst->parent = nullptr;
        if (policy == return_value_policy::automatic)
            policy = return_value_policy::take_ownership;
        handle_return_value_policy<type>(inst, policy, parent);
        PyObject *inst_pyobj = (PyObject *) inst;
        type_info.init_holder(inst_pyobj);
        if (!dont_cache)
            internals.registered_instances[inst->value] = inst_pyobj;
        return inst_pyobj;
    }

    template <class T, typename std::enable_if<std::is_copy_constructible<T>::value, int>::type = 0>
    static void handle_return_value_policy(instance<T> *inst, return_value_policy policy, PyObject *parent) {
        if (policy == return_value_policy::copy) {
            inst->value = new T(*(inst->value));
        } else if (policy == return_value_policy::reference) {
            inst->owned = false;
        } else if (policy == return_value_policy::reference_internal) {
            inst->owned = false;
            inst->parent = parent;
            Py_XINCREF(parent);
        }
    }

    template <class T, typename std::enable_if<!std::is_copy_constructible<T>::value, int>::type = 0>
    static void handle_return_value_policy(instance<T> *inst, return_value_policy policy, PyObject *parent) {
        if (policy == return_value_policy::copy) {
            throw cast_error("return_value_policy = copy, but the object is non-copyable!");
        } else if (policy == return_value_policy::reference) {
            inst->owned = false;
        } else if (policy == return_value_policy::reference_internal) {
            inst->owned = false;
            inst->parent = parent;
            Py_XINCREF(parent);
        }
    }

    operator type*() { return value; }
    operator type&() { return *value; }
protected:
    type *value = nullptr;
    const type_info *typeinfo = nullptr;
    object temp;
};

#define PYBIND_TYPE_CASTER(type, py_name) \
    protected: \
        type value; \
    public: \
        static std::string name() { return py_name; } \
        static PyObject *cast(const type *src, return_value_policy policy, PyObject *parent) { \
            return cast(*src, policy, parent); \
        } \
        operator type*() { return &value; } \
        operator type&() { return value; } \

#define PYBIND_TYPE_CASTER_NUMBER(type, py_type, from_type, to_pytype) \
    template <> class type_caster<type> { \
    public: \
        bool load(PyObject *src, bool) { \
            value = (type) from_type(src); \
            if (value == (type) -1 && PyErr_Occurred()) { \
                PyErr_Clear(); \
                return false; \
            } \
            return true; \
        } \
        static PyObject *cast(type src, return_value_policy /* policy */, PyObject * /* parent */) { \
            return to_pytype((py_type) src); \
        } \
        PYBIND_TYPE_CASTER(type, #type); \
    };

PYBIND_TYPE_CASTER_NUMBER(int32_t, long, PyLong_AsLong, PyLong_FromLong)
PYBIND_TYPE_CASTER_NUMBER(uint32_t, unsigned long, PyLong_AsUnsignedLong, PyLong_FromUnsignedLong)
PYBIND_TYPE_CASTER_NUMBER(int64_t, PY_LONG_LONG, PyLong_AsLongLong, PyLong_FromLongLong)
PYBIND_TYPE_CASTER_NUMBER(uint64_t, unsigned PY_LONG_LONG, PyLong_AsUnsignedLongLong, PyLong_FromUnsignedLongLong)

#if defined(__APPLE__) // size_t/ssize_t are separate types on Mac OS X
PYBIND_TYPE_CASTER_NUMBER(ssize_t, Py_ssize_t, PyLong_AsSsize_t, PyLong_FromSsize_t)
PYBIND_TYPE_CASTER_NUMBER(size_t, size_t, PyLong_AsSize_t, PyLong_FromSize_t)
#endif

PYBIND_TYPE_CASTER_NUMBER(float, float, PyFloat_AsDouble, PyFloat_FromDouble)
PYBIND_TYPE_CASTER_NUMBER(double, double, PyFloat_AsDouble, PyFloat_FromDouble)

template <> class type_caster<detail::void_type> {
public:
    bool load(PyObject *, bool) { return true; }
    static PyObject *cast(detail::void_type, return_value_policy /* policy */, PyObject * /* parent */) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    PYBIND_TYPE_CASTER(detail::void_type, "None");
};

template <> class type_caster<bool> {
public:
    bool load(PyObject *src, bool) {
        if (src == Py_True) { value = true; return true; }
        else if (src == Py_False) { value = false; return true; }
        else return false;
    }
    static PyObject *cast(bool src, return_value_policy /* policy */, PyObject * /* parent */) {
        PyObject *result = src ? Py_True : Py_False;
        Py_INCREF(result);
        return result;
    }
    PYBIND_TYPE_CASTER(bool, "bool");
};

template <typename T> class type_caster<std::complex<T>> {
public:
    bool load(PyObject *src, bool) {
        Py_complex result = PyComplex_AsCComplex(src);
        if (result.real == -1.0 && PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }
        value = std::complex<T>((T) result.real, (T) result.imag);
        return true;
    }
    static PyObject *cast(const std::complex<T> &src, return_value_policy /* policy */, PyObject * /* parent */) {
        return PyComplex_FromDoubles((double) src.real(), (double) src.imag());
    }
    PYBIND_TYPE_CASTER(std::complex<T>, "complex");
};

template <> class type_caster<std::string> {
public:
    bool load(PyObject *src, bool) {
        const char *ptr = PyUnicode_AsUTF8(src);
        if (!ptr) { PyErr_Clear(); return false; }
        value = std::string(ptr);
        return true;
    }
    static PyObject *cast(const std::string &src, return_value_policy /* policy */, PyObject * /* parent */) {
        return PyUnicode_FromString(src.c_str());
    }
    PYBIND_TYPE_CASTER(std::string, "str");
};

#ifdef HAVE_WCHAR_H
template <> class type_caster<std::wstring> {
public:
    bool load(PyObject *src, bool) {
        const wchar_t *ptr = PyUnicode_AsWideCharString(src, nullptr);
        if (!ptr) { PyErr_Clear(); return false; }
        value = std::wstring(ptr);
        return true;
    }
    static PyObject *cast(const std::wstring &src, return_value_policy /* policy */, PyObject * /* parent */) {
        return PyUnicode_FromWideChar(src.c_str(), src.length());
    }
    PYBIND_TYPE_CASTER(std::wstring, "wstr");
};
#endif

template <> class type_caster<char> {
public:
    bool load(PyObject *src, bool) {
        char *ptr = PyUnicode_AsUTF8(src);
        if (!ptr) { PyErr_Clear(); return false; }
        value = ptr;
        return true;
    }

    static PyObject *cast(const char *src, return_value_policy /* policy */, PyObject * /* parent */) {
        return PyUnicode_FromString(src);
    }

    static PyObject *cast(char src, return_value_policy /* policy */, PyObject * /* parent */) {
        char str[2] = { src, '\0' };
        return PyUnicode_DecodeLatin1(str, 1, nullptr);
    }

    static std::string name() { return "str"; }

    operator char*() { return value; }
    operator char() { return *value; }
protected:
    char *value;
};

template <typename Value> struct type_caster<std::vector<Value>> {
    typedef std::vector<Value> type;
    typedef type_caster<Value> value_conv;
public:
    bool load(PyObject *src, bool convert) {
        if (!PyList_Check(src))
            return false;
        size_t size = (size_t) PyList_GET_SIZE(src);
        value.reserve(size);
        value.clear();
        for (size_t i=0; i<size; ++i) {
            value_conv conv;
            if (!conv.load(PyList_GetItem(src, (ssize_t) i), convert))
                return false;
            value.push_back((Value) conv);
        }
        return true;
    }

    static PyObject *cast(const type &src, return_value_policy policy, PyObject *parent) {
        PyObject *list = PyList_New(src.size());
        size_t index = 0;
        for (auto const &value: src) {
            PyObject *value_ = value_conv::cast(value, policy, parent);
            if (!value_) {
                Py_DECREF(list);
                return nullptr;
            }
            PyList_SetItem(list, index++, value_);
        }
        return list;
    }
    PYBIND_TYPE_CASTER(type, "list<" + value_conv::name() + ">");
};

template <typename Key, typename Value> struct type_caster<std::map<Key, Value>> {
public:
    typedef std::map<Key, Value>  type;
    typedef type_caster<Key>   key_conv;
    typedef type_caster<Value> value_conv;

    bool load(PyObject *src, bool convert) {
        if (!PyDict_Check(src))
            return false;

        value.clear();
        PyObject *key_, *value_;
        ssize_t pos = 0;
        key_conv kconv;
        value_conv vconv;
        while (PyDict_Next(src, &pos, &key_, &value_)) {
            if (!kconv.load(key_, convert) || !vconv.load(value_, convert))
                return false;
            value[kconv] = vconv;
        }
        return true;
    }

    static PyObject *cast(const type &src, return_value_policy policy, PyObject *parent) {
        PyObject *dict = PyDict_New();
        for (auto const &kv: src) {
            PyObject *key   = key_conv::cast(kv.first, policy, parent);
            PyObject *value = value_conv::cast(kv.second, policy, parent);
            if (!key || !value || PyDict_SetItem(dict, key, value) < 0) {
                Py_XDECREF(key);
                Py_XDECREF(value);
                Py_DECREF(dict);
                return nullptr;
            }
            Py_DECREF(key);
            Py_DECREF(value);
        }
        return dict;
    }
    PYBIND_TYPE_CASTER(type, "dict<" + key_conv::name() + ", " + value_conv::name() + ">");
};

template <typename T1, typename T2> class type_caster<std::pair<T1, T2>> {
    typedef std::pair<T1, T2> type;
public:
    bool load(PyObject *src, bool convert) {
        if (!PyTuple_Check(src) || PyTuple_Size(src) != 2)
            return false;
        if (!first.load(PyTuple_GetItem(src, 0), convert))
            return false;
        return second.load(PyTuple_GetItem(src, 1), convert);
    }

    static PyObject *cast(const type &src, return_value_policy policy, PyObject *parent) {
        PyObject *o1 = type_caster<typename detail::decay<T1>::type>::cast(src.first, policy, parent);
        PyObject *o2 = type_caster<typename detail::decay<T2>::type>::cast(src.second, policy, parent);
        if (!o1 || !o2) {
            Py_XDECREF(o1);
            Py_XDECREF(o2);
            return nullptr;
        }
        PyObject *tuple = PyTuple_New(2);
        PyTuple_SetItem(tuple, 0, o1);
        PyTuple_SetItem(tuple, 1, o2);
        return tuple;
    }

    static std::string name() {
        return "(" + type_caster<T1>::name() + ", " + type_caster<T2>::name() + ")";
    }

    operator type() {
        return type(first, second);
    }
protected:
    type_caster<typename detail::decay<T1>::type> first;
    type_caster<typename detail::decay<T2>::type> second;
};

template <typename ... Tuple> class type_caster<std::tuple<Tuple...>> {
    typedef std::tuple<Tuple...> type;
public:
    enum { size = sizeof...(Tuple) };

    bool load(PyObject *src, bool convert) {
        return load(src, convert, typename make_index_sequence<sizeof...(Tuple)>::type());
    }

    static PyObject *cast(const type &src, return_value_policy policy, PyObject *parent) {
        return cast(src, policy, parent, typename make_index_sequence<size>::type());
    }

    static std::string name() {
        std::array<std::string, size> names {{
            type_caster<typename detail::decay<Tuple>::type>::name()...
        }};
        std::string result("(");
        int counter = 0;
        for (auto const &name : names) {
            result += name;
            if (++counter < size)
                result += ", ";
        }
        result += ")";
        return result;
    }

    template <typename ReturnValue, typename Func> typename std::enable_if<!std::is_void<ReturnValue>::value, ReturnValue>::type call(Func &f) {
        return call<ReturnValue, Func>(f, typename make_index_sequence<sizeof...(Tuple)>::type());
    }

    template <typename ReturnValue, typename Func> typename std::enable_if<std::is_void<ReturnValue>::value, detail::void_type>::type call(Func &f) {
        call<ReturnValue, Func>(f, typename make_index_sequence<sizeof...(Tuple)>::type());
        return detail::void_type();
    }

    operator type() {
        return cast(typename make_index_sequence<sizeof...(Tuple)>::type());
    }

protected:
    template <typename ReturnValue, typename Func, size_t ... Index> ReturnValue call(Func &f, index_sequence<Index...>) {
        return f((Tuple) std::get<Index>(value)...);
    }

    template <size_t ... Index> type cast(index_sequence<Index...>) {
        return type((Tuple) std::get<Index>(value)...);
    }

    template <size_t ... Indices> bool load(PyObject *src, bool convert, index_sequence<Indices...>) {
        if (!PyTuple_Check(src))
            return false;
        if (PyTuple_Size(src) != size)
            return false;
        std::array<bool, size> results {{
            std::get<Indices>(value).load(PyTuple_GetItem(src, Indices), convert)...
        }};
        for (bool r : results)
            if (!r)
                return false;
        return true;
    }

    /* Implementation: Convert a C++ tuple into a Python tuple */
    template <size_t ... Indices> static PyObject *cast(const type &src, return_value_policy policy, PyObject *parent, index_sequence<Indices...>) {
        std::array<PyObject *, size> results {{
            type_caster<typename detail::decay<Tuple>::type>::cast(std::get<Indices>(src), policy, parent)...
        }};
        bool success = true;
        for (auto result : results)
            if (result == nullptr)
                success = false;
        if (success) {
            PyObject *tuple = PyTuple_New(size);
            int counter = 0;
            for (auto result : results)
                PyTuple_SetItem(tuple, counter++, result);
            return tuple;
        } else {
            for (auto result : results) {
                Py_XDECREF(result);
            }
            return nullptr;
        }
    }

protected:
    std::tuple<type_caster<typename detail::decay<Tuple>::type>...> value;
};

/// Type caster for holder types like std::shared_ptr, etc.
template <typename type, typename holder_type> class type_caster_holder : public type_caster<type> {
public:
    typedef type_caster<type> parent;
    bool load(PyObject *src, bool convert) {
        if (!parent::load(src, convert))
            return false;
        holder = holder_type(parent::value);
        return true;
    }
    explicit operator type*() { return this->value; }
    explicit operator type&() { return *(this->value); }
    explicit operator holder_type&() { return holder; }
    explicit operator holder_type*() { return &holder; }
protected:
    holder_type holder;
};

template <> class type_caster<handle> {
public:
    bool load(PyObject *src) {
        value = handle(src);
        return true;
    }
    static PyObject *cast(const handle &src, return_value_policy /* policy */, PyObject * /* parent */) {
        src.inc_ref();
        return (PyObject *) src.ptr();
    }
    PYBIND_TYPE_CASTER(handle, "handle");
};

#define PYBIND_TYPE_CASTER_PYTYPE(name) \
    template <> class type_caster<name> { \
    public: \
        bool load(PyObject *src, bool) { value = name(src, true); return true; } \
        static PyObject *cast(const name &src, return_value_policy /* policy */, PyObject * /* parent */) { \
            src.inc_ref(); return (PyObject *) src.ptr(); \
        } \
        PYBIND_TYPE_CASTER(name, #name); \
    };

PYBIND_TYPE_CASTER_PYTYPE(object)  PYBIND_TYPE_CASTER_PYTYPE(buffer)
PYBIND_TYPE_CASTER_PYTYPE(capsule) PYBIND_TYPE_CASTER_PYTYPE(dict)
PYBIND_TYPE_CASTER_PYTYPE(float_)  PYBIND_TYPE_CASTER_PYTYPE(int_)
PYBIND_TYPE_CASTER_PYTYPE(list)    PYBIND_TYPE_CASTER_PYTYPE(slice)
PYBIND_TYPE_CASTER_PYTYPE(tuple)   PYBIND_TYPE_CASTER_PYTYPE(function)

NAMESPACE_END(detail)

template <typename T> inline T cast(PyObject *object) {
    detail::type_caster<typename detail::decay<T>::type> conv;
    if (!conv.load(object, true))
        throw cast_error("Unable to cast Python object to C++ type");
    return conv;
}

template <typename T> inline object cast(const T &value, return_value_policy policy = return_value_policy::automatic, PyObject *parent = nullptr) {
    if (policy == return_value_policy::automatic)
        policy = std::is_pointer<T>::value ? return_value_policy::take_ownership : return_value_policy::copy;
    return object(detail::type_caster<typename detail::decay<T>::type>::cast(value, policy, parent), false);
}

template <typename T> inline T handle::cast() { return pybind::cast<T>(m_ptr); }

template <typename ... Args> inline object handle::call(Args&&... args_) {
    const size_t size = sizeof...(Args);
    std::array<PyObject *, size> args{
        { detail::type_caster<typename detail::decay<Args>::type>::cast(
            std::forward<Args>(args_), return_value_policy::automatic, nullptr)... }
    };
    bool fail = false;
    for (auto result : args)
        if (result == nullptr)
            fail = true;
    if (fail) {
        for (auto result : args) {
            Py_XDECREF(result);
        }
        throw cast_error("handle::call(): unable to convert input arguments to Python objects");
    }
    PyObject *tuple = PyTuple_New(size);
    int counter = 0;
    for (auto result : args)
        PyTuple_SetItem(tuple, counter++, result);
    PyObject *result = PyObject_CallObject(m_ptr, tuple);
    Py_DECREF(tuple);
    return object(result, false);
}

NAMESPACE_END(pybind)
