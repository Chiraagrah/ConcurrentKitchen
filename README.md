# Restaurant Server — Concurrent C++ HTTP Server

A multithreaded HTTP server modeled as a restaurant kitchen using 
a producer-consumer architecture with two concurrent queues.

## Architecture
- Host thread accepts HTTP connections and submits orders to the ticket spike queue
- Chef threads (pool) pick up orders, process them (CPU-bound), push to pickup counter
- Waiter threads serve completed orders back to clients via HTTP response

## Key Concepts Demonstrated
- Producer-consumer pattern with two synchronized queues
- std::mutex + std::condition_variable with spurious wakeup prevention
- RAII socket management (SafeSocket)
- std::atomic for thread-safe order ID generation
- Queue latency measurement under concurrent load
- Global print mutex preventing output interleaving across all threads

## Performance Observation
Queue wait time scales linearly with request volume. With 2 chef threads 
processing ~1.8s per order, orders 3+ begin queuing:

[screenshot here]

## Build
Requires Windows (Winsock2). Compile with:
g++ RestaurantSimulation.cpp -o RestaurantServer -lws2_32
