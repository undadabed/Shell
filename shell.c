#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

pid_t pid;
int fg;
int* job;
int* jobid;
char** jobstatus;
char** jobname;
char* bg;
bool* valid;
bool status;
int s;
int words;
int spot;
bool isbg;
int current;
bool stop;
char* oginput;

void handle_sigint(int sig) {
	// handle sigint
	killpg(fg, SIGINT);
	valid[current] = false;
	printf("[%d] %d terminated by signal %d\n", current+1, jobid[current], sig);
}

void handle_sigtstp(int sig) {
	// handle sigtstp
	stop = true;
	killpg(fg, SIGTSTP);
	jobstatus[current] = "Stopped";
}

void handle_sigchld(int sig) {
	int status;
	pid_t p;
	if ((p = waitpid(-1, &status, WNOHANG)) != -1) {
		for (int i = 0; i < s; i++) {
			if (jobid[i] == p) {
				valid[i] = false;
				break;
			}
		}
	}
}

void absolute(char** input) {
	int c = 0;
	bool found = false;
	while (c < s) {
		if (!valid[c]) {
			found = true;
			break;
		}
		c++;
	}
	if (!found) {
		c = s;
	}
	spot = c;
	// handle absolute path
	// fork
	int stat;
	pid = fork();
	if (pid == 0) {
		if (input[0][0] == '/') {
			if (execve(input[0], input, NULL) == -1) {
				printf("%s; No such file or directory\n", input[0]);
				exit(EXIT_FAILURE);
			}
		}
		else if (strchr(input[0], '/')) {
			char *x = (char*)malloc(sizeof(getenv("PWD")) + 256);
			strcpy(x,getenv("PWD"));
			strcat(x,"/");
			strcat(x,input[0]);
			if (execve(x, input, NULL) == -1) {
				printf("%s; No such file or directory\n", input[0]);
				exit(EXIT_FAILURE);
			}
			free(x);
		}
		exit(EXIT_SUCCESS);
	}
	else {
		// put process in the array
		setpgid(pid,0);
		valid[spot] = true;
		jobid[spot] = pid;
		jobstatus[spot] = "Running";
		strcpy(jobname[spot], oginput);
		if (isbg) {
			bg[spot] = '&';
			printf("[%d] %d\n", spot+1, jobid[spot]);
		}
		else {
			bg[spot] = ' ';
		}
		// Handle code based on if it's a foreground job
		if (!isbg) {
			current = spot;
			fg = pid;
			while (!stop) {
				if(waitpid(pid, &stat, WNOHANG)) {
					break;
				}
			}
			if (WIFEXITED(stat) && !stop) {
				valid[spot] = false;
			}
			if (stop) {
				stop = false;
			}
		}
	}
	isbg = false;
}

int clean(char* in) {
	// break input into a sentence
	if (in == NULL) {
		return 0;
	}
	int size = 0;
	int n = 0;
	bool new = false;
	for (int i = 0; i < strlen(in); i++) {
		if (in[i] != ' ' && in[i] != '\n') {
			in[n++] = in[i];
			if (new == false) {
				size++;
				new = true;
			}
		}
		else if (new) {
			in[n++] = ' ';
			new = false;
		}
	}
	if (new) {
		in[n] = '\0';
	}
	else {
		in[n-1] = '\0';
	}
	if (in[strlen(in)-1] == '&') {
		in[strlen(in)-1] = '\0';
		isbg = true;
	}
	if (in[strlen(in)-1] == ' ') {
		words--;
		in[strlen(in)-1] = '\0';
	}
	return size;
}

void background(char** input) {
	// move foreground application to the background
	int index = atoi(input[1]+1) - 1;
	current = index;
	jobstatus[index] = "Running";
	kill(jobid[index], SIGCONT);
	bg[index] = '&';
}

bool is_apath(char* input) {
	// helper function for cd
	if (strncmp(input, "/", 1) == 0) {
		return true;
	}
	else {
		return false;
	}
}

void changedestination(char** input) {
	// implementation of cd
	if (words == 1) {
		setenv("PWD", getenv("HOME"), 1);
	}
	else {
		if (input[1][0] == '/' ) {
			setenv("PWD", input[1], 1);
		}
		else {
			char *x = (char*)malloc(sizeof(getenv("PWD")) + 256);
			strcpy(x,getenv("PWD"));
			strcat(x,"/");
			strcat(x,input[1]);
			setenv("PWD",x,1);
			free(x);
		}
	}
	chdir(getenv("PWD"));
}

void foreground(char** input) {
	// make a background task to the foreground
	int stat;
	int index = atoi(input[1]+1) - 1;
	current = index;
	if (strcmp(jobstatus[index], "Stopped") == 0) {
		jobstatus[index] = "Running";
		kill(jobid[index], SIGCONT);
	}
	bg[index] = ' ';
	fg = jobid[index];
	pid = fg;
	while (!stop) {
		if(waitpid(pid, &stat, WNOHANG)) {
			break;
		}
	}
	if (WIFEXITED(stat) && !stop) {
		valid[index] = false;
	}
	if (stop) {
		stop = false;
	}
}
void jobs(char** input) {
	// list out all active programs
	for (int i = 0; i < s; i++) {
		if (valid[i] == true) {
			printf("[%d] %d %s %s %c\n", i+1, jobid[i], jobstatus[i], jobname[i], bg[i]);
		}
	}
}
void terminate(char** input) {
	// kill a program
	int id = atoi(input[1] + 1) - 1;
	kill(jobid[id], SIGTERM);
	valid[id] = false;
	printf("[%d] %d terminated by signal 15\n", current+1, jobid[current]);
}

