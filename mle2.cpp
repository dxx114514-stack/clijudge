#include <iostream>
#include <cstring>
using namespace std;
int main() {
    char* p = new char[100*1024*1024]; // 100MB
    memset(p, 0, 100*1024*1024); // actually use it
    delete[] p;
    return 0;
}
