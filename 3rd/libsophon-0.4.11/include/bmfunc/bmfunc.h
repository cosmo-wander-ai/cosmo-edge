#ifndef BMFUNC_H_
#define BMFUNC_H_

#include "bmrt_arch_info.h"
#include "bmfunc/bmdnn_func.h"

namespace bmruntime {

class bmfunc {
public:
  explicit bmfunc(const string &arch_name);
  virtual ~bmfunc();

  static bmdnn_func *bmdnn_base() { return sta_bmfunc_ptr->bmdnn_fn; }
  static bmdnn_func_1682 *bmdnn_1682() {
    return (bmdnn_func_1682 *)(sta_bmfunc_ptr->bmdnn_fn);
  }
  static bmdnn_func_1684 *bmdnn_1684() {
    return (bmdnn_func_1684 *)(sta_bmfunc_ptr->bmdnn_fn);
  }
  static bmdnn_func_1880 *bmdnn_1880() {
    return (bmdnn_func_1880 *)(sta_bmfunc_ptr->bmdnn_fn);
  }
  static bmdnn_func_1684x *bmdnn_1684x() {
    return (bmdnn_func_1684x *)(sta_bmfunc_ptr->bmdnn_fn);
  }
  static bmdnn_func_1688 *bmdnn_1688() {
    return (bmdnn_func_1688 *)(sta_bmfunc_ptr->bmdnn_fn);
  }
  static bmdnn_func_mars3 *bmdnn_mars3() {
    return (bmdnn_func_mars3 *)(sta_bmfunc_ptr->bmdnn_fn);
  }

  static bmdnn_func_2260 *bmdnn_2260() {
    return (bmdnn_func_2260 *)(sta_bmfunc_ptr->bmdnn_fn);
  }

  static bmdnn_func_2380 *bmdnn_2380() {
    return (bmdnn_func_2380 *)(sta_bmfunc_ptr->bmdnn_fn);
  }

  bmrt_arch_info *get_arch_info_ptr() { return p_bmtpu_arch; }

private:
  static bmfunc *sta_bmfunc_ptr; /* why not this ? */
  bmrt_arch_info *p_bmtpu_arch;

  bmdnn_func *bmdnn_fn;
};
}

#endif
