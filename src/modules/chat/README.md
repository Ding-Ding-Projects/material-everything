# Chat / Messaging Module

Local-first chat foundation for Material Everything.

- `chat_module.hpp/.cpp` owns conversations, messages, delivery state and a pluggable `ChatBackend`.
- `chat_ui.hpp/.cpp` provides Material Design 3 view data for sent/received bubbles, navigation-rail destinations, typing indicator and emoji picker.
- Backends are intentionally independent of the UI model. IRC and Matrix can be added by implementing `me::chat::ChatBackend`.

Attachment handling is represented as names in this foundation slice and remains pluggable for future transfer implementations.
