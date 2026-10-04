#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DIE(msg) do { \
	write(STDERR_FILENO, msg, sizeof(msg) - 1); \
	exit(EXIT_FAILURE); \
} while (0)

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

	signal(SIGPIPE, SIG_IGN);

	int pipe1[2];
	if (pipe(pipe1) == -1) {
		DIE("ошибка: не удалось создать канал\n");
	}

	pid_t pid = fork();
	if (pid == -1) {
		DIE("ошибка: не удалось создать процесс (fork)\n");
	}

	if (pid == 0) {
		close(pipe1[1]);
		if (dup2(pipe1[0], STDIN_FILENO) == -1) {
			DIE("ошибка: не удалось выполнить dup2\n");
		}
		close(pipe1[0]);

		char *args[] = {"child", name, NULL};
		char *env[] = {NULL};
		execve("./child", args, env);

		const char msg[] = "ошибка: не удалось запустить дочернюю программу (execve)\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		_exit(EXIT_FAILURE);
	}

	close(pipe1[0]);

	char buf[4096];
	ssize_t n;
	int failed = 0;

	while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
		if (write(pipe1[1], buf, n) != n) {
			failed = 1;
			break;
		}
	}
	if (n < 0) {
		failed = 1;
	}

	close(pipe1[1]);

	int status;
	if (waitpid(pid, &status, 0) == -1) {
		DIE("ошибка: не удалось дождаться дочернего процесса\n");
	}

	if (failed) {
		DIE("ошибка: не удалось передать данные дочернему процессу\n");
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		DIE("ошибка: дочерний процесс завершился с ошибкой\n");
	}

	return 0;
}
