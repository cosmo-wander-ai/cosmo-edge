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

#ifndef __X_TYPES_H
#define __X_TYPES_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#define __X_DEF_XOPEN
#endif

#include <time.h>
#include "xstdint.h"
#include <string>
#include <stdexcept>

#ifdef __X_DEF_XOPEN
#undef _XOPEN_SOURCE
#endif

namespace x2struct {

struct x_condition_t {
    typedef bool (*cond_f)(void*obj, void*doc);

    void* parent;
    cond_f  cond;

    x_condition_t():parent(0),cond(0){}
    void set(void*p, cond_f c){parent=p; cond=c;}
};

template<typename TYPE>
class XType:public TYPE {
public:
    x_condition_t __x_cond;
    template<class DOC>
    void __x_to_struct(DOC& obj) {
        std::string str;
        obj.convert(NULL, str);
        this->parse(str);
    }
    template<class DOC>
    void __struct_to_str(DOC& obj, const char*key) const {
        std::string str = this->format();
        obj.convert(key, str);
    }
    /*TYPE* operator->() {
        return &_t;
    }
private:
    TYPE _t;*/
};

// YYYY-MM-DD HH:MM:SS
class _XDate {
public:
    int64_t unix_time;   // unix time
public:
    std::string format() const {
        time_t tt = (time_t)unix_time;
        tm     ttm;

        #ifndef WINDOWS
        localtime_r(&tt, &ttm);
        #else
        localtime_s(&ttm, &tt);
        #endif
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ttm);
        return buf;
    }
    void parse(const std::string&str) {
        #ifndef WINDOWS
        tm ttm;

        if (0 != strptime(str.c_str(), "%Y-%m-%d %H:%M:%S", &ttm)) {
            unix_time = mktime(&ttm);
        } else {
            std::string err("invalid time string[");
            err.append(str).append("]. use format YYYY-mm-dd H:M:S. ");
            throw std::runtime_error(err);
        }
        #else
        //unix_time = 1218196800;
        // just parse. not check exception
        static int days[]={31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 32};
        struct tm ttm={0};
        sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &ttm.tm_year, &ttm.tm_mon, &ttm.tm_mday, &ttm.tm_hour, &ttm.tm_min, &ttm.tm_sec);
        ttm.tm_mon-=1; // mon[0-11]
        if (ttm.tm_mon<0 || ttm.tm_mon>11
         || ttm.tm_mday<1 || ttm.tm_mday>days[ttm.tm_mon]
         || ttm.tm_hour<0 || ttm.tm_hour>23
         || ttm.tm_min<0 || ttm.tm_min>59
         || ttm.tm_sec<0 || ttm.tm_sec>61 // leap seconds
         || (ttm.tm_mon==1 && ttm.tm_mday==29 && (ttm.tm_year%4!=0 || ((ttm.tm_year%100==0 && ttm.tm_year%400!=0))))) {
            std::string err("invalid time string[");
            err.append(str).append("]. use format YYYY-mm-dd H:M:S. ");
            throw std::runtime_error(err);
        }
        ttm.tm_year-=1900; // since 1900
        unix_time = mktime(&ttm);
        #endif
    }
};

typedef XType<_XDate> XDate;

}

#endif
