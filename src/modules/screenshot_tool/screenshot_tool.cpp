#include "screenshot_tool.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace me::screenshot_tool {
namespace {

constexpr std::size_t kChannels = 4;

std::string nowId() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    std::ostringstream stream;
    stream << "capture-" << std::put_time(&parts, "%Y%m%d-%H%M%S");
    return stream.str();
}

Rect normalizeRect(Rect input) {
    if (input.size.width < 0.0) {
        input.origin.x += input.size.width;
        input.size.width = -input.size.width;
    }
    if (input.size.height < 0.0) {
        input.origin.y += input.size.height;
        input.size.height = -input.size.height;
    }
    return input;
}

bool containsPoint(const Rect& rect, const Point& point) {
    return point.x >= rect.origin.x && point.y >= rect.origin.y &&
           point.x <= rect.origin.x + rect.size.width &&
           point.y <= rect.origin.y + rect.size.height;
}

void putPixel(Image& image, int x, int y, std::uint8_t r, std::uint8_t g,
              std::uint8_t b, std::uint8_t a = 255) {
    if (x < 0 || y < 0 || x >= static_cast<int>(image.size.width) ||
        y >= static_cast<int>(image.size.height)) {
        return;
    }
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.size.width) +
         static_cast<std::size_t>(x)) * kChannels;
    image.pixels[offset] = r;
    image.pixels[offset + 1] = g;
    image.pixels[offset + 2] = b;
    image.pixels[offset + 3] = a;
}

void drawLine(Image& image, Point from, Point to, std::uint8_t r,
              std::uint8_t g, std::uint8_t b) {
    int x0 = static_cast<int>(std::lround(from.x));
    int y0 = static_cast<int>(std::lround(from.y));
    const int x1 = static_cast<int>(std::lround(to.x));
    const int y1 = static_cast<int>(std::lround(to.y));
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int stepX = x0 < x1 ? 1 : -1;
    const int stepY = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        putPixel(image, x0, y0, r, g, b);
        putPixel(image, x0, y0 + 1, r, g, b);  // Minimum visible stroke weight at 100% scale.
        if (x0 == x1 && y0 == y1) break;
        const int doubledError = error * 2;
        if (doubledError >= dy) {
            error += dy;
            x0 += stepX;
        }
        if (doubledError <= dx) {
            error += dx;
            y0 += stepY;
        }
    }
}

void drawRectangleOutline(Image& image, const Rect& bounds, std::uint8_t r,
                          std::uint8_t g, std::uint8_t b) {
    const int left = static_cast<int>(bounds.origin.x);
    const int top = static_cast<int>(bounds.origin.y);
    const int right = static_cast<int>(bounds.origin.x + bounds.size.width);
    const int bottom = static_cast<int>(bounds.origin.y + bounds.size.height);
    drawLine(image, {left, top}, {right, top}, r, g, b);
    drawLine(image, {right, top}, {right, bottom}, r, g, b);
    drawLine(image, {right, bottom}, {left, bottom}, r, g, b);
    drawLine(image, {left, bottom}, {left, top}, r, g, b);
}

void fillRectangle(Image& image, const Rect& bounds, std::uint8_t alpha) {
    const int left = std::max(0, static_cast<int>(bounds.origin.x));
    const int top = std::max(0, static_cast<int>(bounds.origin.y));
    const int right =
        std::min(static_cast<int>(image.size.width),
                 static_cast<int>(bounds.origin.x + bounds.size.width));
    const int bottom =
        std::min(static_cast<int>(image.size.height),
                 static_cast<int>(bounds.origin.y + bounds.size.height));
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(image.size.width) +
                 static_cast<std::size_t>(x)) * kChannels;
            image.pixels[offset + 3] = alpha;
        }
    }
}

