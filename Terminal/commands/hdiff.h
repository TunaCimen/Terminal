#include <stdio.h>
#include <string.h>

#define MAX_LINE_LENGTH 1024

// Function prototypes
void compareLines(FILE *file1, FILE *file2);
void compareBytes(FILE *file1, FILE *file2);

int hdiff(int argc, char* argv[]) {

    if (argc < 4) {
        fprintf(stderr, "Usage: %s [-a | -b] file1 file2\n", argv[0]);
        return 1;
    }

    // Defaults
    char *mode = "-a";
    char *file1_name = argv[1];
    char *file2_name = argv[2];

    // If 4 arguments are provided, assume the first is the mode
    if (argc == 5) {
        mode = argv[1];
        file1_name = argv[2];
        file2_name = argv[3];
    }
    char cwd[256], file1_name_cwd[8096],file2_name_cwd[8096];
    getcwd(cwd, sizeof(cwd));
    snprintf(file1_name_cwd, sizeof(file1_name_cwd), "%s/%s", cwd,file1_name);
    snprintf(file2_name_cwd, sizeof(file2_name_cwd), "%s/%s", cwd,file2_name);

    FILE* file1 = fopen(file1_name_cwd, "r");
    FILE* file2 = fopen(file2_name_cwd, "r");

    if (file1 == NULL || file2 == NULL) {
        fprintf(stderr, "Error opening files.\n");
        return 1;
    }

    // Compare based on mode
    if (strcmp(mode, "-b") == 0) {
        compareBytes(file1, file2);
    } else {
        compareLines(file1, file2);
    }

    fclose(file1);
    fclose(file2);
    return 0;
}

void compareLines(FILE *file1, FILE *file2) {
    char line1[MAX_LINE_LENGTH];
    char line2[MAX_LINE_LENGTH];
    int line_number = 1;
    int diff_counter = 0;

    while (fgets(line1, MAX_LINE_LENGTH, file1) != NULL &&
           fgets(line2, MAX_LINE_LENGTH, file2) != NULL) {
        if (strcmp(line1, line2) != 0) {
            printf("%s:Line %d: %s", "file1", line_number, line1);
            printf("%s:Line %d: %s", "file2", line_number, line2);
            diff_counter++;
        }
        line_number++;
    }
    printf("%d different lines found\n", diff_counter);
}

void compareBytes(FILE *file1, FILE *file2) {
    int ch1 = getc(file1);
    int ch2 = getc(file2);
    int byte_position = 1;
    int diff_counter = 0;

    while (ch1 != EOF && ch2 != EOF) {
        if (ch1 != ch2) {
            printf("Difference at byte %d: %c in file1 and %c in file2\n", byte_position, ch1, ch2);
            diff_counter++;
        }
        ch1 = getc(file1);
        ch2 = getc(file2);
        byte_position++;
    }
    printf("%d different bytes found\n", diff_counter);
}
