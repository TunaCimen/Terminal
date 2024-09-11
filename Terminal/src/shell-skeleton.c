#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../module/psvis.h"
#include "../commands/hdiff.h"
#include "../commands/sizedir.h"

const char *sysname = "mishell";

const char *directory = "/usr/";

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
    AUTO_COMPLETE = 3,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

int is_executable(const char *path){
	struct stat status;
	return stat(path, &status) == 0 && (status.st_mode & S_IXUSR);

}
char *resolve_path(const char *command){
	char *path = getenv("PATH");
	char *paths = strdup(path);
	char *token = strtok(paths, ":");
	while(token != NULL) {
		char *filepath = malloc(strlen(token) + strlen(command) + 2);
		sprintf(filepath, "%s/%s", token, command);
		if (is_executable(filepath)) {
			free(paths);
			return filepath;
		}
		free(filepath);
		token = strtok(NULL, ":");
	}
	free(paths);
	return NULL;
}

void search_directory(char* directory, char* buf, char** matches,int*size){
    DIR* dir = opendir(directory);
    if(dir == NULL){
        printf("Fail at opening dir: %s\n error %s\n ", directory, strerror(errno));
        return; //error code
    }
    char filepath[1024];
    struct dirent* entry;
    int i = 0;
    while((entry = readdir(dir)) != NULL){
        if(entry->d_type == DT_REG){ //check if regular file
            snprintf(filepath, sizeof(filepath), "%s/%s", directory, entry->d_name);
            if(!is_executable(filepath))continue;
            if(strncmp(entry->d_name,buf, strlen(buf)) == 0){
                matches[*size] = strdup(entry->d_name);
                i++;
                (*size)++;
                if(i >= 1024) break;
            }
        }
    }

    closedir(dir);
}

void autocorrect(char* commd, char ** matches){
    char *path = getenv("PATH");
    char *paths = strdup(path);
    char *token = strtok(paths, ":");
    int size = 0;
    while(token != NULL){
        if(token[0] == '~'){
            token = strtok(NULL, ":");
            continue;
        }
        search_directory(token, commd,matches,&size);
        token = strtok(NULL,":");
    }
    matches[size] = NULL;
    /*for(int j = 0; j<size;j++){
        printf("%s\n", matches[j]);
        free(matches[j]);
    }
    */
    free(paths);
}