void drawEllipseOutline(Image& image, const Rect& bounds, std::uint8_t r,
                        std::uint8_t g, std::uint8_t b) {
    const double centerX = bounds.origin.x + bounds.size.width / 2.0;
    const double centerY = bounds.origin.y + bounds.size.height / 2.0;
    const double radiusX = bounds.size.width / 2.0;
    const double radiusY = bounds.size.height / 2.0;
    if (radiusX <= 0.0 || radiusY <= 0.0) return;
    const int steps = static_cast<int>(
        std::ceil(std::max(radiusX, radiusY) * 8.0 + 16.0));
    Point previous{centerX + radiusX, centerY};
    for (int step = 1; step <= steps; ++step) {
        const double angle = (step * 2.0 * 3.14159265358979323846) /
                             static_cast<double>(steps);
        const Point next{
            centerX + std::cos(angle) * radiusX,
            centerY + std::sin(angle) * radiusY,
        };
        drawLine(image, previous, next, r, g, b);
        previous = next;
    }
}

void boxBlurRegion(Image& image, const Rect& bounds, int strength) {
    const int left = std::max(0, static_cast<int>(bounds.origin.x));
    const int top = std::max(0, static_cast<int>(bounds.origin.y));
    const int right =
        std::min(static_cast<int>(image.size.width),
                 static_cast<int>(bounds.origin.x + bounds.size.width));
    const int bottom =
        std::min(static_cast<int>(image.size.height),
                 static_cast<int>(bounds.origin.y + bounds.size.height));
    const int radius = std::clamp(strength, 1, 64);
    std::vector<std::uint8_t> source = image.pixels;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            unsigned sums[kChannels]{};
            unsigned count = 0;
            for (int sampleY = std::max(top, y - radius);
                 sampleY <= std::min(bottom - 1, y + radius); ++sampleY) {
                for (int sampleX = std::max(left, x - radius);
                     sampleX <= std::min(right - 1, x + radius); ++sampleX) {
                    const std::size_t offset =
                        (static_cast<std::size_t>(sampleY) *
                             static_cast<std::size_t>(image.size.width) +
                         static_cast<std::size_t>(sampleX)) * kChannels;
                    for (std::size_t channel = 0; channel < kChannels; ++channel) {
                        sums[channel] += source[offset + channel];
                    }
                    ++count;
                }
            }
            const std::size_t destinationOffset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(image.size.width) +
                 static_cast<std::size_t>(x)) * kChannels;
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                image.pixels[destinationOffset + channel] =
                    static_cast<std::uint8_t>(sums[channel] / count);
            }
        }
    }
}

struct Rgb {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

Rgb parseColor(const std::string& color) {
    if (color.size() != 7 || color.front() != '#') {
        throw std::invalid_argument("color must be #RRGGBB");
    }
    auto hexDigit = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        throw std::invalid_argument("invalid hex digit");
    };
    return {
        static_cast<std::uint8_t>((hexDigit(color[1]) << 4) | hexDigit(color[2])),
        static_cast<std::uint8_t>((hexDigit(color[3]) << 4) | hexDigit(color[4])),
        static_cast<std::uint8_t>((hexDigit(color[5]) << 4) | hexDigit(color[6])),
    };
}

void renderAnnotation(Image& image, const Annotation& annotation) {
    const Rgb color = parseColor(annotation.strokeColor);
    switch (annotation.kind) {
        case AnnotationKind::Arrow: {
            const Point from = annotation.bounds.origin;
            const Point to{annotation.bounds.origin.x + annotation.bounds.size.width,
                           annotation.bounds.origin.y + annotation.bounds.size.height};
            drawLine(image, from, to, color.red, color.green, color.blue);
            const double angle = std::atan2(to.y - from.y, to.x - from.x);
            const double headLength = std::max(10.0, annotation.strokeWidth * 4.0);
            for (const double spread : {2.5, -2.5}) {
                drawLine(
                    image, to,
                    {to.x - std::cos(angle + spread) * headLength,
                     to.y - std::sin(angle + spread) * headLength},
                    color.red, color.green, color.blue);
            }
            break;
        }
        case AnnotationKind::Text: {
            // The raster renderer draws a bounded placeholder block; the UI
            // layer owns font shaping. Geometry remains exact so exports are
            // positioned correctly even before text is composited by a font
            // backend.
            drawRectangleOutline(image, annotation.bounds, color.red, color.green,
                                 color.blue);
            break;
        }
        case AnnotationKind::Rectangle: {
            if (annotation.filled) fillRectangle(image, annotation.bounds, 160);
            drawRectangleOutline(image, annotation.bounds, color.red, color.green,
                                 color.blue);
            break;
        }
        case AnnotationKind::Ellipse: {
            drawEllipseOutline(image, annotation.bounds, color.red, color.green,
                               color.blue);
            break;
        }
        case AnnotationKind::BlurRegion: {
            boxBlurRegion(image, annotation.bounds,
                          static_cast<int>(annotation.blurStrength));
            break;
        }
    }
}

