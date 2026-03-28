# XRootD HTTP ExtHandler Plugin Guide

This guide covers what exthandler plugins are, how they work, how to build
one correctly, and the gotchas discovered while building
`libXrdBlackholeMetrics`.

---

## What is an ExtHandler?

`XrdHttpExtHandler` is an XRootD plugin interface that lets you intercept
HTTP requests **before** they reach the OFS filesystem layer. The HTTP
protocol handler (`libXrdHttp-5.so`) calls each registered exthandler in
declaration order; the first one whose `MatchesPath()` returns `true` gets
to handle the request exclusively.

Typical uses:

| Use case | Example |
|---|---|
| Monitoring / metrics | `GET /metrics` → Prometheus text |
| Third-party copy | `COPY` with `Source:` header → XrdHttpTPC |
| Custom REST APIs | `GET /api/status` → JSON response |
| WebDAV extensions | Custom `PROPFIND` handling |

Without an exthandler, any HTTP `GET /metrics` would fall through to the OFS
layer, which tries to open `/metrics` as a file and returns
*"Opening path '/metrics' is disallowed"*.

---

## How It Works

### Request dispatch

```
HTTP client
    │
    ▼
XrdHttpProtocol (libXrdHttp-5.so, loaded as xrd.protocol)
    │
    ├── for each registered exthandler (declaration order):
    │       if handler.MatchesPath(verb, path):
    │           handler.ProcessReq(req)   ← your code runs here
    │           return                    ← OFS never sees the request
    │
    └── fall-through → XrdOfs → XrdOss   ← normal file operations
```

### Plugin loading

XRootD loads the plugin via `http.exthandler` **after** `xrd.protocol XrdHttp`
is active. The entry point `XrdHttpGetExtHandler` is resolved by `dlopen`.

---

## The Interface

From `XrdHttp/XrdHttpExtHandler.hh`:

```cpp
class XrdHttpExtHandler {
public:
  /// Return true if this handler owns the given verb+path combination.
  virtual bool MatchesPath(const char *verb, const char *path) = 0;

  /// Handle the request. Send a response via req.SendSimpleResp() (or the
  /// chunked / streaming variants). Return 0 on success, non-zero on error.
  virtual int ProcessReq(XrdHttpExtReq &req) = 0;

  /// Called once at startup with the path of the xrootd config file.
  /// Return 0 on success, non-zero to abort loading.
  virtual int Init(const char *cfgfile) = 0;

  virtual ~XrdHttpExtHandler() {}
};
```

### `XrdHttpExtReq` — the request/response object

| Method | Purpose |
|---|---|
| `verb` | HTTP method string (`"GET"`, `"POST"`, …) |
| `resource` | Path component of the URL (`"/metrics"`) |
| `headers` | `map<string,string>` of all request headers |
| `length` | Content-Length of the request body |
| `clientdn` / `clienthost` | Client identity from TLS/auth |
| `BuffgetData(blen, &data, wait)` | Read up to `blen` bytes of request body |
| `SendSimpleResp(code, desc, headers, body, bodylen)` | Send a complete response |
| `StartSimpleResp(…)` + `SendData(…)` | Stream a response in chunks |
| `StartChunkedResp(…)` + `ChunkResp(…)` | HTTP chunked transfer encoding |
| `GetSecEntity()` | Full `XrdSecEntity` (roles, groups, DN) |

