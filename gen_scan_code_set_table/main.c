#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

# define ERR(format, ...) do {						\
		dprintf(2, "%s:%d " format "\n", __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__); \
	} while (0);

# define ERR_SYS_GEN(expr, callback) do {		\
		if (-1 == expr) {			\
			ERR("Failed to " #expr);	\
			callback;			\
		}					\
	} while (0);

# define IN_BUFFER_SIZE 4096 * 10

enum	key_state {
	PRESSED,
	RELEASED
};

struct scan_key_code {
	uint64_t	code;
	char	        *key_name;
	enum key_state	state;
};

char	*concat(char *s1, char *s2, char *s3)
{
	uint64_t    len1 = strlen(s1);
	uint64_t    len2 = strlen(s2);
	uint64_t    len3 = strlen(s3);
	uint64_t    total_len =  len1 + len2 + len3;
	char	    *str;

	if (NULL == (str = malloc(total_len + 1))) {
		ERR("MALLOC_FAILURE");
		exit(EXIT_FAILURE);
	}
	memcpy(str, s1, len1);
	memcpy(str + len1, s2, len2);
	memcpy(str + len1 + len2, s3, len3);
	str[total_len] = 0;
	return str;
}

char	 *str_toupper(char *str)
{
	char	*tmp = str;
	while ((*str = (toupper(*str))))
		str++;

	return tmp;
}

int main(int argc, char **argv)
{
	char	*in_buffer;
	int	fd;

	if (argc != 2) {
		ERR("Invalid usage of %s\n", argv[0]);
		return EXIT_FAILURE;
	}
	ERR_SYS_GEN((fd = open(argv[1], O_RDONLY)), return EXIT_FAILURE);

	if (NULL == (in_buffer = malloc(IN_BUFFER_SIZE))) { // like if I had the time to code proper memory management...
		ERR("Malloc failure\n");
		return EXIT_FAILURE;
	}
	memset(in_buffer, 0, IN_BUFFER_SIZE);
	ssize_t	    ret;
	uint64_t    count = 0;
	while (0 < (ret = read(fd, in_buffer + count, IN_BUFFER_SIZE))) {
		count += (uint64_t)ret;
		assert(count < IN_BUFFER_SIZE);
	}
	in_buffer[count] = 0;
//	uint64_t    i = 0;
	char	    *current_token = NULL;
	uint64_t    state = 0;
	bool	    once = false;

	uint64_t    code;
	char	    *current_name;
	char	    *current_status;

	while ((current_token = strtok(!once ? in_buffer : NULL, " \t\b\v\a\n"))) {
		once = true;
		switch (state) {
		case 0: //code
			sscanf(current_token, "%lx", &code);
			while (current_token[strlen(current_token) - 1] == ',') {
				current_token = strtok(!once ? in_buffer : NULL, " \t\b\v\a\n");
				code <<= 8;
				uint64_t    plus_code;
				sscanf(current_token, "%lx", &plus_code);
				code |= plus_code;
			}
//			printf("%#02lx ", code);
			state++;
			break;
		case 1: //name
			sscanf(current_token, "%ms", &current_name);
//			printf("%s ", current_name);

			state++;
			break;

		case 2: //status
			if (strcmp("pressed", current_token) && strcmp("released", current_token))
			{
//				printf("%s ", current_token);
				char *tmp = current_name;
				current_name = concat(current_name, " ", current_token);
				free(tmp);
				continue;
			}
			sscanf(current_token, "%ms", &current_status);
//			printf("%s \n", current_status);

			printf("{ %#02lx, \"%s\", %s },\n", code, current_name, str_toupper(current_status));
			state = 0;
			free(current_name);
			free(current_status);
			break;
		default:
			assert(0);
		}
	}

	return EXIT_SUCCESS;
}
