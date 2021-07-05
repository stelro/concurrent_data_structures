#include <iostream>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <array>
#include <functional>
#include <mutex>

constexpr size_t max_hazard_pointers = 100;
struct hazard_pointer {
    std::atomic<std::thread::id> id;
    std::atomic<void*> pointer;
};

std::array<hazard_pointer, max_hazard_pointers> hazard_pointers;

class hp_owner final {
private:
    hazard_pointer* hp_ = nullptr;
public:
    hp_owner() {
        for (size_t i = 0; i < hazard_pointers.size(); i++) {
            std::thread::id old_id;
            if (hazard_pointers[i].id.compare_exchange_strong(
                        old_id, std::this_thread::get_id())) {
                hp_ = &hazard_pointers[i];
                break;
            }
        }

        if (!hp_) {
            throw std::runtime_error("No hazrd pointers available");
        }
    }

    hp_owner(const hp_owner&) = delete;
    hp_owner operator =(const hp_owner&) = delete;

    ~hp_owner() {
        hp_->pointer.store(nullptr);
        hp_->id.store(std::thread::id());
    }

    std::atomic<void*>& get_pointer() {
        return hp_->pointer;
    }
};

std::atomic<void*>& get_hazard_pointer_for_current_thread() {
    thread_local static hp_owner hazard;
    return hazard.get_pointer();
}

bool outstanding_hazard_pointers_for(void* p) {
    for (size_t i = 0; i < hazard_pointers.size(); i++) {
        if (hazard_pointers[i].pointer.load() == p) {
            return true;
        }
    }

    return false;
}

template <typename T>
void do_delete(void* p) {
    delete static_cast<T*>(p);
}

struct data_to_reclaim {
    void *data;
    std::function<void(void*)> deleter;
    data_to_reclaim* next;
    
    template <typename T>
    data_to_reclaim(T* p) :
        data(p),
        deleter(&do_delete<T>),
        next(nullptr) { }
    ~data_to_reclaim() {
        deleter(data);
    }
};

std::atomic<data_to_reclaim*> nodes_to_reclaim;

void add_to_reclaim_list(data_to_reclaim* node) {
    node->next = nodes_to_reclaim.load();
    while (!nodes_to_reclaim.compare_exchange_weak(node->next, node));
}

template <typename T>
void reclaim_later(T* data) {
    add_to_reclaim_list(new data_to_reclaim(data));
}

void delete_nodes_with_no_hazards() {
    data_to_reclaim* current = nodes_to_reclaim.exchange(nullptr);
    while (current) {
        data_to_reclaim* const next = current->next;
        if (!outstanding_hazard_pointers_for(current->data)) {
            delete current;
        } else {
            add_to_reclaim_list(current);
        }
        current = next;
    }
}

template <typename T>
class lockfree_stack {
private:
    struct node {
        std::shared_ptr<T> data;
        node* next;
        node(const T& data) : data(std::make_shared<T>(data)), next(nullptr) { }
    };

    std::atomic<node*> head_;
public:
    lockfree_stack() : head_(nullptr) { }
    void push(const T& data);
    std::shared_ptr<T> pop();
    bool empty() const { return head_.load() == nullptr; }
};

template <typename T>
void lockfree_stack<T>::push(const T& data) {
    node* const new_node = new node(data);
    new_node->next = head_.load();
    while (!head_.compare_exchange_weak(new_node->next, new_node));
}

template <typename T>
std::shared_ptr<T> lockfree_stack<T>::pop() {
    
    std::atomic<void*>& hp = get_hazard_pointer_for_current_thread();
    node* old_head = head_.load();
    do {
        node* temp = nullptr;
        do {
            temp = old_head;
            hp.store(old_head);
            old_head = head_.load();
        } while(temp != old_head);

    } while(old_head && 
            !head_.compare_exchange_strong(old_head, old_head->next));

    hp.store(nullptr);
    std::shared_ptr<T> result;

    if (old_head) {
        
        result.swap(old_head->data);
        if (outstanding_hazard_pointers_for(old_head)) {
            reclaim_later(old_head);
        } else {
            delete old_head;
        }

        delete_nodes_with_no_hazards();
    } 

    return result;
}

struct node {
        std::shared_ptr<int> data;
        std::shared_ptr<node> next;
        node(const int& data) : data(std::make_shared<int>(data)), next(nullptr) { }
};

int main() {

    std::shared_ptr<node> head;
    std::cout << std::boolalpha << std::atomic_is_lock_free(&head) << std::endl;


    // lockfree_stack<int> stack;
    // int n = 10;
    //
    // auto t1 = std::thread([&]() {
    //     while (n > 0) {
    //         stack.push(n);
    //         n--;
    //         std::this_thread::sleep_for(std::chrono::milliseconds(100));
    //     }
    // });
    //
    // auto t2 = std::thread([&]() {
    //     int data = 0;
    //     while (data != 1) {
    //         auto m = stack.pop();
    //         if (m) {
    //             data = *m;
    //             printf("poped: %d\n", data);
    //         }
    //         std::this_thread::yield();
    //     }
    // });
    //
    // t1.join();
    // t2.join();

    return 0;
}

