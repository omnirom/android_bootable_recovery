#include <unistd.h>

int main(int argc, char **argv) {
	execl("sbin/recovery_twrp", "sbin/recovery_twrp", NULL);
	return 0;
}