/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n",
		   command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");

	for (i = 0; i < 3; i++) {
		printf("\t\t%d: %s\n", i,
			   command->redirects[i] ? command->redirects[i] : "N/A");
	}

	printf("\tArguments (%d):\n", command->arg_count);

	for (i = 0; i < command->arg_count; ++i) {
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	}

	if (command->next) {
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
	if (command->arg_count) {
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}

	for (int i = 0; i < 3; ++i) {
		if (command->redirects[i])
			free(command->redirects[i]);
	}

	if (command->next) {
		free_command(command->next);
		command->next = NULL;
	}

	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);

	// trim left whitespace
	while (len > 0 && strchr(splitters, buf[0]) != NULL) {
		buf++;
		len--;
	}

	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL) {
		// trim right whitespace
		buf[--len] = 0;
	}

	// auto-complete
	if (len > 0 && buf[len - 1] == '?') {
		command->auto_complete = true;
	}

	// background
	if (len > 0 && buf[len - 1] == '&') {
		command->background = true;
	}

	char *pch = strtok(buf, splitters);
	if (pch == NULL) {
		command->name = (char *)malloc(1);
		command->name[0] = 0;
	} else {
		command->name = (char *)malloc(strlen(pch) + 1);
		strcpy(command->name, pch);
	}

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1) {
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// trim left whitespace
		while (len > 0 && strchr(splitters, arg[0]) != NULL) {
			arg++;
			len--;
		}

		// trim right whitespace
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL) {
			arg[--len] = 0;
		}

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// piping to another command
		if (strcmp(arg, "|") == 0) {
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0) {
			// handled before
			continue;
		}

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<') {
			redirect_index = 0;
		}

		if (arg[0] == '>') {
			if (len > 1 && arg[1] == '>') {
				redirect_index = 2;
				arg++;
				len--;
			} else {
				redirect_index = 1;
			}
		}

		if (redirect_index != -1) {
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 &&
			((arg[0] == '"' && arg[len - 1] == '"') ||
			 (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}

		command->args =
			(char **)realloc(command->args, sizeof(char *) * (arg_index + 1));

		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;

	// increase args size by 2
	command->args = (char **)realloc(
		command->args, sizeof(char *) * (command->arg_count += 2));

	// shift everything forward by 1
	for (int i = command->arg_count - 2; i > 0; --i) {
		command->args[i] = command->args[i - 1];
	}

	// set args[0] as a copy of name
	command->args[0] = strdup(command->name);

	// set args[arg_count-1] (last) to NULL
	command->args[command->arg_count - 1] = NULL;

	return 0;
}

void prompt_backspace() {
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {

	size_t index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];


    //auto-correction prompt



	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &=
		~(ICANON |
		  ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    if(command->name != NULL){
        command->auto_complete = false;
        for(size_t i = 0; i< strlen(command->name);i++){
            buf[i] = command->name[i];
            index ++;
        }
    }
    else{
        show_prompt();
        buf[0] = 0;

    }


	while (1) {
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		// handle tab
		if (c == 9) {
            buf[index++] = '?';
            break;

        }

		// handle backspace
		if (c == 127) {
			if (index > 0) {
				prompt_backspace();
				index--;
			}
			continue;
		}

		if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
			continue;
		}

		// up arrow
		if (c == 65) {
			while (index > 0) {
				prompt_backspace();
				index--;
			}

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}

	// trim newline from the end
	if (index > 0 && buf[index - 1] == '\n') {
		index--;
	}

	// null terminate string
	buf[index++] = '\0';

	strcpy(oldbuf, buf);

	parse_command(buf, command);


	//print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main() {
	while (1) {
		struct command_t *command = malloc(sizeof(struct command_t));

		// set all bytes to 0
		memset(command, 0, sizeof(struct command_t));

		int code;
		code = prompt(command);
		if (code == EXIT) {
			break;
		}

		code = process_command(command);
        while(code == AUTO_COMPLETE){
            code = prompt(command);
            if (code == EXIT) {
                break;
            }
            code = process_command(command);
        }
		if (code == EXIT) {
			break;
		}
		free_command(command);
	}

	printf("\n");
	return 0;
}


int process_built_ins(struct command_t *command){
    if(strcmp(command->name, "psvis")== 0){
        run_psvis(command->args[1]);
        return SUCCESS;
    }
    if(strcmp(command->name,"backup") == 0){
        char* file_name =  command->args[1];
        char backup_command[8096];
        char * home = getenv("HOME");
        snprintf(backup_command,sizeof(backup_command),"sudo mv %s %s",file_name, home);
        printf("%s", backup_command);
        int result = system(backup_command);
        if(result<0){
            perror("Fault at backing up");
        }
        return SUCCESS;
    }
    if(strcmp(command->name, "revert") == 0){
        char* file_name = command->args[1];
        char* home = getenv("HOME");
        char backup_command[8096],cwd[256];
        getcwd(cwd, sizeof(cwd));
        DIR *dir = opendir(home);
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, file_name) == 0) {
                snprintf(backup_command,sizeof(backup_command),"sudo mv %s/%s %s",home,file_name, cwd);
                snprintf(backup_command,sizeof(backup_command),"sudo mv %s/%s %s",home,file_name, cwd);
                system(backup_command);
                printf("%s\n", backup_command);
                printf("File reverted succesfully");
                closedir(dir);
                return SUCCESS; // File found
            }
        }
        printf("File to revert not found!");
        return SUCCESS;
    }

    if(strcmp(command->name, "sizedir") == 0){
        run_sizedir(command->arg_count, command->args);
        return SUCCESS;
    }

    if(strcmp(command->name, "hdiff") == 0){
        hdiff(command->arg_count, command->args);
        return SUCCESS;
    }
    return EXIT;
}
int process_command(struct command_t *command) {

	int r;

    //auto-completion
   if(command->auto_complete){
       char *matches[1000];
       char* commd = malloc(strlen(command->name));
       strncpy(commd, command->name, strlen(command->name)-1);
       commd[strlen(command->name)-1] = '\0';
       autocorrect(commd,matches);
       int n = 0;
       while(matches[n] != NULL){
           n++;
       }
       if(n == 1){//single match
           for(size_t i = strlen(commd); i< strlen(matches[0]);i++){
               putchar(matches[0][i]);
           }
           command->name = matches[0];
           //printf("Auto-completed to--> %s", command->name); %%debugging purposes.
       }
       else if(n==0){
           printf("\n");
           char *args[] = {"ls", NULL};
           execvp("ls", args);
           return SUCCESS;
       }
       else{//multiple match
           printf("\n");
           for(int j = 0; j<n;j++){
               printf("%s\n", matches[j]);
               free(matches[j]);
           }
           show_prompt();
           for(size_t i = 0; i< strlen(commd); i++){
               putchar(commd[i]);
           }
           command->name = commd;
       }
       return AUTO_COMPLETE;
   }


	if (strcmp(command->name, "") == 0) {
		return SUCCESS;
	}

	if (strcmp(command->name, "exit") == 0) {
		return EXIT;
	}

	if (strcmp(command->name, "cd") == 0) {
		if (command->arg_count > 1) {
			r = chdir(command->args[1]);
			if (r == -1) {
				printf("-%s: %s\n", sysname,
					   strerror(errno));
			}

			return SUCCESS;
		}
	}

    if (command->next != NULL){
        //Implementing multiple piping.

        //Deducing the chain length
        int chain_length = 1;
        struct command_t *curr = command;
        while(curr->next != NULL){
            curr = curr->next;
            chain_length +=1;
        }
        //printf("Num of commands: %d" , chain_length);

        //Creating the file directory(ies) for redirecting I/O.
        int fd[2*(chain_length-1)];

        //creating the pipes.
        for(int i = 0; i<chain_length-1; i++){
            if(pipe(fd + 2*i) < 0 ){
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        for(int i = 0; i< chain_length; i++){
            pid_t pid = fork();
            if(pid == 0){
                if(i > 0){
                    dup2(fd[2*(i-1)], STDIN_FILENO);
                }
                if(i< chain_length -1){
                    dup2(fd[2*i +1], STDOUT_FILENO);
                }

                for(int j = 0; j<2*(chain_length-1);j++){
                    close(fd[j]);
                }
                struct command_t *c = command;
                for(int l=1; l<= i ; ++l ){
                    c = c->next;
                }
                printf("Executing command: %s", c->name);
                //built in command check
                int result = process_built_ins(c);
                if(result == EXIT){
                    execvp(c->name, c->args);
                }

                exit(EXIT_FAILURE);
            }
        }
        for(int i=0;i<2*(chain_length-1);i++){
            close(fd[i]);
        }

        for(int i = 0; i<chain_length; i++){
            wait(NULL);
        }
        return SUCCESS;

    }

    //mishell built-in commands.
    if(strcmp(command->name, "psvis")== 0){
        run_psvis(command->args[1]);
        return SUCCESS;
    }
    if(strcmp(command->name,"backup") == 0){
        char* file_name =  command->args[1];
        char backup_command[256];
        char * home = getenv("HOME");
        snprintf(backup_command,sizeof(backup_command),"sudo cp %s %s",file_name, home);
        int result = system(backup_command);
        if(result<0){
            perror("Fault at backing up");
            return SUCCESS;
        }
        printf("File backed upsuccesfully\n");
        return SUCCESS;
    }
    if(strcmp(command->name, "revert") == 0){
        char* file_name = command->args[1];
        char* home = getenv("HOME");
        char backup_command[8096],cwd[512];
        getcwd(cwd, sizeof(cwd));
        DIR *dir = opendir(home);
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, file_name) == 0) {
                snprintf(backup_command,sizeof(backup_command),"sudo cp %s/%s %s",home,file_name, cwd);
                snprintf(backup_command,sizeof(backup_command),"sudo cp %s/%s %s",home,file_name, cwd);
                int result = system(backup_command);
                if(result<0){
                    perror("Fault at reverting");
                    closedir(dir);
                    return SUCCESS;
                }
                printf("File reverted succesfully\n");
                closedir(dir);
                return SUCCESS; // File found
            }
        }
        printf("File to revert not found!");
        return SUCCESS;
    }

    if(strcmp(command->name, "sizedir") == 0){
        run_sizedir(command->arg_count, command->args);
        return SUCCESS;
    }

    if(strcmp(command->name, "hdiff") == 0){
        hdiff(command->arg_count, command->args);
        return SUCCESS;
    }



    pid_t pid = fork();
    if (pid == 0) { // Child process
        char* filepath = resolve_path(command->name);
        execv(filepath, command->args);
        perror("execv");  // If execv returns, an error occurred
        exit(EXIT_FAILURE);
    } else if (pid > 0) { // Parent process
        if (command->background) {
            printf("Process %d running in background.\n", pid);
            // Optionally, add the background process PID to a list to manage later
        } else {
            int status;
            waitpid(pid, &status, 0); // Wait only if not a background process
        }
        return SUCCESS;
    } else {
        perror("fork"); // Fork failed
        return EXIT_FAILURE;
    }

    printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}



