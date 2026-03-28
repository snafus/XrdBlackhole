# XRootD HTTP ExtHandler Plugin Guide

This guide covers what exthandler plugins are, how they work, authentication
and authorisation principles, path and verb matching, redirect responses,
how to build one correctly, and the gotchas discovered while building
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
| Proxy / redirect | `GET /static/*` → 302 to nginx |

Without an exthandler, any HTTP `GET /metrics` would fall through to the OFS
layer, which tries to open `/metrics` as a file and returns
*"Opening path '/metrics' is disallowed"*.

---

## How It Works

### Full request pipeline

```
TCP connection
    │
    ▼
TLS handshake  (if HTTPS)
    │
    ▼
HTTP request parsed
    │
    ▼
Security extraction  ← authN happens here
    │  XrdHttpSecXtractor / SciTokens / macaroons / X.509
    │  prot->SecEntity populated
    │  Invalid or missing credentials → 401/403, request ends here
    │
    ▼
MatchesPath() called for each registered exthandler (declaration order)
    │
    ├── match found → ProcessReq()   ← your code runs here
    │                   OFS authdb is NEVER consulted
    │                   You are responsible for authZ
    │
    └── no match   → XrdOfs → XrdOss authdb → file operations
```

### Plugin loading

XRootD loads the plugin via `http.exthandler` **after** `xrd.protocol XrdHttp`
is active. The entry point `XrdHttpGetExtHandler` is resolved by `dlopen`.

---

## Authentication and Authorisation

### What happens, and when

Understanding the distinction between authN and authZ — and exactly where
each occurs relative to your plugin — is essential for writing a secure and
correct exthandler.

**Authentication (authN)** — *who is this client?*

AuthN is handled by the XRootD security framework at the protocol layer,
before any exthandler is consulted. For HTTP this is done by an
`XrdHttpSecXtractor` plugin (e.g. SciTokens, macaroons, or GSI X.509). The
outcome is a populated `XrdSecEntity` on `prot->SecEntity`, which the
`XrdHttpExtReq` constructor copies into the request object.

If the server is configured to require a valid credential (e.g.
`sec.protocol scitokens`) and the client does not present one — or presents
an invalid or expired token — the HTTP layer returns 401/403 and the
request is terminated. **Your exthandler is never called.**

If the server permits anonymous access (or the credential is valid), your
exthandler is called with `req.clientdn`, `req.clienthost`, and
`req.GetSecEntity()` already populated.

**Authorisation (authZ)** — *is this client allowed to do this?*

XRootD's normal authZ layer (`acc.authdb` rules, managed by `XrdAcc`) is
consulted only when the OFS layer opens a file or directory. Because an
exthandler intercepts the request *before* OFS is called, **OFS authZ is
completely bypassed** for any path your exthandler claims.

This is intentional — exthandlers are designed to handle paths that have no
filesystem backing — but it means **you are solely responsible for authZ
within your handler**.

### Implications

| Layer | Handled by | Bypassed by exthandler? |
|---|---|---|
| TLS / HTTPS | XRootD connection layer | No |
| Token / credential validation (authN) | `XrdHttpSecXtractor` | No |
| OFS path access control (authZ) | `acc.authdb` / `XrdAcc` | **Yes** |
| Your handler's own logic | You | N/A |

The practical consequence: if your exthandler matches `/data/*`, any
authenticated user can reach `ProcessReq` for those paths, regardless of
what `acc.authdb` says about `/data`. You must enforce your own access
policy.

### Unauthenticated endpoints (e.g. `/metrics`)

Prometheus scrapers and monitoring tools do not send SciTokens or X.509
credentials. If the server requires authentication for all connections, the
scraper will receive a 401 before reaching the exthandler.

Approaches:

**Option 1 — separate internal port without authN:**

```
# External port: full auth required
xrd.protocol XrdHttp:1094 libXrdHttp.so

# Internal monitoring port: no auth, bind to loopback only
xrd.protocol XrdHttp:1095 libXrdHttp.so
# (restrict access via firewall / network policy)
```

**Option 2 — configure the security extractor to allow anonymous access
to specific paths.** This is extractor-specific; consult the SciTokens or
GSI documentation for the `authz.mapping` or equivalent directive.

**Option 3 — check inside `ProcessReq` and return 401 if the credential
is absent, rather than relying on the protocol layer to enforce it.**

### Implementing authZ in your handler

When your exthandler handles paths that require access control, use
`req.GetSecEntity()` to inspect the authenticated identity:

