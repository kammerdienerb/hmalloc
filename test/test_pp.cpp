#include <string>
#include <iostream>

std::string s  = "this string should be too long for SSA -- I'll add some more just to be sure. ";
int main() {
    std::string s2 = s;
    std::cout << s2 + s << "\n";
}
