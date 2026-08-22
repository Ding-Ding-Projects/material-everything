#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace me::diagram {

enum class ShapeKind { Rect, Ellipse, Diamond, RoundedRect };
struct Shape {
    int id{};
    ShapeKind kind{ShapeKind::Rect};
    double x{}, y{}, w{120}, h{80};
    std::string label;
    unsigned fill{0xFF6750A4};   // ARGB M3 primary
};

struct Connection {
    int id{};
    int fromShape{-1};
    int toShape{-1};
    bool arrowAtEnd{true};
};

class DiagramModule {
public:
    static constexpr double kGrid = 16.0;

    int addShape(ShapeKind kind, double x, double y, std::string label = {});
    void removeShape(int shapeId);
    void moveShape(int shapeId, double x, double y);
    void setLabel(int shapeId, std::string label);
    const Shape* shape(int id) const;
    const std::vector<Shape>& shapes() const { return shapes_; }
    const std::vector<Connection>& connections() const { return connections_; }

    int connect(int fromShape, int toShape, bool arrowAtEnd = true);
    void disconnect(int connectionId);
    Connection* connection(int id);

    // Viewport state for zoom/pan.
    double zoom{1.0}, panX{0.0}, panY{0.0};
    void zoomBy(double factor) {
        if (factor > 0.05 && factor < 20.0) zoom *= factor;
        zoom = std::clamp(zoom, 0.05, 20.0);
    }
    void panBy(double dx, double dy) { panX += dx; panY += dy; }

    std::string exportSvg() const;
    std::string exportPngPlaceholder() const; // PNG bytes via minimal encoder below

private:
    std::vector<Shape> shapes_;
    std::vector<Connection> connections_;
    int nextShapeId_{1};
    int nextConnId_{1};
};

} // namespace me::diagram
