#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <stdint.h>

# define PORT_FILE "/dev/port"
#ifndef PORT_OFF
# define PORT_OFF 0x60 // 0x60
#endif
# define ERR(format, ...) do {						\
		dprintf(2, "%s:%d " format "\n", __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__); \
	} while (0);

# define ERR_SYS_GEN(expr, callback) do {		\
		if (-1 == expr) {			\
			ERR("Failed to " #expr);	\
			callback;			\
		}					\
	} while (0);

static void hexdump_buf(char *buffer, uint64_t size)
{
	uint64_t    i = 0;

	while (i < size)
	{
		printf("%02hhx ", buffer[i]);
		i++;
		if (i % 16 == 0)
			printf("\n");

	}
	printf("\n");
}

static int32_t	fill_buf(char *buffer, uint64_t size, int fd)
{
	uint64_t    count = 0;
	ssize_t	    ret;

	while (-1 != (ret = read(fd, buffer + count, 1))) {
		count += ret;
		ERR_SYS_GEN(lseek(fd, PORT_OFF, SEEK_SET), return (EXIT_FAILURE));
		usleep(10000);
		if (count == size) {
			buffer[count] = '\0';
			return 0;
		}
	}
	return (ret);
}

int main(int argc, char **argv)
{
	static char	buf[129];
	int		fd;


	if (-1 == (fd = open(PORT_FILE, O_RDWR))) {
		ERR("Failed to open: " PORT_FILE);
		return (EXIT_FAILURE);
	}

	static char	command_buf[129];

	uint64_t     i = 0;
	while (i < sizeof(command_buf)) {
		command_buf[i] = i % 128 + 0x01;
		i++;
	}

	command_buf[0] = 0xEF;
	command_buf[1] = 0x02;
//	ssize_t ret;
	while (1) {
		if (-1 == fill_buf(buf, sizeof(buf) -1, fd)) {
			ERR("fill_buf() returned -1");
			ERR("read on " PORT_FILE " returned -1\n");
			return (EXIT_FAILURE);
		}
		hexdump_buf(buf, sizeof(buf) - 1);
		bzero(buf, sizeof(buf));
		/* ERR_SYS_GEN(write(fd, command_buf, sizeof(command_buf)), ERR("write failed\n")); */
		/* ERR_SYS_GEN(lseek(fd, PORT_OFF, SEEK_SET), return (EXIT_FAILURE)); */
	}
	ERR_SYS_GEN(close(fd), return (EXIT_FAILURE));
	return (EXIT_SUCCESS);
}
