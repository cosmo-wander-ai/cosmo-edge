#ifndef COMMON_TPU_FP16_HPP
#define COMMON_TPU_FP16_HPP
#include<iostream>
#include<string>

namespace tpu {

#include "tpu_fp16.h"
template<typename T>
static inline T to(const fp16& v){
    (void)v;
    return T{};
}

template<> fp32 to<fp32>(const fp16& v){
    return fp16_to_fp32(v);
}

template<> float to<float>(const fp16& v){
    return fp16_to_fp32(v).fval;
}

template<typename T>
static inline T to(const bf16& v){
    (void)v;
    return T{};
}

template<> fp32 to<fp32>(const bf16& v){
    return bf16_to_fp32(v);
}

template<> float to<float>(const bf16& v){
    return bf16_to_fp32(v).fval;
}


template<typename T>
static inline T to(const fp32& v){
    (void)v;
    return T{};
}

template<> fp16 to<fp16>(const fp32& v){
    return fp32_to_fp16(v);
}
template<> bf16 to<bf16>(const fp32& v){
    return fp32_to_bf16(v);
}

template<> float to<float>(const fp32& v){
    return v.fval;
}

template<typename T>
static inline T to(const float& v){
    fp32 vv;
    vv.fval = v;
    return to<T>(vv);
}

template<typename T>
static inline T to(const std::string& s) {
    (void)s;
    return T{};
}

template<> float to<float>(const std::string& s){
  float val = strtof(s.c_str(), nullptr);
  return val;
}

template<> fp32 to<fp32>(const std::string& s){
  auto val = to<float>(s);
  return to<fp32>(val);
}

template<> fp16 to<fp16>(const std::string& s){
  auto val = to<float>(s);
  return to<fp16>(val);
}

template<> bf16 to<bf16>(const std::string& s){
  auto val = to<float>(s);
  return to<bf16>(val);
}


static inline std::ostream& operator<< (std::ostream& os, const bf16& v) {
    os<<to<float>(v);
    return os;
}
static inline std::ostream& operator<< (std::ostream& os, const fp16& v) {
    os<<to<float>(v);
    return os;
}
static inline std::ostream& operator<< (std::ostream& os, const fp32& v){
    os<<to<float>(v);
    return os;
}

static inline std::istream& operator>> (std::istream& is, fp16& v) {
    float fv;
    is>>fv;
    v = to<fp16>(fv);
    return is;
}
static inline std::istream& operator>> (std::istream& is, bf16& v) {
    float fv;
    is>>fv;
    v = to<bf16>(fv);
    return is;
}
static inline std::istream& operator>> (std::istream& is, fp32& v) {
    float fv;
    is>>fv;
    v = to<fp32>(fv);
    return is;
}

#define UNARY_OPERATOR(type, op) \
static inline float operator op (const type& a){\
    return op to<float>(a);\
}

UNARY_OPERATOR(fp16, +)
UNARY_OPERATOR(bf16, +)
UNARY_OPERATOR(fp16, -)
UNARY_OPERATOR(bf16, -)

#define OPERATOR_FOR(type, op, op_str, out_type)                   \
  static inline out_type operator op(const type& a, const type& b) \
  {                                                                \
    return type##_##op_str(a, b);                                  \
  }

#define ARITH_OPERATOR(type, op, op_str) OPERATOR_FOR(type, op, op_str, type)
#define LOGIC_OPERATOR(type, op, op_str) OPERATOR_FOR(type, op, op_str, bool)

ARITH_OPERATOR(fp16, +, add)
ARITH_OPERATOR(fp16, -, sub)
ARITH_OPERATOR(fp16, *, mul)
ARITH_OPERATOR(bf16, +, add)
ARITH_OPERATOR(bf16, -, sub)
ARITH_OPERATOR(bf16, *, mul)

LOGIC_OPERATOR(fp16, >, gt)
LOGIC_OPERATOR(fp16, <, lt)
LOGIC_OPERATOR(fp16, ==, eq)
LOGIC_OPERATOR(fp16, !=, neq)

LOGIC_OPERATOR(bf16, >, gt)
LOGIC_OPERATOR(bf16, <, lt)
LOGIC_OPERATOR(bf16, ==, eq)
LOGIC_OPERATOR(bf16, !=, neq)

#define COMMON_LOGIC_OPERATOR(op, LT, RT)                  \
  static inline bool operator op(const LT& a, const RT& b) \
  {                                                        \
    return to<float>(a) op to<float>(b);                   \
  }

#define BINARY_LOGIC_OPERATOR(LT, RT) \
  COMMON_LOGIC_OPERATOR(>, LT, RT)    \
  COMMON_LOGIC_OPERATOR(<, LT, RT)    \
  COMMON_LOGIC_OPERATOR(==, LT, RT)   \
  COMMON_LOGIC_OPERATOR(!=, LT, RT)   \
  COMMON_LOGIC_OPERATOR(>=, LT, RT)   \
  COMMON_LOGIC_OPERATOR(<=, LT, RT)   \
  COMMON_LOGIC_OPERATOR(>, RT, LT)    \
  COMMON_LOGIC_OPERATOR(<, RT, LT)    \
  COMMON_LOGIC_OPERATOR(==, RT, LT)   \
  COMMON_LOGIC_OPERATOR(!=, RT, LT)   \
  COMMON_LOGIC_OPERATOR(>=, RT, LT)   \
  COMMON_LOGIC_OPERATOR(<=, RT, LT)

BINARY_LOGIC_OPERATOR(fp16, fp32)
BINARY_LOGIC_OPERATOR(fp16, float)
BINARY_LOGIC_OPERATOR(fp16, bf16)
BINARY_LOGIC_OPERATOR(fp32, bf16)
BINARY_LOGIC_OPERATOR(fp32, float)
BINARY_LOGIC_OPERATOR(bf16, float)

#define COMMON_OPERATOR(op, LT, RT)                         \
  static inline float operator op(const LT& a, const RT& b) \
  {                                                         \
    return to<float>(a) op to<float>(b);                    \
  }

#define BINARY_OPERATOR(LT, RT) \
  COMMON_OPERATOR(+, LT, RT)    \
  COMMON_OPERATOR(-, LT, RT)    \
  COMMON_OPERATOR(*, LT, RT)    \
  COMMON_OPERATOR(/, LT, RT)    \
  COMMON_OPERATOR(+, RT, LT)    \
  COMMON_OPERATOR(-, RT, LT)    \
  COMMON_OPERATOR(*, RT, LT)    \
  COMMON_OPERATOR(/, RT, LT)

BINARY_OPERATOR(fp16, fp32)
BINARY_OPERATOR(fp16, float)
BINARY_OPERATOR(fp16, bf16)
BINARY_OPERATOR(fp32, bf16)
BINARY_OPERATOR(fp32, float)
BINARY_OPERATOR(bf16, float)

#undef UNARY_OPERATOR
#undef LOGIC_OPERATOR
#undef ARITH_OPERATOR
#undef OPERATOR_FOR
#undef COMMON_OPERATOR
#undef BINARY_OPERATOR
#undef COMMON_LOGIC_OPERATOR
#undef BINARY_LOGIC_OPERATOR

}
#endif
