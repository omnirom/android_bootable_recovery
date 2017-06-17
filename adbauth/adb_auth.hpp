
#pragma once

#include <string>

void adb_auth_start();

// private functions
void adb_auth_log(std::string msg);
void* adb_auth_main(void *arg);
void adb_auth_handle_adbd();
