#ifndef COLORS_H
#define COLORS_H

#include <string>

/* FOREGROUND */
#define RST  "\001\e[0m\002"
#define KRED  "\001\e[1m\e[31m\002"
#define KGRN  "\001\e[32m\002"
#define KYEL  "\001\x1B[33m\002"
#define KBLU  "\001\e[34m\002"
#define KMAG  "\001\x1B[35m\002"
#define KCYN  "\001\x1B[36m\002"
#define KWHT  "\001\x1B[37m\002"

std::string green(const std::string &x);
std::string yellow(const std::string &x);
std::string blue(const std::string &x);
std::string magenta(const std::string &x);
std::string cyan(const std::string &x);
std::string white(const std::string &x);
std::string red(const std::string &x);
std::string bold(const std::string &x);
std::string underline(const std::string &x);

#endif // COLORS_H
