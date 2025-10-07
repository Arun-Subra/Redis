# Redis-like In-Memory Store

A lightweight, Redis-like in-memory key-value store implementation in C, supporting both string operations and sorted set operations using AVL trees.

## Features

### Basic Key-Value Operations
- **SET** - Store string values
- **GET** - Retrieve string values  
- **DEL** - Delete keys

### Sorted Set Operations
- **ZADD** - Add members to sorted sets with scores
- **ZRANGE** - Get range of members from sorted sets
- **ZREM** - Remove members from sorted sets
- **ZSCORE** - Get score of a member
- **ZCARD** - Get number of members in a sorted set

## Architecture

### Server (`server.c`)
- Single-threaded, event-driven architecture using `poll()`
- Non-blocking I/O for handling multiple concurrent clients
- Custom protocol with length-prefixed messages
- In-memory hash map for key storage
- **AVL tree implementation for sorted sets** (balanced binary search trees)

### Client (`client.c`)
- Simple command-line interface
- Binary protocol implementation
- Support for both string and multi-bulk responses

## Building

The project uses a Makefile for compilation:

```bash
# Build both server and client
make

# Build only the server
make server

# Build only the client
make client

# Clean build artifacts
make clean
