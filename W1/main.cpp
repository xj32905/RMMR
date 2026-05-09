#include <iostream>
using namespace std;

int main(int argc, char* argv[]) {
    cout << "程序名：" << argv[0] << endl;
    if (argc > 1) {
        cout << "你传入了 " << argc - 1 << " 个参数：" << endl;
        for (int i = 1; i < argc; i++) {
            cout << "参数" << i << "：" << argv[i] << endl;
        }
    } else {
        cout << "没有传入额外参数。" << endl;
    }
    return 0;
}
