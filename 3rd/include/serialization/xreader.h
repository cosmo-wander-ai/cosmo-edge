/*
* Copyright (C) 2019 YY Inc. All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License"); 
* you may not use this file except in compliance with the License. 
* You may obtain a copy of the License at
*
*	http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, 
* software distributed under the License is distributed on an "AS IS" BASIS, 
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
* See the License for the specific language governing permissions and 
* limitations under the License.
*/


#ifndef __X_READER_H
#define __X_READER_H


#include "xstdint.h"
#include <string>
#include <map>
#include <vector>
#include <list>
#include <set>
#include <stdexcept>
//#include <iostream>

#ifdef XTOSTRUCT_SUPPORT_CHAR_ARRAY
#include <string.h>
#endif

#include "util.h"
#include "traits.h"
#include <memory>

namespace x2struct
{
    //通用异常
    struct x2struct_except : public std::runtime_error
    {

        explicit x2struct_except(const std::string &_Message)
            : std::runtime_error(_Message)
        {
        }

        explicit x2struct_except(const char *_Message)
            : std::runtime_error(_Message)
        {
        }
    };

    //缺少字段异常
    struct x2struct_miss_mandatory_except : public x2struct_except
    {

        explicit x2struct_miss_mandatory_except(const std::string &_Message)
            : x2struct_except(_Message)
        {	// construct from message string
		}

        explicit x2struct_miss_mandatory_except(const char *_Message)
            : x2struct_except(_Message)
        { // construct from message string
        }
    };

    //字段异常
    struct x2struct_parse_except : public x2struct_except
    {

        explicit x2struct_parse_except(const std::string &_Message)
            : x2struct_except(_Message)
        { // construct from message string
        }

        explicit x2struct_parse_except(const char *_Message)
            : x2struct_except(_Message)
        { // construct from message string
        }
    };

/*
  DOC need implement
  bool has(const std::string)
  const std::string& type()
*/
template<typename DOC>
class XReader {
protected:
    typedef DOC doc_type;
    typedef XReader<DOC> xdoc_type;
public:
    // only c++0x support reference initialize, so use pointer
    XReader(const doc_type *parent, const char* key):_parent(parent), _key(key), _index(-1), _set_has(false){}
    XReader(const doc_type *parent, size_t index):_parent(parent), _key(0), _index(int(index)), _set_has(false){}
    ~XReader(){}
public:
    template <typename TYPE>
    bool convert(const char*key, std::vector<TYPE> &val) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        if (NULL == obj) {
            return false;
        }

        size_t s = obj->size();                // [implement] size_t size(bool to_vec=true)
        val.resize(s);
        for (size_t i=0; i<s; ++i) {
            (*obj)[i].convert(NULL, val[i]);   // [implement] doc_type operator[](size_t)
        }
        return true;
    }

    template <typename TYPE>
    bool convert(const char*key, std::vector<std::shared_ptr<TYPE>> &val) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        if (NULL == obj) {
            return false;
        }

