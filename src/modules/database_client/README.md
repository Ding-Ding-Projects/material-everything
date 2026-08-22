# Database Client module

SQL database client for Material Everything. SQLite is implemented natively;
MySQL and PostgreSQL are represented by the backend abstraction and report a
clear `driver unavailable` state until their native adapters are linked.

## API

`DatabaseClientModule`, declared in `database_client.hpp`, provides:

- connection manager: add/remove/connect/disconnect SQLite connections
- schema browser: tables, views, indexes, columns, primary keys and SQL text
- query execution with column names, rows, elapsed time and errors
- CSV (RFC 4180-style) and JSON export
- bounded in-memory query history

`SqlHighlighter` tokenizes keywords, strings, numbers, comments, operators and
identifiers for the editor's syntax highlighting.

## Build

The module is a static C++20 library (`me_database_client`) and links the system
SQLite development library. No tests or captures are included in this lane.
