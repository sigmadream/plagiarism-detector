int f0(int x) { return f1(x) + f1(x); }
int f1(int x) { return f2(x) + f2(x); }
int f2(int x) { return f3(x) + f3(x); }
int f3(int x) { return f4(x) + f4(x); }
int f4(int x) { return f5(x) + f5(x); }
int f5(int x) { return f6(x) + f6(x); }
int f6(int x) { return f7(x) + f7(x); }
int f7(int x) { return f8(x) + f8(x); }
int f8(int x) { return f9(x) + f9(x); }
int f9(int x) { return f10(x) + f10(x); }
int f10(int x) { return f11(x) + f11(x); }
int f11(int x) { return f12(x) + f12(x); }
int f12(int x) { return x; }
int main() { return f0(1); }
