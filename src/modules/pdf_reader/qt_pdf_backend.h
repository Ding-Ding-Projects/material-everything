#pragma once

#include "pdf_reader.h"

namespace material_everything::pdf {

// Adapter enabled when the host app links QtPdf. Concrete methods are supplied
// by the host integration translation unit so this module has no Qt dependency.
class QtPdfRenderBackend final : public RenderBackend {};

}  // namespace material_everything::pdf
