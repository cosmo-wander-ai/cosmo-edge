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

#ifndef __X_UTIL_H
#define __X_UTIL_H

#include <string>
#include <vector>
//#include <iostream>

#include "xstdint.h"
#include <stdio.h>
#include <string.h>

namespace x2struct {


struct cmp_str {
   bool operator()(char const *a, char const *b) const
   {
      return strcmp(a, b) < 0;
   }
};

class Util {
private:
    template <typename T>
    struct Dummy {
        typedef T type;
    };
public:
    template <typename T>
    static std::string tostr(T data) {
        char buf[64];
        size_t i = sizeof(buf);

        T t = data >= 0 ? data : -data;
        while (true) {
            buf[--i] = static_cast<char>('0'+ t % 10);
            t /= 10;
            if (0 == t) {
                break;
            }
        }
        if (data < 0) {
            buf[--i] = '-';
        }
        return std::string(&buf[i], sizeof(buf)-i);
    }

    static std::string tostr(double data) {
        char buf[64];
        sprintf(buf, "%lf", data);
        return buf;
    }

    static std::string tostr(float data) {
        char buf[64];
        sprintf(buf, "%f", data);
        return buf;
    }

    template <typename T>
    static T tonum(const std::string&str) {
        return tonum_dummy(str, Dummy<T>());
    }

    static size_t split(std::vector<std::string>&slice, const std::string&str, char c) {
        size_t last = 0;
        size_t pos = 0;
        while (std::string::npos != (pos=str.find(c, last))) {
            slice.push_back(str.substr(last, pos-last));
            last = pos+1;
        }

        slice.push_back(str.substr(last));
        return slice.size();
    }

    static size_t split(std::vector<std::string>&slice, const std::string&str, const std::string& sp) {
        size_t last = 0;
        size_t pos = 0;
        size_t len = sp.length();
        if (len == 0) {
            slice.push_back(str);
            return 1;
        }
        while (std::string::npos != (pos=str.find(sp, last))) {
            slice.push_back(str.substr(last, pos-last));
            last = pos+len;
        }

        slice.push_back(str.substr(last));
        return slice.size();
    }

    static std::string alias_parse(const std::string&key, const std::string&alias, const std::string&type, bool *md) {
        std::vector<std::string> type_all(2);

        std::vector<std::string> types;
        split(types, alias, ' ');
        for (size_t i=0; i<types.size(); ++i) {
            std::vector<std::string> type_nameopt;
            split(type_nameopt, types[i], ':');
            if (type_nameopt.size()==2 && type_nameopt[0]==type) {
                type_all[0] = type_nameopt[1];
                type_all.resize(1);
                break;
            } else if (type_nameopt.size()==1) {
                type_all[1] = types[i];
            }
        }

        for (size_t i=0; i<type_all.size(); ++i) {
            if (type_all[i].empty()) {
                continue;
            }

            std::vector<std::string> name_opt;
            split(name_opt, type_all[i], ',');
            if (0 != md) {
                for (size_t i=1; i<name_opt.size(); ++i) {
                    if (name_opt[i]=="m") {
                        *md = true;
                        break;
                    }
                }
            }

            return name_opt[0];
        }

        return key;
    }
private:
    // 字符串到整型
    template <typename T>
    static T tonum_dummy(const std::string&str, Dummy<T> dmy) {
        (void)dmy;
        T t = 0;
        size_t s = str.length();
        if (0 == s) {
            return t;
        }

        size_t i = 0;
        T rad = 10;
        const char *d = str.data();
        if ('-'==d[0] || '+'==d[0]) {
            i = 1;
        } else {
            if ('0' == d[0]) {
                if (s>1 && (d[1]=='x' || d[1]=='X')) {
                    i = 2;
                    rad = 16;
                } else {
                    i = 1;
                    rad = 8;
                }
            }
        }

        std::string wrong("wrong number format");
        std::string overflow("number out of range");

        for (; i<s; ++i) {
            T tmp;
            if (d[i]<='9' && d[i]>='0') {
                tmp = 0+d[i]-'0';
            } else if (d[i]<='F' && d[i]>='A') {
                tmp = 10+d[i]-'A';
            } else if (d[i]<='f' && d[i]>='a') {
                tmp = 10+d[i]-'a';
            } else if (d[i]=='U' || d[i]=='u') {
                if (i+1 == s) {
                    break;
                } else if (i+2==s && (d[i]=='L' || d[i]=='l')) {
                    break;
                } else {
                    throw std::runtime_error(wrong);
                }
            } else if (d[i]=='L' || d[i]=='l') {
                if (i+1 == s) {
                    break;
                } else {
                    throw std::runtime_error(wrong);
                }
            }

            if (tmp < rad) {
                tmp = t*rad+tmp;
                if (tmp >= t) {
                    t = tmp;
                } else {
                    throw std::runtime_error(overflow);
                }
            } else {
                throw std::runtime_error(wrong);
            }
        }

        if ('-' != d[0]) {
            return t;
        } else {
            return -t;
        }
    }
    static float tonum_dummy(const std::string&str, Dummy<float> dmy) {
        (void)dmy;
        float t;
        sscanf(str.c_str(), "%f", &t);
        return t;
    }

    static double tonum_dummy(const std::string&str, Dummy<double> dmy) {
        (void)dmy;
        double t;
        sscanf(str.c_str(), "%lf", &t);
        return t;
    }
    
};

}

#endif
