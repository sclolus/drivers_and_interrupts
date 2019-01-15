NAME=gen_scan_code_table
SRC=main.c
OBJ=$(SRC:.c=.o)
CFLAGS= -Wall -Wextra -Werror -O3 -g3 -fsanitize=address
CC=gcc

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $(NAME)

%.o: %.c
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f $(OBJ)

fclean: clean
	rm -f $(NAME)
