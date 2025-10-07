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
```

## Usage

### Starting the Server
```bash
./server
```
Server listens on 127.0.0.1:1234 by default.

### Using the Client
```bash
# String operations
./client set mykey "Hello World"
./client get mykey
./client del mykey

# Sorted set operations
./client zadd scores 100 "Alice"
./client zadd scores 200 "Bob" 
./client zrange scores 0 -1
./client zscore scores "Alice"
./client zcard scores
./client zrem scores "Bob"
```

## Protocol Specification

### Request Format
```
[4-byte total length][4-byte arg count][(4-byte str len + string)...]
```

### Response Format
```
[4-byte total length][4-byte status][4-byte data length][data...]
```

### Status Codes
- `0` - RES_OK: Success
- `1` - RES_ERR: Error  
- `2` - RES_NX: Key not found

## Data Structures

### Command Storage
- Uses a hash map for O(1) key lookups
- Supports both string values and ZSet structures

### Sorted Sets
- Implemented using AVL trees for O(log n) operations
- Self-balancing binary search trees guarantee O(log n) time complexity
- Supports range queries, score lookups, and member deletion
- In-order traversal for range operations

## Performance Characteristics

- **Memory**: All data stored in RAM
- **Concurrency**: Single-threaded event loop
- **Connections**: Supports up to 1024 concurrent clients
- **Message Size**: Maximum 32MB per message
- **Arguments**: Maximum 200,000 arguments per command
- **Sorted Set Operations**: O(log n) for insert, delete, and search
- **Range Queries**: O(log n + k) where k is the number of elements in range

### AVL Tree Advantages
The use of AVL trees for sorted sets provides several benefits:
- Guaranteed O(log n) performance for all operations
- Automatic balancing maintains optimal tree height
- Efficient range queries via in-order traversal
- Predictable performance without worst-case degradation
- Memory efficient compared to skip lists

## Limitations

- No persistence (in-memory only)
- No replication
- No authentication
- Single-threaded (no CPU parallelism)
- No data expiration/TTL

## Dependencies

- Standard C library
- POSIX-compliant system (Linux, macOS, BSD)
- Make build system

## Error Handling

- Comprehensive error checking throughout
- Graceful connection handling
- Memory allocation failure protection
- Protocol validation

## Example Session

```bash
$ ./client set greeting "Hello, Redis!"
server says: [0]

$ ./client get greeting  
server says: [0] Hello, Redis!

$ ./client zadd leaders 95 "CTO"
server says: [0] OK

$ ./client zadd leaders 85 "CEO"
server says: [0] OK

$ ./client zrange leaders 0 -1
server says: [0] CEO CTO

$ ./client zscore leaders "CEO"
server says: [0] 85

$ ./client zcard leaders
server says: [0] 2
```

## Technical Details

### AVL Tree Implementation
The sorted sets use AVL (Adelson-Velsky and Landis) trees, which are self-balancing binary search trees. Each node maintains:
- Member key and score
- Height balance factor
- Left and right child pointers
- Subtree count for efficient range operations

### Memory Management
- Dynamic buffer allocation for client connections
- Automatic buffer growth for large messages
- Proper cleanup of all allocated resources

### Network Protocol
- Efficient binary protocol minimizes overhead
- Length-prefixed framing prevents parsing errors
- Non-blocking I/O ensures high concurrency

This project demonstrates building a production-style networked service in C with efficient data structures (AVL trees) and non-blocking I/O.
