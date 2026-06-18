/*
* Copyright (C) 2019 YY Inc. All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License"); 
* you may not use this file except in compliance with the License. 
* You may obtain a copy of the License at
*
*       http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, 
* software distributed under the License is distributed on an "AS IS" BASIS, 
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
* See the License for the specific language governing permissions and 
* limitations under the License.
*/


#ifndef __X_TO_TRAITS_H
#define __X_TO_TRAITS_H

#include "xstdint.h"

#if __GXX_EXPERIMENTAL_CXX0X__ || _MSC_VER>=1700
#include <type_traits>
#include <memory>
#endif

// using namespace std;

#if __GXX_EXPERIMENTAL_CXX0X__ || _MSC_VER>=1700

#define X_SUPPORT_C0X
#define X_SELF __x_self

namespace x2struct {

struct not_has_x2struct_tag{};
struct has_x2struct_tag{};
struct x2struct_string_tag{};
struct x2struct_bool_tag{};
struct x2struct_double_tag{};
struct x2struct_int64_tag{};
struct x2struct_uint64_tag{};
struct x2struct_enum_tag
{
};

template <typename T>
struct x_has_x2struct
{
    enum { type_must_be_complete = sizeof(T) };
    template<typename C, typename = std::void_t<>> struct helper
    {
        using type = not_has_x2struct_tag;
    };
    template<typename C> struct helper<C, std::void_t<decltype(std::declval<C>().__x_struct_check())>>
    {
        using type = has_x2struct_tag;
    };
    template <typename C>
    struct helper<C, std::void_t<std::enable_if_t<std::is_enum<C>::value, x2struct_enum_tag>>>
    {
        using type = x2struct_enum_tag;
    };
#ifndef _WIN32
    //以下用于支持自定义元素类型
    template<typename C> struct helper<C, std::void_t<decltype(std::declval<C>().operator std::string())>>
    {
        using type = x2struct_string_tag;
    };
    template<typename C> struct helper<C, std::void_t<decltype(std::declval<C>().operator bool())>>
    {
        using type = x2struct_bool_tag;
    };
    template<typename C> struct helper<C, std::void_t<decltype(std::declval<C>().operator double())>>
    {
        using type = x2struct_double_tag;
    };
    template<typename C> struct helper<C, std::void_t<decltype(std::declval<C>().operator int32_t())>>
    {
        using type = x2struct_int64_tag;
    };
    template<typename C> struct helper<C, std::void_t<decltype(std::declval<C>().operator uint32_t())>>
    {
        using type = x2struct_uint64_tag;
    };
    template<typename C> struct helper<C, std::void_t<decltype(std::declval<C>().operator int64_t())>>
    {
        using type = x2struct_int64_tag;
    };
    template<typename C> struct helper<C, std::void_t<decltype(std::declval<C>().operator uint64_t())>>
    {
        using type = x2struct_uint64_tag;
    };
#endif

    using type = typename helper<T>::type;
};

struct x_fake_set {
    void insert(const std::string &s){(void)s;}
};

}


//template <class T>
//using x_for_class = typename std::enable_if<std::is_class<T>::value, int>::type;

//template <class T>
//using x_for_enum = typename std::enable_if<std::is_enum<T>::value, int>::type;

// <c++11 not support using template. so we use macro all
#define x_for_class(T, DEFT) typename std::enable_if<std::is_class<T>::value, DEFT>::type

#define x_for_enum(T)  typename std::enable_if<std::is_enum<T>::value, int>::type
#define x_decltype(T)  decltype(T)

#else  // C11

#define X_SELF this

namespace x2struct {

template<bool B, class T = void>
struct x_enable_if {};

template <class T>
struct x_enable_if<true, T> { typedef T type; };


template <class T>
struct x_tovoid {typedef void type;};

template <class T, typename=void>
struct x_is_class{static bool const value = false;};

template <class T>
struct x_is_class<T, typename x_tovoid<int T::*>::type >{static bool const value = true;};


// thx https://stackoverflow.com/a/12199635/5845104
template<int N> struct x_size { char value[N]; };
x_size<1> x_decltype_encode(int8_t);
x_size<2> x_decltype_encode(uint8_t);
x_size<3> x_decltype_encode(int16_t);
x_size<4> x_decltype_encode(uint16_t);
x_size<5> x_decltype_encode(int32_t);
x_size<6> x_decltype_encode(uint32_t);
x_size<7> x_decltype_encode(int64_t);
x_size<8> x_decltype_encode(uint64_t);


template<int N> struct x_decltype_decode {};
template <> struct x_decltype_decode<1> {typedef int8_t type;};
template <> struct x_decltype_decode<2> {typedef uint8_t type;};
template <> struct x_decltype_decode<3> {typedef int16_t type;};
template <> struct x_decltype_decode<4> {typedef uint16_t type;};
template <> struct x_decltype_decode<5> {typedef int32_t type;};
template <> struct x_decltype_decode<6> {typedef uint32_t type;};
template <> struct x_decltype_decode<7> {typedef int64_t type;};
template <> struct x_decltype_decode<8> {typedef uint64_t type;};



}

#define x_for_class(T, DEFT) typename x2struct::x_enable_if<x2struct::x_is_class<T>::value, DEFT>::type
// too complicated to implement is_enum
#define x_for_enum(T)  typename x2struct::x_enable_if<!x2struct::x_is_class<T>::value, int>::type

#define x_decltype(T) x2struct::x_decltype_decode<sizeof(x2struct::x_decltype_encode(T))>::type

#endif  // C11

#endif  // __X_TO_TRAITS_H

