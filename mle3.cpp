#include <iostream>
#include <cstring>
using namespace std;
int main() {
    for(int i = 0; i < 10; i++) {
        char* p = new char[10*1024*1024]; // 10MB each
        memset(p, 0, 10*1024*1024);
    }
    return 0;
}
