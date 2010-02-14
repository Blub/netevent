#include <fcntl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct stat st;
	int fd;
	static char name[256];
	if (argc != 2) {
		fprintf(stderr, "usage: %s device-file\n", argv[0]);
		exit(1);
	}
	if (lstat(argv[1], &st) != 0) {
		perror("stat");
		exit(1);
	}
	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "not a character device: %s\n", argv[1]);
		exit(1);
	}
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}
	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) == -1) {
		perror("ioctl");
		exit(1);
	}
	fprintf(stdout, "%s\n", name);
	close(fd);
	return 0;
}
