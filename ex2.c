#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

sem_t signal_trace;    // 追踪信号量
sem_t signal_allocate; // 分配信号量
bool finished = false; // 程序完成标志
size_t pagesize;       // 页大小

/*
函数名：show_operation
功能：输出操作信息
*/
void show_operation() {
    FILE *fp;
    char filename[32];
    sprintf(filename, "/proc/%d/status", getpid());
    fp = fopen(filename, "r");
    char result[300] = "";
    char line[100];
    int index = 0;
    while (fgets(line, sizeof line, fp) != NULL) {
        if (index == 17 || index == 21) {
            printf("%s", line);
        }
        index += 1;
    }
    printf("----------\n");
    fclose(fp);
}
/*
函数名：show_status
功能：输出目前内存信息
*/
void show_status() {
    printf("PageSize = %ld B\n", pagesize);
    FILE *fp;
    char filename[32];
    sprintf(filename, "/proc/meminfo");
    fp = fopen(filename, "r");
    char result[300] = "";
    char line[100];
    int index = 0;
    while (fgets(line, sizeof line, fp) != NULL) {
        if (index < 3) {
            printf("%s", line);
        }
        index += 1;
    }
    printf("----------\n");
    fclose(fp);
}

/*
tracer
追踪线程主体
*/
void *tracer(void *argPtr) {
    // 打印内存初始信息
    show_status();

    // 进入循环
    // 循环结束条件：线程1结束文本中的操作序列
    while (!finished) {
        sem_wait(&signal_trace); // 请求trace信号量
        show_operation();        // 输出线程2上一步操作结果
        sem_post(&signal_allocate); // 释放allocate信号量，允许线程2下次操作
    }
    return NULL;
}

/*
allocater
分配线程主体
*/
void *allocater(void *argPtr) {
    // operate 操作代码
    // start_page 相对起始页码
    // page_num 操作的页数
    // protection 权限位串十进制方式
    int operate, start_page, page_num, protection;
    int base = 1 << 20;
    FILE *inputStream;
    inputStream = fopen("input.txt", "r");
    char *region;
    while (fscanf(inputStream, "%d%d%d%d", &operate, &start_page, &page_num,
                  &protection) != EOF) {
        sem_wait(&signal_allocate); // 等待allocate信号量
                                    // operate = 0 分配虚拟内存
        if (operate == 0) {
            region =
                mmap((void *)(pagesize * (base + start)), pagesize * page_num,
                     PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE,
                     0, 0);
            if (region == MAP_FAILED) {
                perror("Could not mmap\n");
                return NULL;
            }
            printf("mmap:    addr = %p, pages = %d\n", region, page_num);
        }
        // operate = 1 写入虚拟内存
        else if (operate == 1) {
            void *pos = (void *)(pagesize * (base + start_page));
            for (int i = 0; i < pagesize * page_num; i++)
                memcpy((void *)((pagesize * (base + start_page) + i)), "a", 1);
            printf("write:   addr = %p, pages = %d\n", region, page_num);
        }
        // operate = 2 锁定物理内存
        else if (operate == 2) {
            mlock((void *)(pagesize * (base + start_page)),
                  page_num * pagesize);
            printf("mlock:   addr = %p, pages = %d\n", region, page_num);
        }
        // operate = 3 解锁物理内存
        else if (operate == 3) {
            munlock((void *)(pagesize * (base + start_page)),
                    page_num * pagesize);
            printf("munlock: addr = %p, pages = %d\n", region, page_num);
        }
        // operate = 4 释放虚拟内存
        else if (operate == 4) {
            munmap((void *)(pagesize * (base + start_page)),
                   page_num * pagesize);
            printf("munmap:  addr = %p, pages = %d\n", region, page_num);
        }
        sem_post(&signal_trace); // 释放trace信号量，允许线程2进行操作分析
    }
    finished = true;
    return NULL;
}
/*
函数名：init_sem
功能：初始化信号量
*/
void init_sem() {
    sem_init(&signal_trace, 0, 1);
    sem_init(&signal_allocate, 0, 0);
}
int main() {
    // 获取页大小
    pagesize = getpagesize();

    // 初始化信号量
    init_sem();

    // 创建两个线程
    // 第一个线程为内存操作线程，以文本输入为操作序列
    // 第二个线程为追踪线程
    pthread_t th_trace, th_allocate;
    pthread_create(&th_trace, NULL, tracer, NULL);
    pthread_create(&th_allocate, NULL, allocater, NULL);
    pthread_join(th_trace, NULL);
    pthread_join(th_allocate, NULL);

    // 结束两个线程
    sem_destroy(&signal_trace);
    sem_destroy(&signal_allocate);

    return 0;
}