std::vector<std::uint8_t> encodeBmp(const Image& image) {
    const std::uint32_t width = static_cast<std::uint32_t>(image.size.width);
    const std::uint32_t height = static_cast<std::uint32_t>(image.size.height);
    const std::uint32_t padding =
        (4U - ((width * 3U) % 4U)) % 4U;
    const std::uint32_t rowBytes = width * 3U + padding;
    const std::uint32_t pixelBytes = rowBytes * height;
    const std::uint32_t fileBytes = 54U + pixelBytes;

    auto append16 = [&](std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    };
    auto append32 = [&](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    };
    std::vector<std::uint8_t> bytes;
    bytes.reserve(fileBytes);
    bytes.insert(bytes.end(), {'B', 'M'});
    append32(fileBytes);
    append32(0);
    append32(54);
    append32(40);
    append32(width);
    append32(height);
    append16(1);
    append16(24);
    append32(0);
    append32(pixelBytes);
    append32(2835);
    append32(2835);
    append32(0);
    append32(0);

    for (std::uint32_t y = height; y > 0; --y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t sourceOffset =
                ((static_cast<std::size_t>(y - 1) *
                      static_cast<std::size_t>(width)) +
                 x) * kChannels;
            bytes.push_back(image.pixels[sourceOffset + 2]);
            bytes.push_back(image.pixels[sourceOffset + 1]);
            bytes.push_back(image.pixels[sourceOffset]);
        }
        bytes.insert(bytes.end(), padding, 0);
    }
    return bytes;
}

std::vector<std::uint8_t> encodeImage(const Image& image, ExportFormat format) {
    switch (format) {
        case ExportFormat::Bmp:
            return encodeBmp(image);
        case ExportFormat::Jpeg:
            // JPEG requires an encoder dependency outside this lane. Callers
            // receive a valid BMP-compatible fallback only through Bmp; Jpeg
            // reports false instead of silently changing the requested format.
            throw std::runtime_error("JPEG encoding requires the shared media encoder");
        case ExportFormat::Png:
            throw std::runtime_error("PNG encoding requires the shared media encoder");
    }
    throw std::invalid_argument("unknown export format");
}

#ifdef _WIN32

class WindowsCaptureSession {
public:
    explicit WindowsCaptureSession(const CaptureRequest& request)
        : desktop_(GetDC(nullptr)), windowContext_() {}

    ~WindowsCaptureSession() {
        if (desktop_) ReleaseDC(nullptr, desktop_);
    }

