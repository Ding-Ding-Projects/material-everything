# FTP/SFTP Client Module

Part of the Material Everything unified desktop application.

## Overview

Self-contained C++17 module providing FTP, FTPS (explicit), SFTP, and SCP client functionality with a dual-pane browsing model.

## Architecture

| File | Purpose |
|------|---------|
| `ftp_client.hpp` | Public API header — all types and the module class |
| `ftp_client.cpp` | Implementation (pimpl; libssh2 for SFTP/SCP, libcurl for FTP/FTPS) |
| `CMakeLists.txt` | Build target with optional backend detection |

## Features implemented in this module

- **Protocols** — FTP, FTPS (explicit TLS), SFTP, SCP (via libssh2/libcurl)
- **Dual-pane browsing** — independent local/remote navigation via the public API
- **Transfer queue** — enqueue, start/pause/resume/cancel/reorder individual items
- **Bookmarked sites** — `SiteBookmark` struct stores connection profiles including key paths
- **Directory sync** — plan-and-execute model supporting mirror-upload, mirror-download, and bidirectional modes with timestamp-based conflict resolution and orphan deletion
- **Permissions editor** — POSIX mode-bit read/write through `PermissionEntry` (SFTP only)
- **Progress callbacks** — byte-accurate speed, ETA, and percentage reporting per queue item

## Build dependencies

| Dependency | Required | Purpose |
|-----------|----------|---------|
| libssh2 | Optional (`ME_WITH_SFTP`) | SFTP + SCP transport |
| libcurl | Optional (`ME_WITH_CURL`) | FTP + FTPS transport |

Both backends are independently optional. If neither is available at build time the module compiles but connection calls return an error string.

## Usage sketch

```cpp
material_everything::FtpClientModule ftp;
ftp.set_progress_callback([](int64_t id, const auto& p) {
    // update UI progress bar
});
ftp.set_log_callback([](const std::string& msg) {
    // append to log pane
});

material_everything::SiteBookmark site;
site.protocol = material_everything::FtpProtocol::Sftp;
site.host = "example.com";
site.username = "deploy";
site.password = "...";

auto result = ftp.connect(site);
if (!result.ok) { /* show error */ }

auto listing = ftp.list_directory("/var/www");
// populate remote pane from listing.value

auto local_listing = FtpClientModule::list_local_directory("C:/Users/me/projects");
// populate local pane

auto qid = ftp.enqueue_transfer(
    material_everything::TransferDirection::Upload,
    "/var/www/index.html",
    "C:/Users/me/projects/index.html",
    false
);
ftp.start_queue();
```

## Thread safety

The transfer queue is mutex-protected. Connection operations are not thread-safe against each other but the queue worker thread runs independently of the calling thread. Progress callbacks fire on the worker thread — marshal to the UI thread as needed.
