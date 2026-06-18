# Cosmo C++ 编码规范

> 本规范基于 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)，并结合项目实际情况进行了简化适配。
> 适用 C++17 标准。

---

## 1. 文件与目录

### 1.1 文件命名

- 头文件使用 `.h` 后缀，源文件使用 `.cc` 后缀
- 使用 **PascalCase**：`VideoDecoder.h`、`StreamViewer.cc`
- 文件名应与其主要类名一致

### 1.2 头文件保护

- 统一使用 `#pragma once`

### 1.3 Include 顺序

按以下顺序排列，各组之间空一行：

```cpp
// 1. 对应的头文件
#include "media/VideoDecoder.h"

// 2. C/C++ 标准库
#include <chrono>
#include <memory>
#include <string>

// 3. 第三方库
#include "bmcv_api_ext.h"

// 4. 项目内其他模块
#include "mem/DeviceContext.h"
#include "util/MVLogFormat.h"
```

### 1.4 不要在头文件中使用 `using namespace`

```cpp
// ❌ Bad — 污染 include 该头文件的所有翻译单元
using namespace MVAD;

// ✅ Good — 在 .cc 中使用，或使用完整限定名
```

### 1.5 声明与实现分离

头文件（`.h`）只放声明，实现放在对应的源文件（`.cc`）中：

```cpp
// ❌ Bad — 在头文件中内联实现
class DurationStat {
public:
    void BeginSample() {
        auto idx = index_.load(std::memory_order_relaxed);
        samples_[idx].start_time = Clock::now();
    }
};

// ✅ Good — 头文件只声明
class DurationStat {
public:
    void BeginSample();
};

// ✅ Good — 实现放在 .cc 中
void DurationStat::BeginSample() {
    auto idx = index_.load(std::memory_order_relaxed);
    samples_[idx].start_time = Clock::now();
}
```

**例外**：模板类/函数、`= default`/`= delete` 声明、以及单行 trivial getter 可以保留在头文件中。

---

## 2. 命名规则

### 2.1 总览

| 类型 | 风格 | 示例 |
| ------ | ------ | ------ |
| 命名空间 | `snake_case` | `cosmo::media` |
| 类/结构体 | `PascalCase` | `VideoDecoder`, `DurationStat` |
| 函数/方法 | `PascalCase` | `SendPacket()`, `GetFrame()` |
| 类成员变量 | `snake_case_` (带下划线后缀) | `frame_index_`, `codec_type_` |
| 结构体成员 | `snake_case` (无后缀) | `frame_index`, `codec_type` |
| 局部变量 | `snake_case` | `frame_count`, `ret` |
| 常量/constexpr | `k` + `PascalCase` | `kMaxCacheSize`, `kDefaultFps` |
| 枚举值 | `k` + `PascalCase` | `kStreamEnd`, `kEmptyFrame` |
| 宏 (尽量避免) | `UPPER_SNAKE_CASE` | `MLOG_INFO` |
| 模板参数 | `PascalCase` | `DataType`, `QueueType` |

### 2.2 成员变量

根据所属类型是 `class` 还是 `struct` 进行区分：

- **类 (`class`)**：用于封装具有内部状态的对象。成员变量统一添加下划线后缀 `_`，不使用 `m_` 前缀（匈牙利命名法）：

```cpp
// ❌ Bad
bool m_isRunning;
std::string m_name;
int mIndex;

// ✅ Good
bool is_running_;
std::string name_;
int index_;
```

- **结构体 (`struct`)**：仅用于包含被动数据（所有成员均为 `public`）。成员变量使用普通 `snake_case`，**不带尾部下划线**：

```cpp
// ✅ Good
struct Point {
    int x_coordinate;
    int y_coordinate;
};
```

### 2.3 布尔变量和方法

布尔变量使用 `is_`/`has_`/`should_` 等前缀；与之对应的查询方法同理：

```cpp
bool is_opened_;
bool has_key_frame_;

bool IsOpened() const;
bool HasKeyFrame() const;
```

