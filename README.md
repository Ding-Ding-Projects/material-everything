<p align="center"><img src="assets/logo.svg" alt="Material Everything logo" width="96"></p>

<h1 align="center">Material Everything</h1>
<p align="center">One native C++ desktop suite. 27 modules. Pure Material Design 3.</p>

Material Everything combines the workflows people love from popular open-source
applications — a browser, editors, media tools, developer utilities, a password
manager and more — into a single Qt/C++ application with one consistent,
accessible Material Design 3 interface.

## Modules (27)

api_client · audio_editor · calculator · calendar · chat · clipboard_manager ·
clock_timer · database_client · download_manager · ftp_client · git_client ·
hex_editor · media_player · notes · paint · password_manager · pdf_reader ·
presentation · rss_reader · screenshot_tool · settings · spreadsheet ·
text_editor · torrent_client · video_editor · web_browser · word_processor

## Build (Windows)

```bat
build.bat /s
```

The script installs its own toolchain dependencies into user-scoped locations,
configures CMake/Ninja and builds every module. No administrator rights are
required.

## Documentation site

A static Material Design 3 landing/documentation site lives at `site/`
(pure HTML/CSS/vanilla JS — no CDN, no analytics). Open `site/index.html`
locally or publish it as GitHub Pages.

## License

See [LICENSE](LICENSE).
