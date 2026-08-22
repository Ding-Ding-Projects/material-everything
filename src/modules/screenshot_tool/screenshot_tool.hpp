#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace me::screenshot_tool {

enum class CaptureMode {
    FullScreen,
    Window,
    Region,
    Freeform,
};

enum class AnnotationKind {
    Arrow,
    Text,
    Rectangle,
    Ellipse,
    BlurRegion,
};

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Size {
    double width = 0.0;
    double height = 0.0;
};

struct Rect {
    Point origin;
    Size size;
};

struct Annotation {
    AnnotationKind kind = AnnotationKind::Rectangle;
    Rect bounds;
    // Freeform annotations store their points in image coordinates.
    std::vector<Point> points;
    std::string text;
    std::string strokeColor = "#6750A4";  // Material Design 3 primary.
    std::string fillColor = "#00000000";
    double strokeWidth = 3.0;
    double blurStrength = 12.0;
    bool filled = false;
};

struct Image {
    Size size;
    std::uint32_t bitsPerPixel = 32;
    std::vector<std::uint8_t> pixels;  // RGBA8888, row-major.
};

struct CaptureRequest {
    CaptureMode mode = CaptureMode::FullScreen;
    std::optional<Rect> region;
    std::optional<std::uintptr_t> targetWindowHandle;
    std::chrono::milliseconds delay{0};
    bool includeCursor = false;
};

struct CaptureRecord {
    std::string id;
    std::chrono::system_clock::time_point capturedAt;
    CaptureMode mode = CaptureMode::FullScreen;
    Rect sourceBounds;
    std::shared_ptr<const Image> image;
    std::vector<Annotation> annotations;
};

enum class ExportFormat {
    Png,
    Jpeg,
    Bmp,
};

struct ToolbarItem {
    std::string id;
    std::string label;
    std::string accessibleName;
    AnnotationKind kind = AnnotationKind::Rectangle;
    std::string containerColor = "#EADDFF";  // MD3 primary-container.
    std::string iconColor = "#21005D";       // MD3 on-primary-container.
    bool selected = false;
};

// Self-contained screenshot engine. Platform capture is implemented natively on
// Microsoft Windows; all geometry, annotation, export and history behavior is
// portable C++17 so the module remains testable without launching the app.
class ScreenshotTool {
public:
    ScreenshotTool();

    // Captures synchronously after honoring request.delay.
    CaptureRecord capture(const CaptureRequest& request);

    void addAnnotation(const std::string& recordId, const Annotation& annotation);
    bool removeAnnotation(const std::string& recordId, std::size_t index);
    const std::vector<Annotation>& annotations(const std::string& recordId) const;

    // Applies annotations to a copy of the source image. Blur regions are
    // represented by a deterministic box blur; other shapes are stroked or
    // filled using an integer rasterizer suitable for preview rendering.
    Image render(const CaptureRecord& record) const;

    bool save(const CaptureRecord& record, const std::string& filePath,
              ExportFormat format = ExportFormat::Png);
    bool copyToClipboard(const CaptureRecord& record) const;

    const std::vector<CaptureRecord>& history() const;
    std::optional<CaptureRecord> find(const std::string& recordId) const;
    bool removeFromHistory(const std::string& recordId);
    void clearHistory();
    std::size_t maxHistorySize() const;
    void setMaxHistorySize(std::size_t value);

    static std::vector<ToolbarItem> materialToolbar();

private:
    std::vector<CaptureRecord> history_;
    std::size_t maxHistorySize_ = 100;
    std::uint64_t nextCaptureNumber_ = 1;
};

}  // namespace me::screenshot_tool
