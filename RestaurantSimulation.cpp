#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>


#include <winsock2.h>
#include <ws2tcpip.h>


std::mutex print_mutex;

class SafeSocket
{
private:
    SOCKET m_sock;

public:
    explicit SafeSocket(SOCKET s) : m_sock(s) {}
    ~SafeSocket()
    {
        if (m_sock != INVALID_SOCKET)
            closesocket(m_sock);
    }
    SafeSocket(const SafeSocket &) = delete;
    SafeSocket &operator=(const SafeSocket &) = delete;
    SOCKET get() const { return m_sock; }
};

struct Order
{
    int id;
    std::string food_item;
    std::shared_ptr<SafeSocket> client_connection;
    std::chrono::time_point<std::chrono::high_resolution_clock> enqueue_time;
};

class Restaurant
{
private:
    // Queue 1: Ticket Spike (Orders waiting to be cooked)
    std::queue<Order> ticket_spike;
    std::mutex spike_mutex;
    std::condition_variable spike_bell;

    // Queue 2: Pickup Counter (Food waiting to be delivered)
    std::queue<Order> pickup_counter;
    std::mutex counter_mutex;
    std::condition_variable counter_bell;

    bool is_open = true;

    // --- Kitchen Logic ---

    void kitchen_worker(int chef_id)
    {
        while (true)
        {
            Order current_order;
            {
                std::unique_lock<std::mutex> lock(spike_mutex);
                spike_bell.wait(lock, [this]
                                { return !ticket_spike.empty() || !is_open; });

                if (!is_open && ticket_spike.empty())
                    return;

                current_order = ticket_spike.front();
                ticket_spike.pop();
            }
            auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - current_order.enqueue_time).count();
            {
                std::lock_guard<std::mutex> lock(print_mutex);
                std::cout << "[Chef " << chef_id << "] is Cooking Order #" << std::to_string(current_order.id) << " (" << current_order.food_item << ") Which waited " << wait_time << " ms in queue \n";
            }
            auto start = std::chrono::high_resolution_clock::now();
            
            // Simulate cooking time

            std::string receipt = "Processing: " + current_order.food_item;
            for(volatile int i = 0; i < 1000000000; i++) {} // real CPU burn
           
            auto end = std::chrono::high_resolution_clock::now();
            {
                std::lock_guard<std::mutex> lock(print_mutex);
                std::cout << "[Chef " << chef_id << "] Order processed in " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms\n";
            }
            {
                std::unique_lock<std::mutex> lock(counter_mutex);
                pickup_counter.push(current_order);
            }

            counter_bell.notify_one();
        }
    }

    void waiter_worker(int waiter_id)
    {
        while (true)
        {
            Order ready_food;
            {
                std::unique_lock<std::mutex> lock(counter_mutex);
                counter_bell.wait(lock, [this]
                                  { return !pickup_counter.empty() || !is_open; });

                if (!is_open && pickup_counter.empty())
                    return;

                ready_food = pickup_counter.front();
                pickup_counter.pop();
            }
            {
                std::lock_guard<std::mutex> lock(print_mutex);
                std::cout << "[Waiter " << waiter_id << "] Serving #" << std::to_string(ready_food.id) << " (" << ready_food.food_item << ") to the customer! \n";
            }
            std::string html = "<html><body><h2>Order #" + std::to_string(ready_food.id) +
                               " is ready!</h2><p>Enjoy your hot " + ready_food.food_item + "!</p></body></html>";
            std::string response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(html.length()) + "\r\n\r\n" + html;
            send(ready_food.client_connection->get(), response.c_str(), response.length(), 0);
        }
    }
    std::vector<std::thread> staff;

public:
    Restaurant(int num_chefs, int num_waiters)
    {
        for (int i = 0; i < num_chefs; ++i)
        {
            staff.emplace_back(&Restaurant::kitchen_worker, this, i + 1);
        }
        for (int i = 0; i < num_waiters; ++i)
        {
            staff.emplace_back(&Restaurant::waiter_worker, this, i + 1);
        }
    }
    ~Restaurant()
    {
        is_open = false;
        spike_bell.notify_all();
        counter_bell.notify_all();
        for (auto &t : staff)
        {
            t.join();
        }
        {
            std::lock_guard<std::mutex> lock(print_mutex);
            std::cout << "Restaurant is closed. All staff have left.\n";
            
        }
    }

    void submit_order(int id, std::string food, std::shared_ptr<SafeSocket> client)
    {
        {
            std::unique_lock<std::mutex> lock(spike_mutex);
            ticket_spike.push(Order{id, food, client, std::chrono::high_resolution_clock::now()});
        }
        spike_bell.notify_one();
    }
};

int main()
{
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 10);

    Restaurant backend_kitchen(2, 2); // 2 chefs and 2 waiters

    std::atomic<int> global_order_id{100};

    std::cout << "Server running! Open your browser and go to: \n";
    std::cout << "http://localhost:8080/Pizza \n";
    std::cout << "http://localhost:8080/Burger \n";
    std::cout << "http://localhost:8080/Salad \n";
    std::cout << "http://localhost:8080/Pasta \n";

    while (true)
    {
        SOCKET raw_sock = accept(server_fd, nullptr, nullptr);
        if (raw_sock == INVALID_SOCKET)
            continue;

        auto client_sock = std::make_shared<SafeSocket>(raw_sock);

        char buffer[1024] = {0};
        recv(client_sock->get(), buffer, 1024, 0);
        std::string request(buffer);

        size_t get_pos = request.find("GET /");
        if (get_pos != std::string::npos)
        {
            size_t start = get_pos + 5; // Skip "GET /"
            size_t end = request.find(" HTTP", start);
            std::string food_item = request.substr(start, end - start);

            if (!food_item.empty() && food_item != "favicon.ico")
            {
                int order_id = global_order_id.fetch_add(1);
                {
                    std::lock_guard<std::mutex>lock(print_mutex);
                    std::cout << "[Host] Sent ticket for: " << food_item << " to kitchen. Order ID: " << std::to_string(order_id) << "\n";
                }
                backend_kitchen.submit_order(order_id, food_item, client_sock);
            }
        }
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}