        size_t s = obj->size();                // [implement] size_t size(bool to_vec=true)
        val.resize(s);
        for (size_t i=0; i<s; ++i) {
            val[i] = std::make_shared<TYPE>();
            (*obj)[i].convert(NULL, *val[i]);   // [implement] doc_type operator[](size_t)
        }
        return true;
    }

    template <typename TYPE>
    bool convert(const char*key, std::list<TYPE> &val) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        if (NULL == obj) {
            return false;
        }

        size_t s = obj->size();
        for (size_t i=0; i<s; ++i) {
            TYPE _t;
            (*obj)[i].convert(NULL, _t);
            val.push_back(_t);
        }
        return true;
    }

    template <typename TYPE>
    bool convert(const char*key, std::set<TYPE> &val) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        if (NULL == obj) {
            return false;
        }

        size_t s = obj->size();
        for (size_t i=0; i<s; ++i) {
            TYPE _t;
            (*obj)[i].convert(NULL, _t);
            val.insert(_t);
        }
        return true;
    }

    template <typename TYPE>
    bool convert(const char*key, std::map<std::string,TYPE> &val) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        if (NULL == obj) {
            return false;
        }

        for (doc_type d=obj->begin(); d; d=d.next()) { // [implement] doc_type begin(); doc_type next(); operator bool() const;
            TYPE _t;
            d.convert(NULL, _t);
            val[d.key()] = _t;
        }
        return true;
    }

    template <typename KEYTYPE, typename TYPE>
    bool convert(const char*key, std::map<KEYTYPE, TYPE> &val) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        if (NULL == obj) {
            return false;
        }

        for (doc_type d=obj->begin(); d; d=d.next()) {
            TYPE _t;
            d.convert(NULL, _t);
            KEYTYPE _k;
            std::string key = d.key();
            if (key[0]!='x') {
                _k = Util::tonum<KEYTYPE>(key);
            } else { // libconfig/xml不支持数字作为key，所以用x开头，比如x11
                _k = Util::tonum<KEYTYPE>(key.substr(1));
            }
            val[_k] = _t;
        }
        return true;
    }

    template <typename TYPE, size_t N>
    bool convert(const char*key, std::array<TYPE, N> &val) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        if (NULL == obj) {
            return false;
        }

        for (size_t i=0; i<N; ++i) {
            (*obj)[i].convert(NULL, val[i]);   // [implement] doc_type operator[](size_t)
        }
        return true;
    }

    // shared_ptr
    template <typename TYPE>
    bool convert(const char*key, std::shared_ptr<TYPE>& val, has_x2struct_tag) {
        if (!val) {
            val.reset(new TYPE{});
        }
        return this->convert(key, *val);
    }
    template <typename TYPE>
    bool convert(const char*key, std::shared_ptr<TYPE> &val, not_has_x2struct_tag) {
        return this->convert(key, *val);
    }
    template <typename TYPE>
    bool convert(const char *key, std::shared_ptr<TYPE> &val)
    {
        return this->convert(
            key, val,
            std::conditional_t<
                std::is_default_constructible_v<TYPE>,
                has_x2struct_tag,
                not_has_x2struct_tag>{});
    }

    // unique_ptr
    template <typename TYPE>
    bool convert(const char*key, std::unique_ptr<TYPE>& val, has_x2struct_tag) {
        if (!val) {
            val.reset(new TYPE{});
        }
        return this->convert(key, *val);
    }
    template <typename TYPE>
    bool convert(const char*key, std::unique_ptr<TYPE> &val, not_has_x2struct_tag) {
        return this->convert(key, *val);
    }
    template <typename TYPE>
    bool convert(const char *key, std::unique_ptr<TYPE> &val)
    {
        return this->convert(
            key, val,
            std::conditional_t<
                std::is_default_constructible_v<TYPE>,
                has_x2struct_tag,
                not_has_x2struct_tag>{});
    }

    // class/struct that not defined macro XTOSTRUCT
    template <typename TYPE>
    bool convert(const char*key, TYPE& val, not_has_x2struct_tag) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        bool ret = false;

        do {
            if (NULL == obj) {
                break;
            }
            x_str_to_struct(*obj, val);
            ret = true;
        } while(false);

        return ret;
    }

    // class/struct that defined macro XTOSTRUCT
    template <typename TYPE>
    bool convert(const char*key, TYPE& val, has_x2struct_tag) {
        doc_type tmp;
        doc_type *obj = get_obj(key, &tmp);
        bool ret = false;

        do {
            if (NULL == obj) {
                break;
            }

            size_t len = obj->size(false);
            if (len <= 1) {
                val.__x_to_struct(*obj);
                ret = true;
            }
        } while(false);
        return ret;
    }

    template <typename TYPE>
    bool convert(const char*key, TYPE& val, x2struct_string_tag) {
        std::string tmp;
        bool result = (static_cast<doc_type*>(this))->convert(key, tmp);
        if(result)
        {
            val = std::move(tmp);
        }
        return result;
    }

    template <typename TYPE>
    bool convert(const char*key, TYPE& val, x2struct_bool_tag) {
        bool tmp;
        bool result = (static_cast<doc_type*>(this))->convert(key, tmp);
        if(result)
        {
            val = tmp;
        }
        return result;
    }

    template <typename TYPE>
    bool convert(const char*key, TYPE& val, x2struct_double_tag) {
        double tmp;
        bool result = (static_cast<doc_type*>(this))->convert(key, tmp);
        if(result)
        {
            val = tmp;
        }
        return result;
    }

    template <typename TYPE>
    bool convert(const char*key, TYPE& val, x2struct_int64_tag) {
        int64_t tmp;
        bool result = (static_cast<doc_type*>(this))->convert(key, tmp);
        if(result)
        {
            val = tmp;
        }
        return result;
    }

    template <typename TYPE>
    bool convert(const char*key, TYPE& val, x2struct_uint64_tag) {
        uint64_t tmp;
        bool result = (static_cast<doc_type*>(this))->convert(key, tmp);
        if(result)
        {
            val = tmp;
        }
        return result;
    }

    template <typename TYPE>
    bool convert(const char*key, TYPE& val) 
    {
        return convert(key, val, typename x_has_x2struct<TYPE>::type{});
    }

    // for enum
    template <typename TYPE>
    bool convert(const char *key, TYPE &val, x2struct_enum_tag)
    {
        int64_t tmp;
        bool result = (static_cast<doc_type *>(this))->convert(key, tmp);
        if (result)
        {
            val = static_cast<TYPE>(tmp);
        }
        return result;
    }

