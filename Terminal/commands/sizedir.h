#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

double convert(long size, char flag) {
    double converted_size;
    if (flag == 'k') {
        converted_size=size / 1024.0;
        return converted_size;
    } 
    else if (flag == 'm') {
        converted_size=size / 1024.0 / 1024.0;
        return converted_size;
    }
    else if (flag == 'g') {
        converted_size=size / 1024.0 / 1024.0 / 1024.0;
        return converted_size;
    }
    else {
        return size; // if none of them, then bytes as default. (or 'b')
    }
}

void displaySize(double size, char flag) {
    if (flag == 'k') {
        printf("%.2f KBs\n", size);
    } 
    else if (flag == 'm') {
        printf("%.2f MBs\n", size);
    } 
    else if (flag == 'g') {
        printf("%.2f GBs\n", size);
    } 
    else {
        printf("%.2f bytes\n", size);
    }
}

long sizedir(char *path, char flag, int depth) {
    long totalSize = 0;
    DIR *dir;
    struct dirent *direntry;
    struct stat statbuf;
    char full_path[1024];

    dir = opendir(path);
    if (dir == NULL) {
        perror("Cannot opening directory!");
        return 0;
    }

    while ((direntry = readdir(dir)) != NULL) {
        if (strcmp(direntry->d_name, ".") != 0 && strcmp(direntry->d_name, "..") != 0) {

            snprintf(full_path, sizeof(full_path), "%s/%s", path, direntry->d_name);

            if (stat(full_path, &statbuf) == -1) {
                perror("Cannot get file status!");
                continue;
            }

            if (S_ISDIR(statbuf.st_mode)) {
                printf("%*sDirectory: %s - Size: ", depth * 4, "", full_path);
                long size = sizedir(full_path, flag, depth + 1);
                displaySize(convert(size, flag), flag);
                totalSize += statbuf.st_size; 
            }
            else {
                printf("%*sFile: %s - Size: ", depth * 4, "", full_path);
                double size = convert(statbuf.st_size, flag);
                displaySize(size, flag);
                totalSize += statbuf.st_size;
            }
        }
    }

    closedir(dir);
    return totalSize;
}

int run_sizedir(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Wrong typing, can be used as: %s <directory_path> <-b or -k or -m or -g> (as the unit)\n", argv[0]);
        return 1;
    }
    printf("%d", argc);
    char flag = 'b';
    if (strcmp(argv[2], "-k") == 0) flag = 'k';
    else if (strcmp(argv[2], "-m") == 0) flag = 'm';
    else if (strcmp(argv[2], "-g") == 0) flag = 'g';

    long size = sizedir(argv[1], flag, 0); 

    printf("Total size of the directory '%s': ", argv[1]);
    displaySize(convert(size, flag), flag);

    return 0;
}