```cpp
int MyHandler::ProcessReq(XrdHttpExtReq &req) {
  const XrdSecEntity &sec = req.GetSecEntity();

  // Check for a populated identity.
  if (!sec.name || sec.name[0] == '\0') {
    return req.SendSimpleResp(401, "Unauthorized",
      "WWW-Authenticate: Bearer\r\n", nullptr, 0);
  }

  // Example: restrict to a specific VO group.
  if (!sec.vorg || strcmp(sec.vorg, "atlas") != 0) {
    return req.SendSimpleResp(403, "Forbidden", nullptr, nullptr, 0);
  }

  // Check a specific role/capability from the token.
  // sec.role is populated by the SciTokens extractor from the token's
  // "scope" claim (e.g. "storage.read:/").
  if (!sec.role || strstr(sec.role, "storage.write") == nullptr) {
    return req.SendSimpleResp(403, "Forbidden", nullptr, nullptr, 0);
  }

  // Authenticated and authorised — proceed.
  // ...
}
```

**`XrdSecEntity` fields populated by SciTokens / X.509:**

| Field | Source | Typical content |
|---|---|---|
| `name` | Token `sub` / DN common name | `"jsmith"` / `"/DC=org/CN=..."` |
| `host` | Client IP / reverse DNS | `"client.example.com"` |
| `vorg` | Token `wlcg.groups[0]` / VO | `"atlas"` |
| `role` | Token `scope` | `"storage.read:/ storage.write:/upload"` |
| `grps` | Token `wlcg.groups` | `"/atlas /atlas/production"` |
| `moninfo` | Full DN string | Used for logging |
| `tident` | Trace identifier | For log correlation |

### Design principle: deny by default

Structure your authZ checks so that any unexpected state results in a
refusal, not a grant:

```cpp
// Good: explicitly enumerate what is allowed; deny everything else.
bool isAuthorised(const XrdSecEntity &sec) {
  if (!sec.name || sec.name[0] == '\0') return false;
  if (!sec.vorg)                         return false;
  if (strcmp(sec.vorg, "cms") != 0 &&
      strcmp(sec.vorg, "atlas") != 0)    return false;
  return true;
}

// Bad: check for known-bad states; grant everything else.
bool isAuthorised(const XrdSecEntity &sec) {
  if (sec.name == nullptr) return false;
  return true;   // passes for empty string, unknown VOs, etc.
}
```

---

## Matching Verbs and Paths

### `MatchesPath` contract

`MatchesPath(verb, path)` is called **for every HTTP request** that arrives
at the server, for each registered exthandler, until one returns `true`. It
runs on the connection thread — the same thread that is reading from the
socket. It must be:

- **Thread-safe** — called concurrently from multiple connection threads.
- **Non-blocking** — no I/O, no locks, no heap allocation.
- **Fast** — see performance implications below.

The `path` argument is the URL path component only, without the query string.
Query parameters are available via `req.headers["xrd-http-query"]` inside
`ProcessReq`.

### Matching verbs

Any HTTP method string can be matched. Common ones:

| Verb | Typical use |
|---|---|
| `GET` | Read, metrics, redirects |
| `PUT` | Upload (bypassing OFS) |
| `POST` | REST API |
| `DELETE` | Custom delete logic |
| `HEAD` | Existence / metadata check |
| `COPY` | Third-party copy (XrdHttpTPC) |
| `PROPFIND` | WebDAV directory listing |

Match multiple verbs explicitly:

```cpp
bool MatchesPath(const char *verb, const char *path) {
  if (strncmp(path, "/api/", 5) != 0) return false;
  return strcmp(verb, "GET")  == 0 ||
         strcmp(verb, "POST") == 0 ||
         strcmp(verb, "HEAD") == 0;
}
```

### Path matching strategies

**Exact match** — fastest; use for fixed endpoints like `/metrics`:

```cpp
return strcmp(path, "/metrics") == 0;
```

**Prefix match** — use `strncmp` for path hierarchies:

```cpp
return strncmp(path, "/api/v1/", 8) == 0;
```

**Suffix match** — useful for file-type routing:

```cpp
size_t n = strlen(path);
return n > 5 && strcmp(path + n - 5, ".json") == 0;
```

**Regex** — possible but expensive; see performance section:

```cpp
#include <regex>
static const std::regex re(R"(^/data/[a-z]+/\d{4}/.*)");
return std::regex_match(path, re);
```

### Performance implications

In a busy XRootD instance handling thousands of concurrent `xrdcp` streams,
`MatchesPath` may be called tens of thousands of times per second. Each call
is on the critical path between socket read and request dispatch.

| Strategy | Cost | Notes |
|---|---|---|
| `strcmp` / `strncmp` | O(n), ~ns | Preferred for fixed strings |
| Manual prefix/suffix | O(n), ~ns | Fine for most cases |
| `std::regex_match` | O(n·m), ~µs | Avoid on hot path; compile once to `static const` at minimum |
| `std::string` construction | heap alloc | Avoid; use `strcmp` on the raw `const char*` |
| Map / set lookup | O(log n) | Acceptable for small sets of fixed paths |
| Any lock acquisition | variable | Never; use atomics or lock-free structures |

**If you need regex**, compile the pattern once into a `static const
std::regex` inside `MatchesPath` (initialised on first call, thread-safe in
C++11) and use `regex_match` rather than `regex_search` to anchor both ends:

```cpp
bool MatchesPath(const char *verb, const char *path) {
  if (strcmp(verb, "GET") != 0) return false;   // fast pre-filter
  static const std::regex re(R"(^/run/\d+/file/\d+$)");
  return std::regex_match(path, re);
}
```

The `strcmp` pre-filter on the verb eliminates non-GET requests before the
regex engine is invoked.

**Ordering exthandlers** — register the most frequently matched (or most
quickly rejected) handler first. If handler A claims `/metrics` (an exact
match) and handler B claims `/data/*` (all file I/O), put A first; it will
reject `/data/` requests in one `strcmp` call.

---

## Sending Redirects

`SendSimpleResp` with a 301 or 302 status and a `Location:` header is all
that is needed. The `Location` value is passed as part of the header string.

### Temporary redirect (302)

```cpp
int MyHandler::ProcessReq(XrdHttpExtReq &req) {
  // Build the target URL.  req.resource is the path; headers contain
  // the original Host so you can construct an absolute URL if needed.
  std::string target = "https://cdn.example.com" + req.resource;
  std::string hdr    = "Location: " + target + "\r\n";

  return req.SendSimpleResp(302, "Found", hdr.c_str(), nullptr, 0);
}
```

### Permanent redirect (301)

```cpp
  std::string hdr = "Location: https://new.example.com" + req.resource + "\r\n";
  return req.SendSimpleResp(301, "Moved Permanently", hdr.c_str(), nullptr, 0);
```

### Redirect with query string preservation

The query string is not in `req.resource` — it is in
`req.headers["xrd-http-query"]`. Reconstruct the full URL if needed:

```cpp
  std::string target = "https://backend.example.com" + req.resource;
  const auto &qit = req.headers.find("xrd-http-query");
  if (qit != req.headers.end() && !qit->second.empty())
    target += "?" + qit->second;
  std::string hdr = "Location: " + target + "\r\n";
  return req.SendSimpleResp(302, "Found", hdr.c_str(), nullptr, 0);
```

### Redirect with authZ check

Never redirect to a backend that trusts XRootD's authZ — the backend will
receive an unauthenticated request. Either forward the original credential
in a header, or check authZ before redirecting:

```cpp
int MyHandler::ProcessReq(XrdHttpExtReq &req) {
  const XrdSecEntity &sec = req.GetSecEntity();
  if (!sec.name || sec.name[0] == '\0') {
    return req.SendSimpleResp(401, "Unauthorized",
      "WWW-Authenticate: Bearer\r\n", nullptr, 0);
  }

  // Forward the original Authorization header to the backend.
  std::string loc = "Location: https://nginx.internal" + req.resource + "\r\n";
  const auto &auth = req.headers.find("authorization");
  if (auth != req.headers.end())
    loc += "X-Forwarded-Auth: " + auth->second + "\r\n";

  return req.SendSimpleResp(302, "Found", loc.c_str(), nullptr, 0);
}
```

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

| Member / Method | Purpose |
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
  // Called for every HTTP request — keep this fast and allocation-free.
  return !strcmp(verb, "GET") && !strcmp(path, "/my-endpoint");
}

int MyHandler::ProcessReq(XrdHttpExtReq &req) {
  // Always send a response on every code path.
  const std::string body = "Hello from MyHandler\n";
  return req.SendSimpleResp(
    200, "OK",
    "Content-Type: text/plain\r\n",
    body.c_str(),
    static_cast<long long>(body.size()));
}

int MyHandler::Init(const char *cfgfile) {
  // Parse plugin-specific directives from cfgfile if needed.
  // Return 0 on success, non-zero to abort loading.
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

### 5. `MatchesPath` is on the hot path — keep it allocation-free

`MatchesPath` is called for every HTTP request on the connection thread.
Use `strcmp` / `strncmp` on the raw `const char*` arguments. Never construct
`std::string`, acquire a lock, or call any blocking function here.

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

### 9. OFS authZ is bypassed — you own access control

The `acc.authdb` rules are never evaluated for paths your exthandler claims.
A client that passes authN (valid token) will reach `ProcessReq` regardless
of what the authdb says about that path. Implement explicit authZ checks
using `req.GetSecEntity()` for any handler that is not intentionally public.

### 10. Unauthenticated endpoints require a separate port or policy

Prometheus scrapers, health checks, and similar tooling do not send tokens.
If the server requires authentication globally, these clients will receive
a 401 before your exthandler is invoked. The cleanest solution is a dedicated
internal port bound to loopback, with no `sec.protocol` requirement, used
only for monitoring traffic.

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
- [ ] `MatchesPath` is allocation-free, lock-free, and O(1) or O(n) string compare only
- [ ] `ProcessReq` always sends a response on all code paths
- [ ] AuthZ explicitly checked via `GetSecEntity()` for any non-public endpoint
- [ ] Unauthenticated endpoints use a dedicated port or explicit security policy
- [ ] Add `xrootd-server-devel` to `BuildRequires` in the RPM spec
