#include "tests.h"
#include "x86_desc.h"
#include "lib.h"
#include "terminal.h"
#include "rtc.h"
#include "file_sys.h"
#include "process.h"
#include "sys_call.h"
#include "gensound.h"
#include "scheduler.h"

#define PASS 1
#define FAIL 0
#define END_OF_EXCEPTION 0x20

/* format these macros as you see fit */
#define TEST_HEADER 	\
	printf("[TEST %s] Running %s at %s:%d\n", __FUNCTION__, __FUNCTION__, __FILE__, __LINE__)
#define TEST_OUTPUT(name, result)	\
	printf("[TEST %s] Result = %s\n", name, (result) ? "PASS" : "FAIL");
extern int get_rtc_counter();

static inline void assertion_failure(){
    /* Use exception #15 for assertions, otherwise
       reserved by Intel */
    asm volatile("int $15");
}

/* Checkpoint 1 tests */

/* IDT Test - Example
 * 
 * Asserts that first 10 IDT entries are not NULL
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: Load IDT, IDT definition
 * Files: x86_desc.h/S
 */
int idt_test(){
    TEST_HEADER;

    int i;
    int result = PASS;
    for (i = 0; i < NUM_VEC; ++i){
        if ( i<20 && (idt[i].offset_15_00 == NULL) &&   // test first 20 vec we set
             (idt[i].offset_31_16 == NULL)){
            printf("1\n");
            assertion_failure();
            result = FAIL;
        }

        if(i < END_OF_EXCEPTION){
            if(idt[i].dpl != 0 || idt[i].size != 1 || idt[i].present != 1 || idt[i].seg_selector != KERNEL_CS){
                printf("2\n");
                assertion_failure();
                result = FAIL;
            }
        }
        else{
            if (i == 0x80) {
                if(idt[i].dpl != 3 || idt[i].size != 1 || idt[i].seg_selector != KERNEL_CS){
                    printf("3.1\n");
                    assertion_failure();
                    result = FAIL;
                }
                continue;
            }
            if(idt[i].dpl != 0 || idt[i].size != 1 || idt[i].seg_selector != KERNEL_CS){
                printf("3.2\n");
                assertion_failure();
                result = FAIL;
            }
        }
    }

    return result;
}

/* Page Structure Test
 *
 * Check every content in paging structures
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: paging set up
 * Files: boot.S, kernel.C, x86_desc.S
 */
int page_test() {
    TEST_HEADER;

    int result = PASS;
    int i;
    unsigned long tmp;
    uint32_t* tmp_cnt = (uint32_t*) page_directory;
    uint32_t* tmp_cnt0 = (uint32_t*) page_table0;

    // CR3
    asm volatile ("movl %%cr3, %0"
    : "=r" (tmp)
    :
    : "memory", "cc");
    if ((PDE (*) [1024]) tmp != &page_directory) {
        result = FAIL;
        printf("Incorrect CR3!");
    }

    // CR4
    asm volatile ("movl %%cr4, %0"
    : "=r" (tmp)
    :
    : "memory", "cc");
    if ((tmp & 0x10) == 0) {    // bit mask
        result = FAIL;
        printf("Incorrect CR4!");
    }

    // CR0
    asm volatile ("movl %%cr0, %0"
    : "=r" (tmp)
    :
    : "memory", "cc");
    if ((tmp & 0x80000000) == 0) {  // bit mask
        result = FAIL;
        printf("Incorrect CR0!");
    }

    // page directory
    if ((PTE (*) [1024]) ( *tmp_cnt & 0xFFFFF000) != &page_table0) {
        printf("Incorrect page directory: 0");
        result = FAIL;
    }
    if ((tmp_cnt[1] & 0x00400000) != 0x00400000) {
        printf("Incorrect page directory: 1");
        result = FAIL;
    }

    // page table
    for (i = 0; i < PAGE_TABLE_SIZE; ++i){
        if (i == VIDEO_MEMORY_INDEX) {
            if ((tmp_cnt0[i] & 0x000B8003) != 0x000B8003){  // logical and physical memory should match
                printf("Incorrect page table at %d", i);
                result = FAIL;
            }
            continue;
        }
        if ( tmp_cnt0[i] != 0){
            printf("Incorrect page table at %d", i);
            result = FAIL;
        }
    }

    return result;
}

/* Zero division Test
 *
 * Divide 0 to see if handled correctly
 * Inputs: None
 * Outputs: None
 * Side Effects: Cause zero-division error
 * Coverage: Divide Error exception
 * Files: boot.S, kernel.C
 */
