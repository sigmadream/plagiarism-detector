#include <vector>
namespace ns {
class Stack : public Base {
public:
    Stack() : data_(), size_{0} { init(); }
    explicit Stack(int n) : size_(n) {}
    virtual ~Stack() = default;
    void push(int v) override;
    int top() const noexcept { return data_[size_ - 1]; }
    auto size() const -> int { return size_; }
private:
    void init() { size_ = 0; }
    std::vector<int> data_;
    int size_;
};
void Stack::push(int v) { data_.push_back(v); ++size_; }
}
template <typename T> T maxv(T a, T b) { return a > b ? a : b; }
int helper(int);
int helper(int n) { return n <= 1 ? 1 : n * helper(n - 1); }
int even(int n); int odd(int n) { return n == 0 ? 0 : even(n - 1); }
int even(int n) { return n == 0 ? 1 : odd(n - 1); }
int main() {
    ns::Stack s; s.push(3);
    int x = maxv<int>(1, 2) + helper(5) + even(4);
    x <<= 2; x >>= 1; x |= 0x1F; x ^= 07; x %= 3;
    double d = .5e-3 + 1.0E+2f;
    for (int i = 0; i < 10; i++) { if (i % 2 == 0) continue; else break; }
    while (x-- > 0) { do { x--; } while (x > 5); }
    switch (x) { case 1: helper(x); break; default: return -1; }
    auto lam = [&](int y) { return y + x; };
    try { throw 1; } catch (...) { }
    int* p = new int[3]; delete[] p; p = nullptr;
    return sizeof(int) + (x ? true : false) + lam(1) + d;
}