All `Send*` and `Chunk*` methods are **defined in `libXrdHttpUtils.so.2`**,
not in `libXrdHttp-5.so`. See [Build requirements](#build-requirements) below.

---

## Complete Plugin Template

### Header — `MyHandler.hh`

```cpp
#pragma once

#include "XrdHttp/XrdHttpExtHandler.hh"
#include "XrdSys/XrdSysError.hh"

class MyHandler : public XrdHttpExtHandler {
public:
  explicit MyHandler(XrdSysError *log);
  ~MyHandler() override = default;

  bool MatchesPath(const char *verb, const char *path) override;
  int  ProcessReq(XrdHttpExtReq &req) override;
  int  Init(const char *cfgfile) override;

private:
  XrdSysError *m_log;
};
```

### Implementation — `MyHandler.cc`

```cpp
#include "MyHandler.hh"
#include "XrdVersion.hh"

// Required: declares ABI version used at compile time.
// Second argument is a 1–15 character plugin identifier.
XrdVERSIONINFO(XrdHttpGetExtHandler, MyHandler);

MyHandler::MyHandler(XrdSysError *log) : m_log(log) {
  m_log->Say("myhandler: initialised");
}

bool MyHandler::MatchesPath(const char *verb, const char *path) {
  // Called for every HTTP request — keep this fast.
  return !strcmp(verb, "GET") && !strcmp(path, "/my-endpoint");
}

int MyHandler::ProcessReq(XrdHttpExtReq &req) {
  const std::string body = "Hello from MyHandler\n";
  return req.SendSimpleResp(
    200, "OK",
    "Content-Type: text/plain\r\n",
    body.c_str(),
    static_cast<long long>(body.size()));
}

int MyHandler::Init(const char *cfgfile) {
  // Parse plugin-specific directives from cfgfile if needed.
  // Return 0 on success, non-zero to abort.
  return 0;
}

// Plugin entry point — must have exactly this name.
extern "C" XrdHttpExtHandler *
XrdHttpGetExtHandler(XrdSysError  *eDest,
                     const char   * /*confg*/,
                     const char   * /*parms*/,
                     XrdOucEnv    * /*myEnv*/)
{
  return new MyHandler(eDest);
}
```

---

## Build Requirements

### CMake

```cmake
# 1. Find the XrdHttp private headers (XrdHttpExtHandler.hh lives there).
find_path(XRDHTTP_INCLUDE_DIR
          NAMES XrdHttp/XrdHttpExtHandler.hh
          HINTS ${XROOTD_INCLUDE_DIR}
                ${XROOTD_INCLUDE_DIR}/private
                /usr/include/xrootd
                /usr/local/include/xrootd)

# 2. Find libXrdHttpUtils — the SHARED library that contains SendSimpleResp
#    and all XrdHttpExt* implementations. Do NOT use libXrdHttp (the module).
find_library(XRDHTTP_UTILS_LIBRARY
             NAMES XrdHttpUtils
             HINTS /usr/lib64 /usr/local/lib64)

if(XRDHTTP_INCLUDE_DIR AND XRDHTTP_UTILS_LIBRARY)

  set(PLUGIN_VERSION 5)   # match your XRootD major version
  add_library(MyHandler-${PLUGIN_VERSION} MODULE
    MyHandler.cc MyHandler.hh)

  target_include_directories(MyHandler-${PLUGIN_VERSION}
    PRIVATE ${XRDHTTP_INCLUDE_DIR})

  target_link_libraries(MyHandler-${PLUGIN_VERSION}
    ${XROOTD_LIBRARIES}
    ${XRDHTTP_UTILS_LIBRARY})

  # Prevent CMake from propagating link dependencies to consumers.
  set_target_properties(MyHandler-${PLUGIN_VERSION} PROPERTIES
    INTERFACE_LINK_LIBRARIES ""
    LINK_INTERFACE_LIBRARIES "")

  install(TARGETS MyHandler-${PLUGIN_VERSION}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

endif()
```

### RPM build dependency

The `XrdHttpExtHandler.hh` header ships in the **private** headers of
`xrootd-server-devel`. Ensure it is listed in your spec's `BuildRequires`:

```spec
BuildRequires: xrootd-server-devel
```

---

## Configuration

```
# Load the XrdHttp protocol on port 1094.
xrd.protocol XrdHttp:1094 libXrdHttp.so

# Register the exthandler.  XRootD auto-appends the version suffix (-5),
# so do NOT include it in the path.
http.exthandler myhandler /usr/lib64/libMyHandler.so
```

Multiple exthandlers can be registered; they are tested in declaration order.

---

## Gotchas

### 1. Link against `XrdHttpUtils`, not `XrdHttp`

`libXrdHttp-5.so` is a dlopen MODULE — it is the HTTP protocol handler
itself and is not designed to be linked against. All `XrdHttpExt*` class
implementations (`SendSimpleResp`, `SendData`, `ChunkResp`, etc.) live in
`libXrdHttpUtils.so.2` (a proper SHARED library). The XRootD source tree
states this explicitly:

> *"XrdHttpUtils is marked as a shared library as XrdHttp plugins are
> expected to link against it for the XrdHttpExt class implementations."*

`XrdHttpTPC` — the canonical reference exthandler in the XRootD tree — links
against `XrdHttpUtils`. Follow that pattern.

### 2. Omit the `-5` version suffix from `http.exthandler`

```
# Wrong — causes double-versioning: libMyHandler-5-5.so not found
http.exthandler myhandler /usr/lib64/libMyHandler-5.so

# Correct — XRootD appends -5 automatically
http.exthandler myhandler /usr/lib64/libMyHandler.so
```

This applies to all plugin directives (`ofs.osslib`, `ofs.xattrlib`,
`http.exthandler`).

### 3. Symbols are not shared between plugins (`RTLD_LOCAL`)

XRootD loads plugins with `RTLD_LOCAL`, so a global variable or function
defined in one plugin `.so` is invisible to another plugin loaded later. If
your exthandler needs to access state owned by the OSS plugin (e.g. a global
`g_statsManager`), it must have an explicit `DT_NEEDED` on that library.

Use `-L`/`-l` in `target_link_options`, **not** `$<TARGET_FILE:...>`:

```cmake
# Correct: produces DT_NEEDED: libXrdBlackhole-5.so (bare name)
add_dependencies(MyHandler-5 XrdBlackhole-5)
target_link_options(MyHandler-5 PRIVATE
  -L$<TARGET_FILE_DIR:XrdBlackhole-5>
  -lXrdBlackhole-5)

# Wrong: embeds absolute build path in DT_NEEDED
# → dlopen fails with "No such file or directory" on installed systems
target_link_options(MyHandler-5 PRIVATE
  $<TARGET_FILE:XrdBlackhole-5>)
```

### 4. `XrdVERSIONINFO` is required

Without the version macro the plugin may load silently but XRootD will
emit a warning. Include it at file scope in the `.cc` file:

```cpp
#include "XrdVersion.hh"
XrdVERSIONINFO(XrdHttpGetExtHandler, MyPluginName);
```

The second argument must be 1–15 characters. It appears in the XRootD log
at load time.

### 5. `MatchesPath` is called for every request — keep it O(1)

`MatchesPath` is called on the hot path for every HTTP request, including
high-frequency `xrdcp` and WebDAV operations. Use `strcmp` / prefix checks,
not regex or map lookups.

### 6. `Init` is called before `MatchesPath` or `ProcessReq`

If `Init` returns non-zero, the plugin is unloaded and XRootD logs an error.
Use `Init` to validate configuration and pre-allocate resources. Do not defer
initialisation to `MatchesPath`.

### 7. `ProcessReq` must always send a response

If `ProcessReq` returns without calling a `Send*` method, the client
connection will hang. Always send a response — even on error paths:

```cpp
int MyHandler::ProcessReq(XrdHttpExtReq &req) {
  if (something_wrong) {
    return req.SendSimpleResp(500, "Internal Server Error",
                              nullptr, nullptr, 0);
  }
  // ...
}
```

### 8. Private headers may not be installed by default

`XrdHttpExtHandler.hh` is in the `private/` subdirectory of the XRootD
include path. On some distributions or build configurations it may be absent
even when `xrootd-server-devel` is installed. Always guard the build with a
`find_path` check and skip the plugin gracefully if the header is missing.

---

## Checklist for a new exthandler plugin

- [ ] Inherit from `XrdHttpExtHandler`; implement `MatchesPath`, `ProcessReq`, `Init`
- [ ] Export `XrdHttpGetExtHandler` as `extern "C"`
- [ ] Add `XrdVERSIONINFO(XrdHttpGetExtHandler, <name>)` at file scope
- [ ] Link against `XrdHttpUtils` (not `XrdHttp`)
- [ ] Use `find_path` + `find_library` guards; skip build gracefully if absent
- [ ] Set `INTERFACE_LINK_LIBRARIES ""` on the MODULE target
- [ ] Config directive omits `-5` suffix
- [ ] If sharing state with OSS plugin: use `-L/-l` DT_NEEDED, not `$<TARGET_FILE:...>`
- [ ] `ProcessReq` always sends a response on all code paths
- [ ] Add `xrootd-server-devel` to `BuildRequires` in the RPM spec
