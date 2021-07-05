#include <iostream>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdio>

template <typename T>
class lock_free_stack {
private:
    struct node {
        std::shared_ptr<T> data;
        std::shared_ptr<node> next;
        node(const T& data) : data(std::make_shared<T>(data)) { }
    };
    std::shared_ptr<node> head;
public:
    void push(const T& data);
    std::shared_ptr<T> pop();
    lock_free_stack() {
        std::cout << "Is shard_ptr lock free? " << std::boolalpha << 
            std::atomic_is_lock_free(&head) << std::endl;
    }
    ~lock_free_stack() { while (pop()); }
};

template <typename T>
void lock_free_stack<T>::push(const T& data) {
    auto new_node = std::make_shared<node>(data);
    new_node->next = std::atomic_load(&head);
    while (!std::atomic_compare_exchange_weak(&head, 
                &new_node->next, new_node));
}

template <typename T>
std::shared_ptr<T> lock_free_stack<T>::pop() {
    std::shared_ptr<node> old_head = std::atomic_load(&head);
    while (old_head && !std::atomic_compare_exchange_weak(&head,
                &old_head, std::atomic_load(&old_head->next)));
    if (old_head) {
        std::atomic_store(&old_head->next, std::shared_ptr<node>());
        return old_head->data;
    }
    return nullptr;
}

int main() {

    lock_free_stack<int> stack;

    auto t1 = std::thread([&]() {
        int n = 10;
        while (n > 0) {
            stack.push(n);           
            std::cout << "pusing: " << n << '\n';
            n--;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    auto t2 = std::thread([&]() {
        int data = 0;
        while (data != 2) {
            auto p = stack.pop();
            if (p) {
                data = *p;
                printf("  poping: %d\n", *p);
            }
            std::this_thread::yield();
        }
    });

    t1.join();
    t2.join();

    return 0;
}
