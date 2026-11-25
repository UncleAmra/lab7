/*
 * Lab7.c  -  user-space LED control program (Lab7 Item 1)
 *
 *   ./Lab7 LED1 on     turn LED1 on   (other LEDs unchanged)
 *   ./Lab7 LED1 off    turn LED1 off
 *   ./Lab7 LED1        query, prints "LED1 Status: 0"
 *
 * Talks to the driver through /dev/demo with open/ioctl/write/read/close,
 * so dmesg shows all five required messages.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

static int usage(const char *prog)
{
	fprintf(stderr, "Usage: %s LED1|LED2|LED3|LED4 [on|off]\n", prog);
	return 1;
}

int main(int argc, char *argv[])
{
	char cmd[32], reply[64];
	ssize_t n;
	size_t len;
	int fd;

	if (argc < 2 || argc > 3)
		return usage(argv[0]);
	if (strlen(argv[1]) != 4 || strncmp(argv[1], "LED", 3) ||
	    argv[1][3] < '1' || argv[1][3] > '4')
		return usage(argv[0]);
	if (argc == 3 && strcmp(argv[2], "on") && strcmp(argv[2], "off"))
		return usage(argv[0]);

	if (argc == 3)
		snprintf(cmd, sizeof(cmd), "%s %s", argv[1], argv[2]);
	else
		snprintf(cmd, sizeof(cmd), "%s", argv[1]);
	len = strlen(cmd);

	fd = open("/dev/demo", O_RDWR);
	if (fd < 0) {
		perror("open /dev/demo");
		return 1;
	}

	/* explicit ioctl call -> "Enter I/O Control function" in dmesg */
	if (ioctl(fd, 0, 0) < 0)
		perror("ioctl");

	if (write(fd, cmd, len) != (ssize_t)len) {
		perror("write");
		close(fd);
		return 1;
	}

	n = read(fd, reply, sizeof(reply) - 1);
	if (n <= 0) {
		perror("read");
		close(fd);
		return 1;
	}
	reply[n] = '\0';
	printf("%s", reply); /* e.g. "LED1 Status: 1" */

	close(fd);
	return 0;
}
