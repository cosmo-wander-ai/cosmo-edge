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

#ifndef __X_JSON_READER_H
#define __X_JSON_READER_H

#include <stdexcept>
#include <fstream>
#include <memory>

#include "rapidjson_custom.h"
#include <rapidjson/document.h>

#include "xreader.h"

namespace x2struct {


class JsonReader:public XReader<JsonReader> {
public:
    friend class XReader<JsonReader>;
    using xdoc_type::convert;

    JsonReader(const std::string& str, bool isfile=false):xdoc_type(0, ""),_doc(std::make_shared<rapidjson::Document>()),_val(_doc.get()) {
        std::string err;
        std::string data;

        do {
            if (isfile) {
               std::ifstream fs(str.c_str(), std::ifstream::binary);
               if (!fs) {
                   err = "Open file["+str+"] fail.";
                   break;
               }
               std::string _tmp((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
               data.swap(_tmp);
               _doc->Parse(data); //Insitu((char *)_raw.c_str());
            } else  {
               _doc->Parse(str); //Insitu((char *)_raw.c_str());
            }
			/*错误解析*/
            if (_doc->HasParseError()) {
                size_t offset = _doc->GetErrorOffset();
                if  (isfile) {
                    std::string err_info = data.substr(offset, offset+32);
                    err = "Parse json file ["+str+"] fail. "+err_info;
                    break;
                } else {
                    std::string err_info = str.substr(offset, offset+32);
                    err = "Parse json string ["+str+"] fail. "+err_info;
                    break;
                }
            }
            init();
            return;
        } while (false);
        throw x2struct_parse_except(err);
    }

	JsonReader(std::shared_ptr<rapidjson::Document> doc) :xdoc_type(0, ""), _doc(doc), _val(_doc.get()) {
		std::string data;
		do {
			/*错误解析*/
			if (_doc->HasParseError()) {
				//size_t offset = _doc->GetErrorOffset();

				//std::string err_info = str.substr(offset, offset + 32);
				//err = "Parse json string [" + str + "] fail. " + err_info;
                throw x2struct_parse_except("Parse error");
            }
			init();
			return;
		} while (false);
	}

    ~JsonReader() {
        if (0 != _iter) {
            delete _iter;
            _iter = 0;
        }
    }
public: // convert
    #define XTOSTRUCT_JSON_GETVAL(f, ...)           \
        const rapidjson::Value *v = get_val(key);   \
        if (NULL != v && !v->IsNull()) {            \
            try {                                   \
                val = __VA_ARGS__ v->f();           \
            } catch (const std::exception&e) {      \
                read_exception(e.what(), key);      \
            }                                       \
            return true;                            \
        } else return false

    bool convert(const char*key, std::string &val) {
        XTOSTRUCT_JSON_GETVAL(GetString);
    }
    bool convert(const char*key, bool &val) {
        /*try {
            XTOSTRUCT_JSON_GETVAL(GetBool);
        } catch (...) {
            XTOSTRUCT_JSON_GETVAL(GetInt64, (bool));
        }*/
        const rapidjson::Value *v = get_val(key);
        if (NULL == v) {
            return false;
        } else if (v->IsBool()) {
            val = v->GetBool();
            return true;
        } else if (v->IsInt64()) {
            val = (0 != (v->GetInt64()));
            return true;
        } else {
            read_exception("wish bool, but not bool or int", key);
            return false;
        }
    }
    bool convert(const char*key, int8_t &val) {
        XTOSTRUCT_JSON_GETVAL(GetInt, (int8_t));
    }
    bool convert(const char*key, uint8_t &val) {
        XTOSTRUCT_JSON_GETVAL(GetInt, (uint8_t));
    }
    bool convert(const char*key, int16_t &val) {
        XTOSTRUCT_JSON_GETVAL(GetInt, (int16_t));
    }
    bool convert(const char*key, uint16_t &val) {
        XTOSTRUCT_JSON_GETVAL(GetInt, (uint16_t));
    }
    bool convert(const char*key, int32_t &val) {
        XTOSTRUCT_JSON_GETVAL(GetInt);
    }
    bool convert(const char*key, uint32_t &val) {
        XTOSTRUCT_JSON_GETVAL(GetUint);
    }
    bool convert(const char*key, int64_t &val) {
        XTOSTRUCT_JSON_GETVAL(GetInt64);
    }
    bool convert(const char*key, uint64_t &val) {
       XTOSTRUCT_JSON_GETVAL(GetUint64);
    }
    bool convert(const char*key, double &val) {
        XTOSTRUCT_JSON_GETVAL(GetDouble);
    }
    bool convert(const char*key, float &val) {
        XTOSTRUCT_JSON_GETVAL(GetFloat);
    }

    const std::string& type() {
        static std::string t("json");
        return t;
    }
    bool has(const char*key) {
        if (_val->HasMember(key)) {
            return !(*_val)[key].IsNull();
        } else {
            return false;
        }
    }
    size_t size(bool to_vec=true) {
        (void)to_vec;
        if (_val->IsArray()) {
            return (size_t)_val->Size();
        } else {
            return 0;
        }
    }
    JsonReader operator[](const char *key) {
        if (_val->HasMember(key)) {
            return JsonReader(&(*_val)[key], this, key);
        } else {
            throw x2struct_parse_except(std::string("Did not have ") + key);
        }
        return JsonReader(0, 0, "");
    }
    JsonReader operator[](size_t index) {
        if (_val->IsArray()) {
            return JsonReader(&(*_val)[(rapidjson::SizeType)index], this, index);
        } else {
            throw x2struct_parse_except("Out of index");
        }
        return JsonReader(0, 0, "");
    }
    JsonReader begin() {
        if (_iter != 0) {
            delete _iter;
        }
        _iter = new(rapidjson::Value::ConstMemberIterator);
        *_iter = _val->MemberBegin();
        if (*_iter != _val->MemberEnd()) {
            return JsonReader(&(*_iter)->value, this, (*_iter)->name.GetString());
        } else {
            return JsonReader(0, this, "");
        }
    }
    JsonReader next() {
        if (0 == _parent) {
            throw std::runtime_error("parent null");
        } else if (0 == _parent->_iter) {
            throw std::runtime_error("parent no iter");
        } else {
            ++(*_parent->_iter);
        }
        if (*_parent->_iter != _parent->_val->MemberEnd()) {
            return JsonReader(&(*_parent->_iter)->value, _parent, (*_parent->_iter)->name.GetString());
        } else {
            return JsonReader(0, _parent, "");
        }
    }
    operator bool() const {
        return 0 != _val;
    }

private:
    JsonReader():xdoc_type(0, ""),_doc(0),_val(0) {
        init();
    }
    JsonReader(const rapidjson::Value* val, const JsonReader*parent, const char*key):xdoc_type(parent, key),_doc(0),_val(val) {
        init();
    }
    JsonReader(const rapidjson::Value* val, const JsonReader*parent, size_t index):xdoc_type(parent, index),_doc(0),_val(val) {
        init();
    }
    void init() {
        _iter = 0;
    }
    
    JsonReader* child(const char*key, JsonReader*tmp) {
        rapidjson::Value::ConstMemberIterator iter;
        if (NULL!=_val && _val->MemberEnd()!=(iter=_val->FindMember(key))) {
            tmp->_key = key;
            tmp->_parent = this;
            tmp->_val = &iter->value;
            return tmp;
        } else {
            return NULL;
        }
    }

    const rapidjson::Value* get_val(const char *key) {
        if (NULL == key) {
            return _val;
        } else if (NULL != _val) {
            rapidjson::Value::ConstMemberIterator iter = _val->FindMember(key);
            if (iter != _val->MemberEnd()) {
                return &iter->value;
            } else {
                return NULL;
            }
        } else {
            return NULL;
        }
    }

    //std::string _raw;
    //rapidjson::Document* _doc;
	std::shared_ptr<rapidjson::Document> _doc;
	const rapidjson::Value* _val;
    //const rapidjson::Value* _val;
    mutable rapidjson::Value::ConstMemberIterator* _iter;
};

}

#endif
