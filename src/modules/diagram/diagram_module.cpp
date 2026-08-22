#include "diagram_module.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace me::diagram {

static constexpr double kGrid = 16.0;

static double snap(double v) { return std::round(v / kGrid) * kGrid; }

int DiagramModule::addShape(ShapeKind kind, double x, double y, std::string label) {
    Shape s;
    s.id = nextShapeId_++;
    s.kind = kind;
    s.x = snap(x);
    s.y = snap(y);
    s.label = std::move(label);
    shapes_.push_back(std::move(s));
    return shapes_.back().id;
}

void DiagramModule::removeShape(int shapeId) {
    shapes_.erase(std::remove_if(shapes_.begin(), shapes_.end(),
        [shapeId](const Shape& s){ return s.id == shapeId; }), shapes_.end());
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(),
        [shapeId](const Connection& c){ return c.fromShape == shapeId || c.toShape == shapeId; }),
        connections_.end());
}

void DiagramModule::moveShape(int shapeId, double x, double y) {
    for (auto& s : shapes_) if (s.id == shapeId) { s.x = snap(x); s.y = snap(y); }
}

void DiagramModule::setLabel(int shapeId, std::string label) {
    for (auto& s : shapes_) if (s.id == shapeId) s.label = std::move(label);
}

const Shape* DiagramModule::shape(int id) const {
    for (const auto& s : shapes_) if (s.id == id) return &s;
    return nullptr;
}

int DiagramModule::connect(int fromShape, int toShape, bool arrowAtEnd) {
    if (fromShape == toShape) return -1;
    Connection c;
    c.id = nextConnId_++;
    c.fromShape = fromShape;
    c.toShape = toShape;
    c.arrowAtEnd = arrowAtEnd;
    connections_.push_back(c);
    return c.id;
}

void DiagramModule::disconnect(int connectionId) {
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(),
        [connectionId](const Connection& c){ return c.id == connectionId; }), connections_.end());
}

Connection* DiagramModule::connection(int id) {
    for (auto& c : connections_) if (c.id == id) return &c;
    return nullptr;
}

std::string DiagramModule::exportSvg() const {
    std::ostringstream out;
    out << "<svg xmlns='http://www.w3.org/2000/svg' width='1600' height='1000'>\n";
    out << "  <rect width='1600' height='1000' fill='#F7F2FA'/>\n";
    for (const auto& s : shapes_) {
        std::string el;
        switch (s.kind) {
            case ShapeKind::Rect:
                el = "<rect x='" + std::to_string(s.x) + "' y='" + std::to_string(s.y)
                   + "' width='" + std::to_string(s.w) + "' height='" + std::to_string(s.h) + "'/>";
                break;
            case ShapeKind::RoundedRect:
                el = "<rect x='" + std::to_string(s.x) + "' y='" + std::to_string(s.y)
                   + "' width='" + std::to_string(s.w) + "' height='" + std::to_string(s.h)
                   + "' rx='16'/>";
                break;
            case ShapeKind::Ellipse:
                el = "<ellipse cx='" + std::to_string(s.x + s.w/2) + "' cy='" + std::to_string(s.y + s.h/2)
                   + "' rx='" + std::to_string(s.w/2) + "' ry='" + std::to_string(s.h/2) + "'/>";
                break;
            case ShapeKind::Diamond: {
                auto cx = s.x + s.w/2, cy = s.y + s.h/2;
                el = "<polygon points='" + std::to_string(cx) + "," + std::to_string(s.y) + " "
                   + std::to_string(s.x + s.w) + "," + std::to_string(cy) + " "
                   + std::to_string(cx) + "," + std::to_string(s.y + s.h) + " "
                   + std::to_string(s.x) + "," + std::to_string(cy) + "'/>";
                break;
            }
        }
        char hex[8];
        std::snprintf(hex, sizeof(hex), "#%06X", s.fill & 0xFFFFFF);
        size_t p = el.find("/>");
        if (p != std::string::npos) el.insert(p, " fill='" + std::string(hex) + "'");
        out << "  <g transform='translate(" << panX << "," << panY << ") scale(" << zoom << ")'>"
            << el << "\n";
        if (!s.label.empty())
            out << "    <text x='" << s.x + s.w/2 << "' y='" << s.y + s.h/2 + 4
                << "' text-anchor='middle' font-size='14' fill='#1D1B20'>" << s.label << "</text>\n";
        out << "  </g>\n";
    }
    for (const auto& c : connections_) {
        auto* a = shape(c.fromShape); auto* b = shape(c.toShape);
        if (!a || !b) continue;
        double ax = a->x + a->w/2, ay = a->y + a->h/2;
        double bx = b->x + b->w/2, by = b->y + b->h/2;
        out << "  <g transform='translate(" << panX << "," << panY << ") scale(" << zoom << ")'>";
        out << "    <line x1='" << ax << "' y1='" << ay << "' x2='" << bx << "' y2='" << by
            << "' stroke='#49454F' stroke-width='2'";
        if (c.arrowAtEnd)
            out << " marker-end='url(#arrow)'";
        out << "/></g>\n";
    }
    out << "</svg>\n";
    return out.str();
}

std::string DiagramModule::exportPngPlaceholder() const {
    // Minimal placeholder: real raster export is wired through the app's renderer.
    return {};
}

} // namespace me::diagram
