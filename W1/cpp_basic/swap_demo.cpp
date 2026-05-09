#include <iostream>
#include <string>
using namespace std;

template <typename T>
class Swapper {
public:
void swapNum(T &x, T &y) {
        T temp = x;
        x = y;
        y = temp;
    }

   
    void swapNum(T &a, T &b, T &c) {
        T temp = a;
        a = b;
        b = c;
        c = temp;
    }

    
    void swapIfDifferent(T &x, T &y) {
        if (x != y) {
            T temp = x;
            x = y;
            y = temp;
        }
    }

    static void print(const string &msg, const T &a, const T &b) {
        cout << msg << "a=" << a << " b=" << b << endl;
    }

    
    static void print(const string &msg, const T &a, const T &b, const T &c) {
        cout << msg << "a=" << a << " b=" << b << " c=" << c << endl;
    }
};

int main() {
 
    Swapper<int> sw;
    int a = 10, b = 20;
    Swapper<int>::print("交换前：", a, b);
    sw.swapNum(a, b);
    Swapper<int>::print("交换后：", a, b);

  
    int c = 30;
    Swapper<int>::print("三变量交换前：", a, b, c);
    sw.swapNum(a, b, c);   // 调用重载的三参数版本
    Swapper<int>::print("三变量交换后：", a, b, c);

  
    int m = 5, n = 5;
    Swapper<int>::print("swapIfDifferent 前：", m, n);
    sw.swapIfDifferent(m, n);   // m == n，不会交换
    Swapper<int>::print("swapIfDifferent 后：", m, n);
    int p = 7, q = 8;
    Swapper<int>::print("swapIfDifferent 前：", p, q);
    sw.swapIfDifferent(p, q);   // p != q，会交换
    Swapper<int>::print("swapIfDifferent 后：", p, q);


    Swapper<double> sw_d;
    double x = 1.5, y = 3.7, z = 9.2;
    Swapper<double>::print("double三变量交换前：", x, y, z);
    sw_d.swapNum(x, y, z);
    Swapper<double>::print("double三变量交换后：", x, y, z);

    cout << "hello world\n";
    return 0;
}