int div0_test() {
    TEST_HEADER;
    unsigned long a = 0;
    unsigned long b = 1 / a;

    // should never get here
    printf("1 / 0 = %u\n", b);
    return FAIL;
}

/* Deference Test
 *
 * Deference to see if handled correctly
 * Inputs: None
 * Outputs: None
 * Side Effects: Cause paging exception if paging is turning on
 * Coverage: paging exception
 * Files: boot.S, kernel.C, x86_desc.S
 */
int dereference_test() {
    TEST_HEADER;
    unsigned long a = 0;
    unsigned long b;
    // dereference accessible pointer
    unsigned long* test_ptr = &a;
    if (*test_ptr != a) {
        printf("*test_ptr (0) = %u\n", a);
        return FAIL;
    }
    // then dereference 0
    b = *((unsigned long *)a);
    // should Never get here
    printf("*0 = %u\n", b);
    return FAIL;
}

/* Deference Test 2
 *
 * Deference pointers with given accessible and inaccessible addresses
 * Inputs: None
 * Outputs: None
 * Side Effects: Cause paging exception
 * Coverage: paging exception
 * Files: boot.S, kernel.C, x86_desc.S
 */
int deref_test2() {
    TEST_HEADER;
    unsigned long* a = (unsigned long*) 0x000B8010;
    unsigned long* b = (unsigned long*) 0x000C8000;
    // dereference accessible pointer
    printf("deref 0x000B8010\n");
    unsigned long tmp = *a;
    // dereference inaccessible pointer
    printf("deref 0x000C8000\n");
    tmp = *b;
    return FAIL;
}

///* RTC Test
//// *
//// * receive 1024 interrupts
//// * Inputs: None
//// * Outputs: PASS/FAIL
//// * Side Effects: None
//// * Coverage: RTC interrupts
//// * Files: kernel.c
//// */
//int rtc_test() {
//    TEST_HEADER;
//
//    int ref = get_rtc_counter();
//    int ctr;
//    printf("Count for 1 sec...\n");
//    while(1) {
//        ctr = get_rtc_counter();
//        if (ctr - ref > 1024) {
//            break;
//        }
//    }
//    return PASS;
//}


/* System Call Test
 *
 * int $0x80
 * Inputs: None
 * Outputs: None
 * Side Effects: Execute system call handler
 * Coverage: system call
 * Files: x86_desc.h/S
 */
static inline int naive_system_call_test() {
    TEST_HEADER;
    asm volatile("int $0x80");  // system call handler at index 0x80
    return FAIL;
}

/* Checkpoint 2 tests */

/* terminal Test
 *
 * receive 1024 interrupts
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: terminal
 * Files: kernel.c
 */
int terminal_test(){
    TEST_HEADER;
    int ret;
    long result = PASS;
    char user_buffer[KEYBOARD_BUF_SIZE + 1];
    char test_buffer[2*KEYBOARD_BUF_SIZE];
    int i;


    // test read
    printf("---------------Terminal read test starts-----------------\n");
    printf("We set terminal_read(0,user_buffer,-2) to test the invalid nbytes \n");
    i = terminal_read(STDIN,user_buffer,-2);
    if(i < 0)  printf("the invalid input could be rejected!\n");
    else  {
        printf("the invalid input could not be rejected!\n");
        result = FAIL;
        return result;
    }

    // 10 words test
    printf("\n please input at most 10 characters before pressing enter\n");

    ret = terminal_read(STDIN,user_buffer,10);
    user_buffer[ret] = '\0';
    printf("The contents you have put in is: %s",user_buffer);

    // at most 127 words test
    printf("please input whatever characters then pressing enter\n");
    ret = terminal_read(STDIN,user_buffer,250);

    user_buffer[ret] = '\0';
    printf("The contents you have put in is: %s\n",user_buffer);

    // test write
    printf("---------------Terminal write test starts-----------------\n");
    printf("\n we will firstly print out what you have just keyed in\n");

    terminal_write(STDOUT,user_buffer,ret);
    printf("\n");
    printf("\n now we will print out a string longer than 128 bytes \n");
    for(i = 0; i < 2*KEYBOARD_BUF_SIZE; i++){
        test_buffer[i] = 'a';
    }
    terminal_write(STDOUT,test_buffer,2*KEYBOARD_BUF_SIZE);
    printf("\n");




    return PASS;

}


/* RTC Test2
 *
 * set and read rtc in different interrupts
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: RTC interrupts
 * Files: rtc.c, rtc.h
 */
