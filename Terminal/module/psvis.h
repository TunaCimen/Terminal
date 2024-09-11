#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int load_kernel_module(char* pid) {
    char command[256]; // Buffer to hold the complete command string

    // Format the command string with the pid
    sprintf(command, "sudo insmod module/mymodule.ko pid=%s", pid);

    // Execute the command
    if (system(command) != 0) {
        perror("Failed to load kernel module");
        return -1;
    }
    return 0;
}

int unload_kernel_module() {
    if (system("sudo rmmod module/mymodule.ko") != 0) {
        perror("Failed to unload kernel module");
        return -1;
    }
    return 0;
}

int set_permissions() {
    if (system("sudo chmod 666 /dev/mymodule") != 0) {
        perror("Failed to set permissions on /dev/mymodule");
        return -1;
    }
    return 0;
}

int run_psvis(char* pid) {
    if (load_kernel_module(pid) != 0) {
        return 1;  // Kernel module failed to load, exit early
    }

    if (set_permissions() != 0) {
        unload_kernel_module();  // Attempt to clean up by unloading the module
        return 1;  // Exit after cleanup attempt
    }

    int fd = open("/dev/mymodule", O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device");
        unload_kernel_module();  // Attempt to clean up by unloading the module
        return 1;  // Exit after cleanup attempt
    }

    char read_buf[8096];
    ssize_t result = read(fd, read_buf, sizeof(read_buf));
    if (result < 0) {
        perror("Failed to read from the device");
        close(fd);
        unload_kernel_module();
        return 1;
    }
    //fprintf(stdout, read_buf);
    //fflush(stdout);
    FILE *fp = fopen("process_data.txt", "w");
    if (fp == NULL) {
        perror("Failed to open file for writing");
        return 1;
    }
    fprintf(fp, "%s", read_buf);
    fclose(fp);
    fprintf(stdout, "Process data saved to 'process_data.txt\n");
    fflush(stdout);
    int python_result = system("python3 visualize.py");
    if(python_result < 0){
        perror("Python script error.");
    }

    fprintf(stdout, "Kernel module loaded and device read successfully\n");
    fflush(stdout);
    close(fd);
    return unload_kernel_module();
}
