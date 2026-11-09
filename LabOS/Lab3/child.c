#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define BUF_SIZE 4096

typedef struct {
	ssize_t len;
	char data[BUF_SIZE];
} shm_t;

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

static int wait_sem(sem_t *s) {
	while (sem_wait(s) == -1) {
		if (errno != EINTR) {
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	if (argc < 5) {
		DIE("ошибка: не указаны имя файла и имена объектов\n");
	}

	file = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (file == -1) {
		DIE("ошибка: не удалось открыть файл\n");
	}

	int fd = shm_open(argv[2], O_RDWR, 0600);
	if (fd == -1) {
		DIE("ошибка: не удалось открыть shared memory\n");
	}
	shm_t *shm = mmap(NULL, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm == MAP_FAILED) {
		DIE("ошибка: не удалось выполнить mmap\n");
	}
	close(fd);

	sem_t *sem_w = sem_open(argv[3], 0);
	if (sem_w == SEM_FAILED) {
		DIE("ошибка: не удалось открыть семафор\n");
	}
	sem_t *sem_r = sem_open(argv[4], 0);
	if (sem_r == SEM_FAILED) {
		DIE("ошибка: не удалось открыть семафор\n");
	}

	while (1) {
		if (wait_sem(sem_r) == -1) {
			DIE("ошибка: не удалось дождаться семафора\n");
		}
		ssize_t n = shm->len;
		if (n <= 0) {
			break;
		}

		for (ssize_t i = 0; i < n; ++i) {
			char c = shm->data[i];

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

		sem_post(sem_w);
	}

	end_line();

	munmap(shm, sizeof(shm_t));
	sem_close(sem_w);
	sem_close(sem_r);

	if (close(file) == -1) {
		DIE("ошибка: не удалось закрыть файл\n");
	}
	return 0;
}