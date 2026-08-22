#pragma once

#include <QString>
#include <QSize>
#include <QWidget>
#include <memory>
#include <vector>

namespace material_everything::presentation {

struct SlideElement {
    enum class Kind { TextBox, Image, Rectangle, Ellipse, Line };

    Kind kind = Kind::TextBox;
    QRectF bounds;
    QString text;
    QString imagePath;      // Images only
    QColor fillColor{0xFF, 0xFF, 0xFF};
    int fontSize = 24;
};

enum class Transition { None, Fade, SlideLeft, SlideUp };

class Slide {
public:
    QString title() const;
    void setTitle(const QString& title);

    std::vector<SlideElement>& elements();
    const std::vector<SlideElement>& elements() const;
    Transition transition() const;
    void setTransition(Transition t);

private:
    QString m_title;
    std::vector<SlideElement> m_elements;
    Transition m_transition = Transition::None;
};

class PresentationDocument {
public:
    std::vector<Slide>& slides();
    const std::vector<Slide>& slides() const;
    void addSlide();
    void insertSlide(int index);
    void removeSlide(int index);
    bool save(const QString& path) const;   // JSON
    static std::optional<PresentationDocument> load(const QString& path);

private:
    std::vector<Slide> m_slides{Slide{}};
};

// Main module widget: thumbnails sidebar + M3 canvas + toolbar.
class PresentationModule : public QWidget {
    Q_OBJECT
public:
    explicit PresentationModule(QWidget* parent = nullptr);
    ~PresentationModule() override;

    PresentationDocument& document();

signals:
    void currentSlideChanged(int index);
    void documentModified();

public slots:
    void addSlide();
    void deleteCurrentSlide();
    void setCurrentSlide(int index);
    void setPresenterMode(bool on);
    bool exportToPdf(const QString& path);
    bool exportImages(const QString& directory); // slide-001.png ...

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace material_everything::presentation
