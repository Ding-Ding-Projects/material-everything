#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace material_everything { namespace vector_graphics {
struct Vec2 { double x=0,y=0; Vec2()=default; Vec2(double px,double py):x(px),y(py){}
  Vec2 operator+(const Vec2&o)const{return{x+o.x,y+o.y};}
  Vec2 operator-(const Vec2&o)const{return{x-o.x,y-o.y};}
  Vec2 operator*(double s)const{return{x*s,y*s};} };
struct Rect { double x=0,y=0,width=0,height=0;
  double right()const{return x+width;}double bottom()const{return y+height;}
  bool contains(const Vec2&p)const{return p.x>=x&&p.x<=right()&&p.y>=y&&p.y<=bottom();}
  Rect united(const Rect&o)const{double nx=x<o.x?x:o.x,ny=y<o.y?y:o.y,nr=right()>o.right()?right():o.right(),nb=bottom()>o.bottom()?bottom():o.bottom();return{nx,ny,nr-nx,nb-ny};}};
struct Color{uint8_t r=0,g=0,b=0,a=255;static Color fromHex(const std::string&h);std::string toHex()const;
 bool operator==(const Color&)const=default;};
enum class NodeType{Anchor};
enum class ToolMode{Select,PenBezier,Rectangle,Ellipse,Polygon,NodeEdit};
enum class ShapeKind{FreePath,Rectangle,Ellipse,Polygon};
struct PathNode{NodeType type=NodeType::Anchor;Vec2 pos;Vec2 handleIn;Vec2 handleOut;bool smooth=false;};
struct FillStroke{Color fill{255,255,255,255};Color stroke{0,0,0,255};double strokeWidth=1.0;bool fillEnabled=true;bool strokeEnabled=true;};
struct Shape{ShapeKind kind=ShapeKind::FreePath;std::vector<PathNode>nodes;FillStroke style;Rect bounds()const;bool hitTest(const Vec2&p)const;};
struct Layer{int id=0;std::string name;bool visible=true;bool locked=false;double opacity=1.0;std::vector<Shape>shapes;};
enum class AlignAction{Left,HCenter,Right,Top,VCenter,Bottom,DistributeH,DistributeV};
class Document{
public:
 Document();Layer&activeLayer();void setActiveLayer(int);Layer*findLayer(int);
 const std::vector<Layer>&layers()const{return layers_;}
 int addLayer(const std::string&);bool removeLayer(int);void moveLayerUp(int);void moveLayerDown(int);
 bool toggleLayerVisible(int);bool toggleLayerLocked(int);
 int addShape(Shape);bool removeShape(int,int);Shape*shapeAt(int,int);Rect documentBounds()const;void alignShapes(AlignAction);
private:std::vector<Layer>layers_;int activeLayerId_=1;};
bool importSvg(Document&,const std::string&);
std::string exportSvg(const Document&);
struct RasterResult{int width=0,height=0;std::vector<uint8_t>pixels;};
RasterResult rasterizePng(const Document&,int,int);
std::string encodePng(const RasterResult&);
}} // namespace