---

## 3. 命名空间

### 3.1 统一使用 `cosmo` 作为顶层命名空间

所有新代码必须放在 `cosmo::` 下，按模块划分子命名空间：

| 模块 | 命名空间 |
| ------ | ---------- |
| 媒体 | `cosmo::media` |
| 流程 | `cosmo::flow` |
| 推理 | `cosmo::infer` |
| 工具 | `cosmo::util` |
| 联动 | `cosmo::linkage` |
| 内存 | `cosmo::mem` |
| 网络 | `cosmo::network` |
| 应用 | `cosmo::app` |
| 数据库 | `cosmo::db` |
| 平台 | `cosmo::platform` |

> 遗留的 `MVAD`、`MVC`、`MVAL` 命名空间已完全清除，新代码禁止使用。

### 3.2 类型别名放在命名空间内

```cpp
// ❌ Bad — 全局命名空间中的 using
using VideoFramePtr = std::shared_ptr<cosmo::media::VideoFrame>;

// ✅ Good — 放在 namespace 内部
namespace cosmo::media {
using VideoFramePtr = std::shared_ptr<VideoFrame>;
}
```

---

## 4. 类设计

### 4.1 构造与析构

- 使用 `= default` / `= delete` 显式声明特殊成员函数
- 不可拷贝的类使用 `= delete` 显式声明
- 析构函数中不要调用虚函数

### 4.2 虚函数

- 基类析构函数必须声明为 `virtual`
- 派生类 override 时使用 `override` 关键字，不重复写 `virtual`

```cpp
// ❌ Bad
virtual bool Open() override;

// ✅ Good
bool Open() override;
```

### 4.3 成员声明顺序

```cpp
public → protected → private
```

每个访问控制块内，按顺序排列：

1. 类型别名 / 嵌套类型
2. 构造函数 / 析构函数
3. 方法
4. 数据成员

---

## 5. 函数

### 5.1 参数传递

| 场景 | 方式 | 示例 |
| ------ | ------ | ------ |
| 只读小类型 (int, bool, ptr) | 值传递 | `int count` |
| 只读大类型 | `const&` | `const std::string& name` |
| 需要获取所有权 | 值传递 + `std::move` | `std::string name` → `name_ = std::move(name)` |
| 输出参数 (尽量避免) | 指针 | `bool Get(Result* out)` |
| 仅使用对象 (不涉权) | `const T&` 或 `T*` | `const VideoFrame& frame` |
| 共享所有权 | 值传递 + `std::move` | `VideoFramePtr frame` → `frame_ = std::move(frame)` |
| 转移所有权 | `unique_ptr<T>` | `std::unique_ptr<Decoder> decoder` |

```cpp
// ❌ Bad — 大型对象值传递导致不必要的拷贝
VideoFramePtr DecodeJpeg(const std::vector<uint8_t> data);

// ✅ Good
VideoFramePtr DecodeJpeg(const std::vector<uint8_t>& data);
```

### 5.2 const 正确性

- 所有不修改对象状态的方法必须声明为 `const`
- getter 方法必须是 `const`

```cpp
// ❌ Bad
size_t GetWidth();
bool IsOpened();

// ✅ Good
size_t GetWidth() const;
bool IsOpened() const;
```

### 5.3 返回值

- 不要在 `void` 函数末尾写 `return;`
- 优先返回值而非输出参数

---

## 6. 内存与资源管理

### 6.1 禁止裸 new/delete 和 malloc/free

优先使用 RAII 和智能指针：

```cpp
// ❌ Bad
BMVidFrame* frame = (BMVidFrame*)malloc(sizeof(BMVidFrame));
// ... 容易忘记 free

// ✅ Good
auto frame = std::make_unique<BMVidFrame>();
```

如需与 C API 交互，封装自定义 deleter：

```cpp
auto handle = std::unique_ptr<BMVidFrame, decltype(&free)>(
    static_cast<BMVidFrame*>(malloc(sizeof(BMVidFrame))), free);
```

