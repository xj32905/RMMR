#include<iostream>
using namespace std;
template<swapNum>
int main(){
    int a=10;
    int b=20;
    int& c=a;
    c=b;
    cout<<"a="<<a<<"b="<<b<<endl;
    cout<<"hello word\n";
    return 0;
}
//1. 语法错误（会直接编译报错）
template<swapNum>	第 3 行	完全多余的错误写法，笔记里的 template 是泛型模板，这里用不上，直接删掉就行
cout<<"hello word\n";	第 10 行	word 拼写错误，应该是 world，虽然不影响运行，但最好改对
2. 逻辑错误
int& c=a; c=b;	第 7-8 行	想实现交换，但这两句根本做不到交换：c 是 a 的别名，c=b 只是把 b 的值赋值给了 a，结果是 a 和 b 都变成了 20，根本没交换
没有自定义函数	整个代码	作业明确要求体现「自定义函数」语法点，把所有逻辑都写在 main 里，无法满足要求

#include <iostream>
using namespace std;
void swapNum(int &x, int &y) {
    int temp = x;
    x = y;
    y = temp;
}

int main() {
    int a = 10;
    int b = 20;
    cout << "交换前：a=" << a << " b=" << b << endl;
    swapNum(a, b); // 调用自定义函数
    cout << "交换后：a=" << a << " b=" << b << endl;
    cout << "hello world\n";
    return 0;
}
