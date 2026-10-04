#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>

#define DIE(msg) do { \
	write(STDERR_FILENO, msg, sizeof(msg) - 1); \
	exit(EXIT_FAILURE); \
} while (0)

static int file;
static int64_t sum, cur;
static int in_num, neg, has_nums, bad;

static void write_sum(int64_t v) {
	char out[32];
	int pos = sizeof(out);
	uint64_t u = v < 0 ? -(uint64_t)v : (uint64_t)v;

	out[--pos] = '\n';
	do {
		out[--pos] = '0' + u % 10;
		u /= 10;
	} while (u > 0);
	if (v < 0) {
		out[--pos] = '-';
	}

	ssize_t size = sizeof(out) - pos;
	if (write(file, out + pos, size) != size) {
		DIE("ошибка: не удалось записать в файл\n");
	}
}

static void end_number(void) {
	if (in_num) {
		sum += neg ? -cur : cur;
		has_nums = 1;
	} else if (neg) {
		bad = 1;
	}
	cur = 0;
	in_num = 0;
	neg = 0;
}

static void end_line(void) {
	end_number();
	if (bad) {
		const char msg[] = "ошибка: некорректная строка пропущена\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
	} else if (has_nums) {
		write_sum(sum);
	}
	sum = 0;
	has_nums = 0;
	bad = 0;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		DIE("ошибка: не указано имя файла\n");
	}

	file = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (file == -1) {
		DIE("ошибка: не удалось открыть файл\n");
	}

	char buf[4096];
	ssize_t n;

	while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
		for (ssize_t i = 0; i < n; ++i) {
			char c = buf[i];

			if (c >= '0' && c <= '9') {
				in_num = 1;
				cur = cur * 10 + (c - '0');
			} else if (c == '-' && !in_num && !neg) {
				neg = 1;
			} else if (c == ' ' || c == '\t' || c == '\r') {
				end_number();
			} else if (c == '\n') {
				end_line();
			} else {
				bad = 1;
			}
		}
	}
	if (n < 0) {
		DIE("ошибка: не удалось прочитать данные из stdin\n");
	}

	end_line();

	if (close(file) == -1) {
		DIE("ошибка: не удалось закрыть файл\n");
	}
	return 0;
}
