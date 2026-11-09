#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define BUF_SIZE 4096

typedef struct {
	ssize_t len;
	char data[BUF_SIZE];
} shm_t;

static char shm_name[32], sem_w_name[32], sem_r_name[32];
static pid_t pid;
static int status, reaped;

static void cleanup(void) {
	shm_unlink(shm_name);
	sem_unlink(sem_w_name);
	sem_unlink(sem_r_name);
}

#define DIE(msg) do { \
	cleanup(); \
	write(STDERR_FILENO, msg, sizeof(msg) - 1); \
	exit(EXIT_FAILURE); \
} while (0)

static void on_chld(int sig) {
	(void)sig;
}

static void make_name(char *out, const char *prefix, pid_t p) {
	int i = 0;
	while (*prefix) {
		out[i++] = *prefix++;
	}
	char t[16];
	int k = 0;
	unsigned v = (unsigned)p;
	do {
		t[k++] = '0' + v % 10;
		v /= 10;
	} while (v > 0);
	while (k > 0) {
		out[i++] = t[--k];
	}
	out[i] = '\0';
}

static int wait_sem(sem_t *s) {
	while (sem_wait(s) == -1) {
		if (errno != EINTR) {
			return -1;
		}
		if (!reaped && waitpid(pid, &status, WNOHANG) == pid) {
			reaped = 1;
			return -1;
		}
	}
	return 0;
}

int main(void) {
	char name[256];
	int len = 0;
	char c;

	while (len < (int)sizeof(name) - 1 && read(STDIN_FILENO, &c, 1) == 1 && c != '\n') {
		name[len++] = c;
	}
	name[len] = '\0';
	if (len == 0) {
		DIE("ошибка: пустое имя файла\n");
	}

	pid_t me = getpid();
	make_name(shm_name, "/shm_", me);
	make_name(sem_w_name, "/sem_w_", me);
	make_name(sem_r_name, "/sem_r_", me);

	struct sigaction sa;
	sa.sa_handler = on_chld;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		DIE("ошибка: не удалось установить обработчик сигнала\n");
	}

	int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd == -1) {
		DIE("ошибка: не удалось создать shared memory\n");
	}
	if (ftruncate(fd, sizeof(shm_t)) == -1) {
		DIE("ошибка: не удалось задать размер shared memory\n");
	}
	shm_t *shm = mmap(NULL, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm == MAP_FAILED) {
		DIE("ошибка: не удалось выполнить mmap\n");
	}
	close(fd);

	sem_t *sem_w = sem_open(sem_w_name, O_CREAT | O_EXCL, 0600, 1);
	if (sem_w == SEM_FAILED) {
		DIE("ошибка: не удалось создать семафор\n");
	}
	sem_t *sem_r = sem_open(sem_r_name, O_CREAT | O_EXCL, 0600, 0);
	if (sem_r == SEM_FAILED) {
		DIE("ошибка: не удалось создать семафор\n");
	}

	pid = fork();
	if (pid == -1) {
		DIE("ошибка: не удалось создать процесс (fork)\n");
	}

	if (pid == 0) {
		char *args[] = {"child", name, shm_name, sem_w_name, sem_r_name, NULL};
		char *env[] = {NULL};
		execve("./child", args, env);

		const char msg[] = "ошибка: не удалось запустить дочернюю программу (execve)\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		_exit(EXIT_FAILURE);
	}

	int failed = 0;

	while (1) {
		if (wait_sem(sem_w) == -1) {
			failed = 1;
			break;
		}
		ssize_t n = read(STDIN_FILENO, shm->data, BUF_SIZE);
		if (n < 0) {
			failed = 1;
			n = 0;
		}
		shm->len = n;
		sem_post(sem_r);
		if (n == 0) {
			break;
		}
	}

	if (!reaped) {
		if (waitpid(pid, &status, 0) == -1) {
			DIE("ошибка: не удалось дождаться дочернего процесса\n");
		}
	}

	munmap(shm, sizeof(shm_t));
	sem_close(sem_w);
	sem_close(sem_r);
	cleanup();

	if (failed) {
		DIE("ошибка: не удалось передать данные дочернему процессу\n");
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		DIE("ошибка: дочерний процесс завершился с ошибкой\n");
	}

	return 0;
}