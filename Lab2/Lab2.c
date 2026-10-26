#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static int running = 1;
static int peak = 1;
static int create_errors = 0;

static void enter_thread(void) {
    pthread_mutex_lock(&m);
    running++;
    if (running > peak) peak = running;
    pthread_mutex_unlock(&m);
}

static void leave_thread(void) {
    pthread_mutex_lock(&m);
    running--;
    pthread_mutex_unlock(&m);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void compare_range(int *a, size_t k, size_t j, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) {
        if (i % (2 * j) < j) {
            size_t l = i + j;
            int asc = ((i / k) % 2 == 0);
            int need_swap;
            if (asc) need_swap = (a[i] > a[l]);
            else     need_swap = (a[i] < a[l]);
            if (need_swap) {
                int t = a[i];
                a[i] = a[l];
                a[l] = t;
            }
        }
    }
}

static void sort_seq(int *a, size_t N) {
    for (size_t k = 2; k <= N; k = k * 2)
        for (size_t j = k / 2; j > 0; j = j / 2)
            compare_range(a, k, j, 0, N);
}

typedef struct {
    int *a;
    size_t k, j, from, to;
    bool demo;
} task_t;

static void *thread_func(void *arg) {
    task_t *t = arg;
    enter_thread();
    if (t->demo) sleep(5);
    compare_range(t->a, t->k, t->j, t->from, t->to);
    leave_thread();
    return NULL;
}

static void sort_par(int *a, size_t N, int p, bool demo) {
    pthread_t *threads = malloc(p * sizeof(pthread_t));
    task_t *tasks = malloc(p * sizeof(task_t));
    bool *created = malloc(p * sizeof(bool));

    for (size_t k = 2; k <= N; k = k * 2) {
        for (size_t j = k / 2; j > 0; j = j / 2) {
            bool first_step = demo && k == 2;
            for (int t = 0; t < p; t++) {
                tasks[t].a = a;
                tasks[t].k = k;
                tasks[t].j = j;
                tasks[t].from = N * t / p;
                tasks[t].to = N * (t + 1) / p;
                tasks[t].demo = first_step;
            }
            for (int t = 0; t < p - 1; t++) {
                created[t] = (pthread_create(&threads[t], NULL, thread_func, &tasks[t]) == 0);
                if (!created[t]) {
                    create_errors++;
                    compare_range(a, k, j, tasks[t].from, tasks[t].to);
                }
            }
            if (first_step) sleep(5);
            compare_range(a, k, j, tasks[p - 1].from, tasks[p - 1].to);
            for (int t = 0; t < p - 1; t++)
                if (created[t]) pthread_join(threads[t], NULL);
        }
    }
    free(threads);
    free(tasks);
    free(created);
}

static int is_sorted(const int *a, size_t n) {
    for (size_t i = 1; i < n; i++)
        if (a[i - 1] > a[i]) return 0;
    return 1;
}

static void print_arr(const char *title, const int *a, size_t n) {
    size_t k = n < 10 ? n : 10;
    printf("%s\n начало:", title);
    for (size_t i = 0; i < k; i++) printf(" %d", a[i]);
    printf("\n конец: ");
    for (size_t i = n - k; i < n; i++) printf(" %d", a[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    long n = 10000000;
    int p = 1;
    bool demo = false;
    bool show = false;
    int o;
    while ((o = getopt(argc, argv, "n:t:dp")) != -1) {
        if (o == 'n') n = atol(optarg);
        else if (o == 't') p = atoi(optarg);
        else if (o == 'd') demo = true;
        else if (o == 'p') show = true;
        else return 1;
    }
    if (p < 1) p = 1;
    if (n < 2) {
        fprintf(stderr, "Размер массива должен быть не меньше 2\n");
        return 1;
    }

    size_t N = 1;
    while (N < (size_t)n) N = N * 2;

    int *orig = malloc(N * sizeof(int));
    int *a = malloc(N * sizeof(int));
    int *b = malloc(N * sizeof(int));
    if (!orig || !a || !b) {
        fprintf(stderr, "Не хватает памяти\n");
        return 1;
    }
    srand(1);
    for (long i = 0; i < n; i++) orig[i] = rand();
    for (size_t i = n; i < N; i++) orig[i] = INT_MAX;

    printf("PID=%d, логических ядер=%ld, n=%ld, потоков (лимит)=%d\n",
           getpid(), sysconf(_SC_NPROCESSORS_ONLN), n, p);
    if (show) print_arr("Массив до сортировки:", orig, n);

    memcpy(a, orig, N * sizeof(int));
    double t0 = now_ms();
    sort_seq(a, N);
    double ts = now_ms() - t0;

    memcpy(b, orig, N * sizeof(int));
    t0 = now_ms();
    sort_par(b, N, p, demo);
    double tp = now_ms() - t0;

    if (show) print_arr("Массив после параллельной сортировки:", b, n);

    bool ok = memcmp(a, b, n * sizeof(int)) == 0 && is_sorted(b, n);
    double sp = ts / tp;
    double ef = sp / p;

    printf("Проверка: %s, пик потоков: %d\n", ok ? "OK" : "ОШИБКА", peak);
    if (create_errors > 0)
        printf("Ошибок создания потоков: %d (их работу выполнил главный поток)\n", create_errors);
    printf("Время последовательной версии: %.2f мс\n", ts);
    printf("Время параллельной версии: %.2f мс\n", tp);
    printf("Ускорение: %.3f\n", sp);
    printf("Эффективность: %.3f\n", ef);

    free(orig);
    free(a);
    free(b);
    return ok ? 0 : 2;
}
