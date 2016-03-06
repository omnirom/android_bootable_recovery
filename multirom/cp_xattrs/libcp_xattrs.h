#ifndef LIBCPXATTRS_H
#define LIBCPXATTRS_H

#include <string>
#include <map>
#include <vector>

bool cp_xattrs_single_file(const std::string& from, const std::string& to);
bool cp_xattrs_recursive(const std::string& from, const std::string& to, unsigned char type);
bool cp_xattrs_list_xattrs(const std::string& path, std::map<std::string, std::vector<char> > &res);

#endif
