# PDF Reader module

Pluggable C++ PDF reader surface for Material Everything. Scope: page
navigation, zoom, fit-to-width, table-of-contents sidebar, text search, and
bookmarks. `RenderBackend` is the adapter seam for MuPDF, Poppler, or Qt PDF;
`QtPdfRenderBackend` is the integration point when the app links QtPdf.

This lane intentionally ships no tests and no captures per its task brief.