int rtc_test2() {
    TEST_HEADER;
    int result = PASS;
    int freq;
    int i,j;
    int frequencies[] = {4, 8, 16, 32, 64, 128, 256, 512, 1024};
    int fd;
    fd = rtc_open((uint8_t *) "fn");
    printf("Waiting 2 interrupts with RTC at 2 hz...\n");
    rtc_read(fd, NULL, 0);
    printf("1");
    rtc_read(fd, NULL, 0);
    printf("1\n");

    // test under valid frequencies
    for (i = 0; i < 9; i++) {   // 9: the number of valid frequency in our test
        freq = frequencies[i];
        if (rtc_write(fd, &freq, 4) != 0) {
            printf("fail setting rtc to %u hz\n", freq);
            result = FAIL;
        }
        printf("Waiting %u interrupts with RTC at %u hz...\n", freq, freq);
        for (j = 0; j < frequencies[i]; j++) {
            rtc_read(fd, NULL, 0);
            printf("1");
        }
        printf("\n");
    }

    // test under invalid frequencies
    freq = 1;
    if (rtc_write(fd, &freq, 4) == 0) {
        result = FAIL;
        printf("Illegal frequency value %u!\n", freq);
    }
    freq = 18;
    if (rtc_write(fd, &freq, 4) == 0) {
        result = FAIL;
        printf("Illegal frequency value %u!\n", freq);
    }
    freq = 2048;
    if (rtc_write(fd, &freq, 4) == 0) {
        result = FAIL;
        printf("Illegal frequency value %u!\n", freq);
    }

    rtc_close(fd);
    return result;
}

/* file_system_test
 *
 * Test our file system
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: File System
 * Files: file_sys.c
 */
int file_system_test() {
    TEST_HEADER;
    // Print the root directory
    int32_t fd, ret_val, buf_size = 32;
    char buf[32+1]; // Maximum byte is 32 with a extra end character
    if ((fd = dir_open((uint8_t *) "." ) == -1)) {
        printf("FAIL TO OPEN ROOT DIRECTORY\n");
        return FAIL;
    }
    printf("Now we test the directory file\n");
    while ((ret_val=dir_read(fd, buf, buf_size))!=0) {  
        if (ret_val == -1) {
            printf("FAILED TO READ\n");
            return FAIL;
        }
        if (terminal_write(1, buf, ret_val) == -1) {
            printf("FAILED TO WRITE TO STDOUT\n");
            return FAIL;
        }
        dentry_t my_file;
        if (read_dentry_by_name((uint8_t*)buf, &my_file) == -1) {
            printf("File not found\n");
            return FAIL;
        }   
        printf("\n");
        // printf("File type: %d and file size :%dB\n", file.file_type)
    }

    const char *valid_test_file[] = {"frame0.txt", "frame1.txt", "grep", "ls", 
                     "fish", "verylargetextwithverylongname.tx"};

    // Test to open the file
    int i;
    for (i = 0; i < sizeof(valid_test_file)/sizeof(const char *); ++i) {
        printf("\nPress Enter to Continue\n");
        terminal_read(STDIN, &buf, buf_size);
        clear();
        reset_screen();
        if ((fd = file_open((uint8_t *) valid_test_file[i] )) == -1) {
            printf("FAIL TO OPEN FILE\n");
            return FAIL;
        }
        // printf("In file_test: Now the fd = %d\n", fd);
        printf("Now we test the content of the file %s\n", valid_test_file[i]);
        while (0!=(ret_val=file_read(fd, buf, buf_size))) {
            if (ret_val==-1) {
                printf("FAILED TO READ\n");
                file_close(fd);
                return FAIL;
            }
            if (terminal_write(STDOUT, buf, ret_val) == -1) {
                printf("FAILED TO WRITE TO STDOUT\n");
                file_close(fd);
                return FAIL;
            }
            // printf("\n");
        }
        file_close(fd);
    }
    
    return PASS;
}

/* sys_file_op_test
 *
 * Test our file system for system support
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: File System
 * Files: file_sys.c
 */