bool search(const char* directory, char* comp, bool found) {
	int die = 0;
	struct dirent **spider;
	die = scandir(directory, &spider, NULL, alphasort);
	if (die < 0) {
		printf("error!\n");
	}
	for (int i = 0; i < die; i++) {
	if(strcmp(spider[i]->d_name, ".") != 0 && strcmp(spider[i]->d_name, "..") != 0) {
		if (strcmp(spider[i]->d_name, comp) == 0) {
			found = true;
		}
	}
	}
	for (int i = 0; i < die; i++) {
		free(spider[i]);
	}
	free(spider);
	return found;
}

bool noPath(char** input) {
	const char* dirone = "/usr/bin";
	const char* dirtwo = "/bin";
	if (search(dirone, input[0], false)) {
		char** newinput = malloc(words * sizeof(char*));
		newinput[0] = malloc(1024*sizeof(char));
		newinput[0][0] = '\0';
		strcat(newinput[0],"/usr/bin/");
		strcat(newinput[0], input[0]);
		for (int i = 1; i < words; i++) {
			newinput[i] = input[i];
		}
		absolute(newinput);
		free(newinput[0]);
		free(newinput);
	}
	else if (search(dirtwo, input[0], false)) {
		char** newinput = malloc(words * sizeof(char*));
		newinput[0] = malloc(1024*sizeof(char));
		newinput[0][0] = '\0';
		strcat(newinput[0],"/bin/");
		strcat(newinput[0], input[0]);
		for (int i = 1; i < words; i++) {
			newinput[i] = input[i];
		}
		absolute(newinput);
		free(newinput[0]);
		free(newinput);
	}
	else {
		return false;
	}
	return true;
}

void execute(char** input) {
	// execute command based on what type (built in, absolute, relative, local)
	if (strcmp(input[0], "bg") == 0) {
		background(input);
	}
	else if (strcmp(input[0], "cd") == 0) {
		changedestination(input);
	}
	else if (strcmp(input[0], "exit") == 0) {
		status = false;
	}
	else if (strcmp(input[0], "fg") == 0) {
		foreground(input);
	}
	else if (strcmp(input[0], "jobs") == 0) {
		jobs(input);
	}
	else if (strcmp(input[0], "kill") == 0) {
		terminate(input);
	}
	else if (input[0][0] == '/') {
		absolute(input);
	}
	else if (strchr(input[0], '/')) {
		absolute(input);
	}
	else {
		if (!noPath(input)) {
			printf("%s: command not found\n", input[0]);
		}
	}
}

void tokenize(char* in) {
	// break sentence into tokens
	char** out = malloc(words*sizeof(char*));
	char* c = strtok(in, " ");
	out[0] = c;
	
	for(int i = 1; i < words; i++) {
		c = strtok(NULL, " ");
		out[i] = c;
	}
	execute(out);
	free(out);
}

void run() {
	// keep running program until its told to terminate
	char* input = NULL;
	size_t l = 0;
	status = true;
	while (status) {
		printf("> ");
		if (getline(&input,&l, stdin) == EOF) {
			break;
		}
		words = clean(input);
		strcpy(oginput, input);
		tokenize(input);
	}
	free(input);
}

void end() {
	for (int i = 0; i < s; i++) {
		if (valid[i]) {
			kill(jobid[i], SIGHUP);
			if (strcmp(jobstatus[i], "Stopped") == 0) {
				kill(jobid[i], SIGCONT);
			}
		}
	}
	
	//free everything
	for (int i = 0; i < s; i++) {
		free(jobname[i]);
	}
	free(job);
	free(oginput);
	free(jobid);
	free(jobstatus);
	free(jobname);
	free(bg);
	free(valid);
}

int main(int argc, char *argv[]) {
	// handle signals
	signal(SIGINT, handle_sigint);
	signal(SIGTSTP, handle_sigtstp);
	signal(SIGCHLD, handle_sigchld);
	//allocate to keep track of jobs
	stop = false;
	current = 0;
	pid = 0;
	fg = 0;
	isbg = false;
	s = 1024;
	spot = 0;
	job = malloc(s*sizeof(int));
	oginput = malloc(s*sizeof(char));
	jobid = malloc(s*sizeof(int));
	jobstatus = malloc(s*sizeof(char*));
	jobname = malloc (s*sizeof(char*));
	bg = malloc(s*sizeof(char));
	valid = malloc(s*sizeof(bool));
	for (int i = 0; i < s; i++) {
		valid[i] = false;
		jobname[i] = malloc(1024*sizeof(char));
	}
	run();
	end();
	return EXIT_SUCCESS;
}
