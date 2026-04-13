#include "colors.h"
#include <sstream>

using namespace std;

string green(const string &x) {
    stringstream ss;
    ss << KGRN << x << RST;
    return ss.str();
}

string yellow(const string &x) {
    stringstream ss;
    ss << KYEL << x << RST;
    return ss.str();
}

string blue(const string &x) {
    stringstream ss;
    ss << KBLU << x << RST;
    return ss.str();
}

string magenta(const string &x) {
    stringstream ss;
    ss << KMAG << x << RST;
    return ss.str();
}

string cyan(const string &x) {
    stringstream ss;
    ss << KCYN << x << RST;
    return ss.str();
}

string white(const string &x) {
    stringstream ss;
    ss << KWHT << x << RST;
    return ss.str();
}

string red(const string &x) {
    stringstream ss;
    ss << KRED << x << RST;
    return ss.str();
}

string bold(const string &x) {
    stringstream ss;
    ss << "\001\x1B[1m\002" << x << RST;
    return ss.str();
}

string underline(const string &x) {
    stringstream ss;
    ss << "\001\x1B[4m\002" << x << RST;
    return ss.str();
}