#ifdef XTOSTRUCT_SUPPORT_CHAR_ARRAY
    bool convert(const char*key, char val[]) {
        std::string str;
        bool ret = ((doc_type*)this)->convert(key, str);
        if (ret) {
            strncpy(val, str.data(), str.length());
            val[str.length()] = '\0';
        }
        return ret;
    }
    #endif

    std::string attribute(const char* key) {
        std::string val;
        (static_cast<doc_type*>(this))->convert(key, val);
        return val;
    }

    const std::string key() const {
        if (0 != _key) {
            return _key;
        } else {
            return Util::tostr(_index);
        }
    }

    const char* key_char() const {
        if (0 != _key) {
            return _key;
        } else {
            return "";
        }
    }

    std::string hasa(const std::string&key, const std::string&alias, bool *md) {
        return Util::alias_parse(key, alias, static_cast<doc_type*>(this)->type(), md);
    }
    std::string path() {
        std::vector<std::string> nodes;
        const doc_type* tmp = static_cast<doc_type*>(this);
        while (tmp) {
            std::string k;
            k.reserve(32);
            if (0 != tmp->_key) {
                if (0!=tmp->_parent && 0!=tmp->_parent->_parent) {
                    k.append(".");
                }
                k.append(tmp->_key);
            } else {
                k.append("[").append(Util::tostr(tmp->_index)).append("]");
            }
            nodes.push_back(k);
            tmp = tmp->_parent;
        }
        std::string p;
        p.reserve(64);
        for (int i=(int)nodes.size()-1; i>=0; --i) {
            p.append(nodes[i]);
        }
        return p;
    }
    void md_exception(const std::string&key) {
		std::string err;
		err.reserve(128);
		err.append("miss mandatory node ");
		std::string p = path();
		if (!p.empty()) {
			err.append(p).append(".");
		}
		err.append(key);
		throw x2struct_miss_mandatory_except(err);
    }
    void read_exception(const char* what, const std::string&key) {
        std::string err;
        err.reserve(128);
        if (NULL != what) {
            err.append(what);
        }
        err.append("[");
        std::string p = path();
        if (!p.empty()) {
            err.append(p).append(".");
        }
        err.append(key);
        err.append("]");
        throw x2struct_parse_except(err);
    }

    bool set_has() const {
        return _set_has;
    }
    void set_has(bool set) {
        _set_has = set;
    }
    /*void set_condition(void *parent, cond_f f) {
        if (NULL == parent) {
            this->_p_cond = NULL;
        } else {
            this->_cond.parent = parent;
            this->_cond.cond = f;
            this->_p_cond = &this->_cond;
        }
    }*/
protected:
    doc_type* get_obj(const char *key, doc_type *tmp) {
        doc_type *obj = static_cast<doc_type*>(this);
        if (NULL != key) {
            obj = obj->child(key, tmp);   // [implement] doc_type* child(const char*, doc_type*)
        }
        return obj;
    }
    const doc_type* _parent;
    const char* _key;
    int _index;
    bool _set_has;
/*
private:
    x_condition _cond;
    x_condition *_p_cond;*/
};

} // namespace x2struct

#endif