### 6.2 禁止 C 风格强制类型转换

```cpp
// ❌ Bad
frame = (BMVidFrame*)malloc(sizeof(BMVidFrame));
aclRegCommand((char*)"setll", ...);

// ✅ Good
frame = static_cast<BMVidFrame*>(malloc(sizeof(BMVidFrame)));
// 或更好：使用 const_cast（仅在确实需要去 const 时）
```

---

## 7. 线程安全

### 7.1 共享状态使用原子类型或加锁

跨线程读写的 flag 必须使用 `std::atomic`：

```cpp
// ❌ Bad — 在多线程中读写普通 bool
bool stop_ = true;

// ✅ Good
std::atomic<bool> stop_{true};
```

### 7.2 锁的使用

- 优先使用 `std::lock_guard` / `std::scoped_lock`
- 使用 `std::unique_lock` 仅在需要条件变量或手动 unlock 时
- 持锁时间尽量短，不要在锁内执行 IO 或耗时操作

---

## 8. 常量

### 8.1 使用 constexpr 代替 #define

```cpp
// ❌ Bad
#define IMAGE_DEFAULT_SAVE_QUALITY 95
#define DURATIONSTAT_MAXSIZE (3000)
#define H264_NAL_SPS 7

// ✅ Good
constexpr int kImageDefaultSaveQuality = 95;
constexpr int kDurationStatMaxSize     = 3000;
constexpr int kH264NalSps              = 7;
```

### 8.2 魔数

不允许在代码中直接使用未命名的数字常量：

```cpp
// ❌ Bad
dec_params.streamBufferSize = 0x500000;
std::this_thread::sleep_for(std::chrono::milliseconds(100));

// ✅ Good
constexpr size_t kStreamBufferSize = 0x500000;  // 5MB
constexpr auto kCloseWaitDuration  = std::chrono::milliseconds(100);
```

---

## 9. 注释

### 9.1 语言

所有注释统一使用 **英文**。

### 9.2 文件头

使用简洁的文件头，不使用 `@Author`/`@Date` 等信息（交给 git 管理）：

```cpp
// Brief description of what this file/class does.
// More details if needed.
```

### 9.3 注释风格

- 行尾注释用 `//`，与代码之间空两格
- 多行注释优先使用多个 `//` 而非 `/* */`
- 不要提交被注释掉的代码，用 git 历史管理旧代码
- TODO 格式：`// TODO(author): description`

```cpp
// ❌ Bad — 注释掉的代码
// if (m_isNetwork) {
//     m_fmtCtx->use_wallclock_as_timestamps = 1;
// }

// ❌ Bad — 中英混用
// 循环播放的  需要是点播文件
// find stream...

// ✅ Good
// Skip repeated open for local file loop playback.
```

---

## 10. 错误处理

### 10.1 日志级别

| Level | 场景 |
| ------- | ------ |
| `MLOG_DEBUG` | 开发调试信息，生产环境不输出 |
| `MLOG_INFO` | 正常运行节点（启动、关闭、配置变更） |
| `MLOG_WARN` | 可恢复的异常情况 |
| `MLOG_ERRO` | 不可恢复的错误 |

### 10.2 返回值 vs 异常

- 性能敏感的热路径使用返回值（`bool`、`ErrorEnum`、`std::optional`）
- 构造阶段的不可恢复错误可使用异常
- 捕获异常时使用 `const std::exception&`

---

## 11. 现代 C++ 实践

| 规则 | 说明 |
| ------ | ------ |
| 使用 `auto` | 类型明显时使用 `auto`，但不要滥用导致可读性下降 |
| 使用 `nullptr` | 禁止使用 `NULL` 或 `0` 表示空指针 |
| 使用范围 for | `for (const auto& item : container)` |
| 使用 `enum class` | 禁止使用无作用域的 `enum` |
| 初始化 | 优先使用花括号初始化 `int count{0};` |
| 使用 `[[nodiscard]]` | 在返回值必须被检查的函数上标注 |