    CaptureRecord capture(const CaptureRequest& request) {
        RECT bounds{};
        HWND window = nullptr;
        if (request.mode == CaptureMode::Window) {
            window = reinterpret_cast<HWND>(request.targetWindowHandle.value_or(0));
            if (!window || !IsWindow(window) || !GetWindowRect(window, &bounds)) {
                throw std::runtime_error("target window is unavailable");
            }
        } else if (!GetVirtualScreenRect(bounds)) {
            throw std::runtime_error("virtual screen is unavailable");
        }

        Rect source{{static_cast<double>(bounds.left),
                     static_cast<double>(bounds.top)},
                    {static_cast<double>(bounds.right - bounds.left),
                     static_cast<double>(bounds.bottom - bounds.top)}};
        if (request.mode == CaptureMode::Region ||
            request.mode == CaptureMode::Freeform) {
            if (!request.region) throw std::invalid_argument("region is required");
            const Rect requested = normalizeRect(*request.region);
            source.origin = requested.origin;
            source.size = requested.size;
        }
        const int width = static_cast<int>(source.size.width);
        const int height = static_cast<int>(source.size.height);
        if (width <= 0 || height > std::numeric_limits<int>::max() / 4) {
            throw std::out_of_range("capture dimensions are invalid");
        }

        HDC memoryContext = CreateCompatibleDC(desktop_);
        HBITMAP bitmap = CreateCompatibleBitmap(desktop_, width, height);
        HGDIOBJ previous = SelectObject(memoryContext, bitmap);
        BitBlt(memoryContext, 0, 0, width, height, desktop_,
               static_cast<int>(source.origin.x), static_cast<int>(source.origin.y),
               SRCCOPY | (request.includeCursor ? CAPTUREBLT : 0));

        Image result;
        result.size = source.size;
        result.pixels.resize(static_cast<std::size_t>(width) *
                             static_cast<std::size_t>(height) * kChannels);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;  // Top-down rows.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        if (!GetDIBits(memoryContext, bitmap, 0, height, result.pixels.data(), &info,
                       DIB_RGB_COLORS)) {
            SelectObject(memoryContext, previous);
            DeleteObject(bitmap);
            DeleteDC(memoryContext);
            throw std::runtime_error("pixel retrieval failed");
        }
        // GDI returns BGRA; normalize it to RGBA8888.
        for (std::size_t offset = 0; offset < result.pixels.size(); offset += 4) {
            std::swap(result.pixels[offset], result.pixels[offset + 2]);
            result.pixels[offset + 3] = 255;
        }

        SelectObject(memoryContext, previous);
        DeleteObject(bitmap);
        DeleteDC(memoryContext);

        CaptureRecord record;
        record.id = nowId();
        record.capturedAt = std::chrono::system_clock::now();
        record.mode = request.mode;
        record.sourceBounds = source;
        record.image = std::make_shared<Image>(std::move(result));
        return record;
    }

    static bool copyToClipboard(const CaptureRecord& record) {
        if (!OpenClipboard(nullptr)) return false;
        const bool opened = true;
        EmptyClipboard();
        const std::size_t byteCount =
            record.image->pixels.size() + sizeof(BITMAPV5HEADER);
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, byteCount);
        bool copied = false;
        if (global) {
            void* destination = GlobalLock(global);
            if (destination) {
                BITMAPV5HEADER header{};
                header.bV5Size = sizeof(header);
                header.bV5Width = static_cast<LONG>(record.image->size.width);
                header.bV5Height = -static_cast<LONG>(record.image->size.height);
                header.bV5Planes = 1;
                header.bV5BitCount = 32;
                header.bV5Compression = BI_RGB;
                header.bV5RedMask = 0x00ff0000;
                header.bV5GreenMask = 0x0000ff00;
                header.bV5BlueMask = 0x000000ff;
                header.bV5AlphaMask = 0xff000000;
                std::memcpy(destination, &header, sizeof(header));
                std::memcpy(static_cast<std::uint8_t*>(destination) + sizeof(header),
                            record.image->pixels.data(),
                            record.image->pixels.size());
                GlobalUnlock(global);
                copied = SetClipboardData(CF_DIBV5, global) != nullptr;
            }
            if (!copied) GlobalFree(global);
        }
        CloseClipboard();
        return opened && copied;
    }

private:
    HDC desktop_;
};

#endif  // _WIN32

}  // namespace

ScreenshotTool::ScreenshotTool() = default;

CaptureRecord ScreenshotTool::capture(const CaptureRequest& request) {
    if (request.delay.count() > 0) {
        std::this_thread::sleep_for(request.delay);
    }
    if (request.mode == CaptureMode::FullScreen && !request.region) {
        // Full-screen uses the native virtual-screen bounds.
    } else if (request.mode == CaptureMode::Window &&
               !request.targetWindowHandle.has_value()) {
        throw std::invalid_argument("window handle is required");
    }

    CaptureRecord record;
    if (request.mode != CaptureMode::Freeform) {
        record.sourceBounds = normalizeRect(request.region.value_or(
            Rect{{0.0, 0.0}, {0.0, 0.0}}));
    } else if (!request.region) {
        throw std::invalid_argument("freeform bounding region is required");
    }

#ifdef _WIN32
    record = WindowsCaptureSession(request).capture(request);
#else
    // Portable non-Windows builds expose the same contract but require a host
    // capture provider; they do not fabricate pixels.
    throw std::runtime_error("platform capture provider is not installed");
#endif
    history_.push_back(record);
    if (history_.size() > maxHistorySize_) {
        history_.erase(history_.begin());
    }
    return record;
}