int sys_file_op_test() {
   TEST_HEADER;
   // Print the root directory
   int32_t fd, ret_val, buf_size = 32;
   char buf[32+1];
   if ((fd = sys_open((uint8_t *) ".")) == -1) {
       printf("FAIL TO OPEN ROOT DIRECTORY\n");
       return FAIL;
   }
   // printf("In sys file_test: Now the fd = %d\n", fd);
   while ((ret_val = sys_read(fd, buf, buf_size)) != 0) {
       // printf("1\n");
       if (ret_val == -1) {
           printf("FAILED TO READ\n");
           return FAIL;
       }
       if (sys_write(STDOUT, buf, ret_val) == -1) {
           printf("FAILED TO WRITE TO STDOUT\n");
           return FAIL;
       }
       dentry_t my_file;
       if (read_dentry_by_name((uint8_t*)buf, &my_file) == -1) {
           printf("File not found\n");
           return FAIL;
       }
       printf("\n");
   }

   const char *valid_test_file[] = {"frame0.txt", "frame1.txt", "grep", "ls",
                                    "fish", "verylargetextwithverylongname.tx"};

   int i;
   for (i = 0; i < sizeof(valid_test_file)/sizeof(const char *); ++i) {
       printf("\nPress Enter to Continue\n");
       sys_read(STDIN, &buf, buf_size);
       clear();
       reset_screen();
       if ((fd = sys_open((uint8_t *) valid_test_file[i] )) == -1) {
           printf("FAIL TO OPEN FILE\n");
           return FAIL;
       }
       // printf("In file_test: Now the fd = %d\n", fd);
       printf("Now we test the content of the file %s\n", valid_test_file[i]);
       while (0!=(ret_val=sys_read(fd, buf, buf_size))) {
           if (ret_val==-1) {
               printf("FAILED TO READ\n");
               sys_close(fd);
               return FAIL;
           }
           if (sys_write(STDOUT, buf, ret_val) == -1) {
               printf("FAILED TO WRITE TO STDOUT\n");
               sys_close(fd);
               return FAIL;
           }
           // printf("\n");
       }
       sys_close(fd);
   }

   return PASS;
}

//int sys_execute_test() {
//    TEST_HEADER;
//
//    int32_t ret;
//
//    ret = sys_execute((uint8_t *) "ls");
//    if (ret == 0) return PASS;
//    else return FAIL;
//}

/* system_call_test
 *
 * Test for the system call
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: system call of 4 functions: read, write, open, close
 * Files: 
 */
int system_call_test() {
    TEST_HEADER;
    // Print the root directory
    int32_t fd, ret_val, buf_size = 32;
    char buf[32+1];
    char root_name[5] = ".";
    printf("%d\n", root_name);
    if ((fd = open((uint8_t *) root_name)) == -1) {
        printf("FAIL TO OPEN ROOT DIRECTORY\n");
        return FAIL;
    }
    // printf("In sys file_test: Now the fd = %d\n", fd);
    while ((ret_val = read(fd, buf, buf_size)) != 0) {
        // printf("1\n");
        if (ret_val == -1) {
            printf("FAILED TO READ\n");
            return FAIL;
        }
        if (write(STDOUT, buf, ret_val) == -1) {
            printf("FAILED TO WRITE TO STDOUT\n");
            return FAIL;
        }
        dentry_t my_file;
        if (read_dentry_by_name((uint8_t*)buf, &my_file) == -1) {
            printf("File not found\n");
            return FAIL;
        }
        printf("\n");
    }

    const char *valid_test_file[] = {"frame0.txt", "frame1.txt", "grep", "ls", 
                     "fish", "verylargetextwithverylongname.tx"};

    int i;
    for (i = 0; i < sizeof(valid_test_file)/sizeof(const char *); ++i) {
        printf("\nPress Enter to Continue\n");
        read(STDIN, &buf, buf_size);
        clear();
        reset_screen();
        if ((fd = open((uint8_t *) valid_test_file[i] )) == -1) {
            printf("FAIL TO OPEN FILE\n");
            return FAIL;
        }
        // printf("In file_test: Now the fd = %d\n", fd);
        printf("Now we test the content of the file %s\n", valid_test_file[i]);
        while (0!=(ret_val= read(fd, buf, buf_size))) {
            if (ret_val==-1) {
                printf("FAILED TO READ\n");
                close(fd);
                return FAIL;
            }
            if (write(STDOUT, buf, ret_val) == -1) {
                printf("FAILED TO WRITE TO STDOUT\n");
                close(fd);
                return FAIL;
            }
            // printf("\n");
        }
        close(fd);
    }

    return PASS;
}

/* Checkpoint 3 tests */
/* fs_err_test
 *
 * Test file system for error input
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: file system
 * Files: 
 */