void ScreenshotTool::addAnnotation(const std::string& recordId,
                                   const Annotation& annotation) {
    auto found = std::find_if(history_.begin(), history_.end(),
                              [&](const CaptureRecord& item) {
                                  return item.id == recordId;
                              });
    if (found == history_.end()) throw std::invalid_argument("unknown capture");
    found->annotations.push_back(annotation);
}

bool ScreenshotTool::removeAnnotation(const std::string& recordId,
                                      std::size_t index) {
    auto found = std::find_if(history_.begin(), history_.end(),
                              [&](const CaptureRecord& item) {
                                  return item.id == recordId;
                              });
    if (found == history_.end() || index >= found->annotations.size()) {
        return false;
    }
    found->annotations.erase(found->annotations.begin() +
                             static_cast<std::ptrdiff_t>(index));
    return true;
}

const std::vector<Annotation>& ScreenshotTool::annotations(
    const std::string& recordId) const {
    auto found = std::find_if(history_.cbegin(), history_.cend(),
                              [&](const CaptureRecord& item) {
                                  return item.id == recordId;
                              });
    if (found == history_.cend()) throw std::invalid_argument("unknown capture");
    return found->annotations;
}

Image ScreenshotTool::render(const CaptureRecord& record) const {
    Image output = *record.image;
    for (const Annotation& annotation : record.annotations) {
        renderAnnotation(output, annotation);
    }
    return output;
}

bool ScreenshotTool::save(const CaptureRecord& record, const std::string& filePath,
                          ExportFormat format) {
    try {
        std::vector<std::uint8_t> encoded =
            encodeImage(render(record), format);
        std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        return static_cast<bool>(output);
    } catch (...) {
        return false;
    }
}

bool ScreenshotTool::copyToClipboard(const CaptureRecord& record) const {
#ifdef _WIN32
    return WindowsCaptureSession::copyToClipboard(record);
#else
    return false;
#endif
}

const std::vector<CaptureRecord>& ScreenshotTool::history() const {
    return history_;
}

std::optional<CaptureRecord> ScreenshotTool::find(const std::string& recordId) const {
    auto found = std::find_if(history_.cbegin(), history_.cend(),
                              [&](const CaptureRecord& item) {
                                  return item.id == recordId;
                              });
    if (found == history_.cend()) return std::nullopt;
    return *found;
}

bool ScreenshotTool::removeFromHistory(const std::string& recordId) {
    auto found = std::find_if(history_.begin(), history_.end(),
                              [&](const CaptureRecord& item) {
                                  return item.id == recordId;
                              });
    if (found == history_.end()) return false;
    history_.erase(found);
    return true;
}

void ScreenshotTool::clearHistory() {
    history_.clear();
}

std::size_t ScreenshotTool::maxHistorySize() const {
    return maxHistorySize_;
}

void ScreenshotTool::setMaxHistorySize(std::size_t value) {
    maxHistorySize_ = std::max<std::size_t>(value, 1);
    while (history_.size() > maxHistorySize_) history_.erase(history_.begin());
}

std::vector<ToolbarItem> ScreenshotTool::materialToolbar() {
    return {
        {"arrow", "Arrow", "Add arrow annotation", AnnotationKind::Arrow,
         "#EADDFF", "#21005D", false},
        {"text", "Text", "Add text annotation", AnnotationKind::Text,
         "#EADDFF", "#21005D", false},
        {"rectangle", "Rectangle", "Add rectangle annotation",
         AnnotationKind::Rectangle, "#EADDFF", "#21005D", true},
        {"ellipse", "Ellipse", "Add ellipse annotation", AnnotationKind::Ellipse,
         "#EADDFF", "#21005D", false},
        {"blur", "Blur", "Blur private information", AnnotationKind::BlurRegion,
         "#E8DEF8", "#1D192B", false},
        {"undo", "Undo", "Remove last annotation", AnnotationKind::Rectangle,
         "#F7F2FA", "#49454F", false},
        {"save", "Save", "Save capture to file", AnnotationKind::Rectangle,
         "#6750A4", "#FFFFFF", false},
        {"clipboard", "Clipboard", "Copy capture to clipboard",
         AnnotationKind::Rectangle, "#4F378B", "#FFFFFF", false},
    };
}