long fs_err_test() {
    TEST_HEADER;

    long result = PASS;
    int32_t ret;
    uint8_t buf[32];
    int32_t fd;

    const char *valid_test_file[] = {"frame0.txt", "frame1.txt", "grep", "ls", 
                     "fish", "verylargetextwithverylongname.tx"};

    printf("Try passing error fds to fs syscalls...\n");
    if (-1 != (ret = read(-1, buf, 31))) {
        printf("read error return value %d\n", ret);
        result = FAIL;
    }
    if (-1 != (ret = read(99999999, buf, 31))) {
        printf("read error return value %d\n", ret);
        result = FAIL;
    }
    if (-1 != (ret = write(-1, buf, 31))) {
        printf("write error return value %d\n", ret);
        result = FAIL;
    }
    if (-1 != (ret = write(99999999, buf, 31))) {
        printf("write error return value %d\n", ret);
        result = FAIL;
    }
    if (-1 != (ret = close(-1))) {
        printf("close error return value %d\n", ret);
        result = FAIL;
    }
    if (-1 != (ret = close(99999999))) {
        printf("close error return value %d\n", ret);
        result = FAIL;
    }

    printf("\nTry passing NULL buffers to fs syscalls...\n");

    fd = open((uint8_t *) valid_test_file[0]);

    if (-1 != (ret = read(fd, NULL, 31))) {
        printf("read error return value %d\n", ret);
        result = FAIL;
    }
    if (-1 != (ret = write(fd, NULL, 31))) {
        printf("write error return value %d\n", ret);
        result = FAIL;
    }
    if (0 != (ret = close(fd))) {
        printf("close error return value %d\n", ret);
        result = FAIL;
    }

    return result;
}
/* shell_test
 *
 * Test for the sys_execute
 * Inputs: None
 * Outputs: PASS/FAIL
 * Side Effects: None
 * Coverage: system call execute
 * Files: 
 */
//int shell_test() {
//    TEST_HEADER;
//    /* non-execuatable file */
//    printf("Now Executing non-execuatable file:\n");
//    if (sys_execute((uint8_t *) "frame0.txt")!=-1) {
//        printf("Return value is wrong!\n");
//        return FAIL;
//    } else printf("Success\n");
//    /* execute shell */
//    printf("Execute Shell\n");
//    sys_execute((uint8_t *) "shell");
//    return PASS;
//}

/* file_closed_test
 *
 * test whether files are closed in the end of process
 * Inputs: None
 * Outputs: None
 * Side Effects: None
 */

void file_closed_test(){
    int i;
    pcb_t *cur_pcb = get_cur_process();
    printf("Running file closing test... ");
    for(i = 2; i < N_FILE_LIMIT; i++){
        if(cur_pcb->file_arr.files[i].flags == OCCUPIED){
            printf("There are files not closed\n");
            return;
        }
//        printf("%d\n", cur_pcb->file_arr.files[i].flags);
    }
     printf("All files are closed\n");
    return;
}

/* invalid_sys_call_test
 *
 * Inputs: None
 * Outputs: None
 * Side Effects: None
 */

void invalid_sys_call_test(){
    TEST_HEADER;

    // asm volatile
    asm volatile("       \n\
    movl $0, %%eax      \n\
    INT $0x80            \n\
    "
    :
    :
    :"cc", "memory", "eax");
}

/* Checkpoint 4 tests */
/* Checkpoint 5 tests */

/* Extra Point tests */

//int play_sound_test() {
//    TEST_HEADER;
//
//    long ret;
//
//    // gensound(1047, 3000);
//
//    // sys_play_sound(1047);
//    asm volatile ("INT $0x80"
//    : "=a" (ret)
//    : "a" (0x0B), "b" (1047)
//    : "memory", "cc");
//
//    printf("begin the sound:\n");
//
//    sleep(5000);
//
//    printf("stop the sound\n");
//
//    asm volatile ("INT $0x80"
//    : "=a" (ret)
//    : "a" (0x0C)
//    : "memory", "cc");
//
//    return PASS;
//}

//int play_music_test() {
//    TEST_HEADER;
//
//    play_song(0);
//
//    return PASS;
//}

/* launch_tests
 *
 * Inputs: None
 * Outputs: None
 * Side Effects: Execute tests
 */
/* Test suite entry point */
void launch_tests(){
//    int j;
//    int fd;
//    int freq = 32;
//    fd = rtc_open((uint8_t *) "fn");
//    for (j = 0; j < 4096; j++) {
//        rtc_read(fd, NULL, 0);
//        printf("1");
//    }
//    printf("\n");
//
//    for (j = 0; j < 2048; j++) {
//        rtc_read(fd, NULL, 0);
//    }
//
//    rtc_write(fd, &freq, 4);
//    for (j = 0; j < 32*4; j++) {
//        rtc_read(fd, NULL, 0);
//        printf("2");
//    }
//    printf("\n");

//     TEST_OUTPUT("file_system_test", file_system_test());
//    TEST_OUTPUT("shell_test", shell_test());
//    TEST_OUTPUT("fs_err_test", fs_err_test());
//     invalid_sys_call_test();
    // TEST_OUTPUT("play_sound_test", play_sound_test());
    // TEST_OUTPUT("play_music_test", play_music_test());
//    init_scheduler();
}
