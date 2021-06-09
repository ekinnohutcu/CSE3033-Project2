#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>

#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */
#define psNUMBER_OF_STRING 10
#define psMAX_STRING_SIZE 40
#define DT_DIR 4

//CREATES COMMAND LINKED LIST
typedef struct node {
    char* val;
    struct node * next;
} command;

//CREATE A QUEUE FOR THE BACKGROUND PROCESSES
typedef struct background_process {
    pid_t pid;
    char* command;
    struct background_process* next;
} backgroundProcess;

backgroundProcess* backgroundQueue = NULL;
// ARRAY TO STORE RUNG AND TERMINATED PROCESS SO THAT WE CAN DISPLAY THEM WHEN ps_all COMMAND CALLED
char psAllArray[psNUMBER_OF_STRING][psMAX_STRING_SIZE] ={"", "", "", "", "", "", "", "", "", ""};

//VARIABLES FOR SIGNAL HANDLING
int ForegroundProcessCheck = 0;
int currentForegroundProcess;


/* The setup function below will not return any value, but it will just: read
in the next command line; separate it into distinct arguments (using blanks as
delimiters), and set the args array entries to point to the beginning of what
will become null-terminated, C-style strings. */

void setup(char inputBuffer[], char *args[], int *background) {
    int length, /* # of characters in the command line */
    i,     /* loop index for accessing inputBuffer array */
    start, /* index where beginning of next command parameter is */
    ct;    /* index of where to place the next parameter into args[] */

    ct = 0;

    /* read what the user enters on the command line */
    length = read(STDIN_FILENO, inputBuffer, MAX_LINE);

    /* 0 is the system predefined file descriptor for stdin (standard input),
       which is the user's screen in this case. inputBuffer by itself is the
       same as &inputBuffer[0], i.e. the starting address of where to store
       the command that is read, and length holds the number of characters
       read in. inputBuffer is not a null terminated C-string. */

    start = -1;
    if (length == 0)
        exit(0); /* ^d was entered, end of user command stream */

    /* the signal interrupted the read system call */
    /* if the process is in the read() system call, read returns -1
    However, if this occurs, errno is set to EINTR. We can check this  value
    and disregard the -1 value */
    if ((length < 0) && (errno != EINTR)) {
        perror("error reading the command");
        exit(-1); /* terminate with error code of -1 */
    }

    printf(">>%s<< \n", inputBuffer);
    for (i = 0; i < length; i++) {
        /* examine every character in the inputBuffer */

        switch (inputBuffer[i]) {
            case ' ':
            case '\t': /* argument separators */
                if (start != -1) {
                    args[ct] = &inputBuffer[start]; /* set up pointer */
                    ct++;
                }
                inputBuffer[i] = '\0'; /* add a null char; make a C string */
                start = -1;
                break;

            case '\n': /* should be the final char examined */
                if (start != -1) {
                    args[ct] = &inputBuffer[start];
                    ct++;
                }
                inputBuffer[i] = '\0';
                args[ct] = NULL; /* no more arguments to this command */
                break;

            default: /* some other character */
                if (start == -1)
                    start = i;
                if (inputBuffer[i] == '&') {
                    *background = 1;
                    inputBuffer[i - 1] = '\0';
                }
        }            /* end of switch */
    }                /* end of for */
    args[ct] = NULL; /* just in case the input line was > 80 */

    for (i = 0; i <= ct; i++)
        printf("args %d = %s\n",i,args[i]);
} /* end of setup routine */




// CHECK FOR FILE EXITS OR NOT
int fileExistenceCheck(const char *path) {

    FILE *fptr = fopen(path, "r"); //OPEN THE TO READ


    if (fptr == NULL)  //IF NOT EXISTS, RETURN 0
        return 0;

    //IF THE FILE EXIST, IT WILL CLOSE THE FILE RETURN 1
    fclose(fptr);

    return 1;
}


//FOR THE "A PART" OF THE PROJECT, WHEN WE GIVE A PROGRAM TO EXECUTE IT WITH execv, WE NEED TO FIND ITS PATH. THIS FUNCTION FINDS THE PATH OF GIVEN PROGRAM.
char** findPath(char *args[]) {

    // TAKES THE PATHS WITH getenv(PATH) SPILT IT WITH ":"
    char *env = getenv("PATH");
    char *str = (char*)malloc(sizeof(char)*1000);
    strcpy(str, env);
    char delimiter[] = ":";

    char *ptr = strtok(str, delimiter);
    char **paths; //array of the paths
    paths = (char**)malloc(sizeof(char*) * 100);
    int j = 0;

    //PUTS THE TOKENS TO paths
    while(ptr != NULL) {
        paths[j] = (char *)malloc(sizeof(char) * 100);
        strcpy(paths[j], ptr);
        j++;
        ptr = strtok(NULL, delimiter);
    }


    int k;
    int i = 0;

    char **temp;
    char **path;

    temp = (char **)malloc(sizeof(char *) * 100);
    path = (char **)malloc(sizeof(char *) * 100);

    // PUT TOGETHER THE PATH FOUND AND THE ARGUMENT TAKEN FROM USER WHICH IS THE NAME OF THE PROGRAM
    for (k = 0; k < j; k++) {
        temp[k] = (char *)malloc(sizeof(char) * 100);
        path[k] = (char *)malloc(sizeof(char) * 100);

        strcpy(temp[k], paths[k]);
        strcat(temp[k], "/");
        strcat(temp[k], args[0]);

        //CHECK WHETHER FILE EXITS OR NOT
        if (fileExistenceCheck(temp[k])) {
            strcpy(path[i], temp[k]);
            i++;
        }
    }
    //DEALLOCATE THE MEMORY AREA FOR THE TEMP
    free(temp);
    return path;
}


//TO ADD NEW BACKGROUND PROCESSES TO QUEUE
void addBackgroundProcess(pid_t pid, char* command){
    if(backgroundQueue == NULL){ // IF THE QUEUE IS EMPTY THEN CREATES THE FIRST ELEMENT
        backgroundQueue = (backgroundProcess*)malloc(sizeof(backgroundProcess));
        backgroundQueue->command = (char*)malloc(sizeof(char) * strlen(command));
        strcpy(backgroundQueue->command, command);
        backgroundQueue->pid = pid;
        backgroundQueue->next = NULL;
        return;
    }

    // TO FIND END OF THE QUEUE SO THAT WE CAN CREATE A NEW ELEMENT AND PUT THE PROCESS TO LAST ELEMENT
    backgroundProcess* last = backgroundQueue;
    while(last->next != NULL){
        last = last->next;
    }
    backgroundProcess* new = (backgroundProcess*)malloc(sizeof(backgroundProcess));
    new->command = (char*)malloc(sizeof(char)*strlen(command));
    strcpy(new->command, command);
    new->pid = pid;
    new->next = NULL;
    last->next = new;
}

//REMOVE THE ELEMENT FROM THE BACKGROUND PROCESS QUEUE WITH GIVEN PID
void removeFromBackgroundQueue(int pid){

    // CHANGE THE TEXT ( xterm (Pid=1000)--> xterm ) OF THE TERMINATED PID TO THE psAllArray
    for (int i=0; i<10;i++){
        char str_pid[5]; //SINCE PID HAS 5 CHARACTER
        snprintf(str_pid,5,"%d)",pid);

        if(strstr(psAllArray[i], str_pid)){  //FINDS THE TERMINATED PROCESS ID IN THE paAllArray AND CHANGES IT'S VALUE THEN BREAK THE LOOP
            char delim[] = " ";
            char *ptr = strtok(psAllArray[i], delim);
            strcpy(psAllArray[i], ptr);
            break;
        }
    }

    // REMOVE THE PROCESS FROM THE backgroundQueue
    if(backgroundQueue != NULL && backgroundQueue->pid == pid) {// IF THE PROCESS IN THE HEAD OF QUEUE
        backgroundProcess *killedProcess = backgroundQueue;
        backgroundQueue = killedProcess->next;
        killedProcess->next = NULL;
        free(killedProcess);
        return;
    }
    else if(backgroundQueue != NULL) {// IF IT'S NOT ON THE HEAD OF QUEUE
        backgroundProcess *iter = backgroundQueue;
        while(iter->next != NULL) {
            if(iter->next->pid == pid) {
                backgroundProcess *killedProcess = iter->next;
                killedProcess->next = iter->next;
                killedProcess->next = NULL;
                free(killedProcess);
                return;
            }
        }
    }
}



// TO STOP THE CURRENT FOREGROUND PROCESS WITH ^Z
static void signalHandler() {
    int status;
    // IF THERE IS A FOREGROUND PROCESS.
    if(ForegroundProcessCheck) {
        // ERROR CHECK BEFORE STOPPING THE FOREGROUND PROCESS
        kill(currentForegroundProcess, 0); // IF SIG IS 0, THEN NO SIGNAL IS SENT, BUT EXISTENCE AND PERMISSION CHECKS ARE STILL PERFORMED.
        if(errno == ESRCH) { // IF THE PROCESS COULD NOT FOUND
            fprintf(stderr, "\nProcess %d not found\n", currentForegroundProcess);
            ForegroundProcessCheck = 0;
            printf("myshell: ");
            fflush(stdout);
        }
        else{ //IF THERE IS A FOREGROUND PROCESS.
            kill(currentForegroundProcess, SIGSTOP); //STOP THE PROCESS
            waitpid(currentForegroundProcess, &status, WNOHANG);
            printf("\n");
            ForegroundProcessCheck = 0;
        }
    }
        // IF THERE IS NO FOREGROUND PROCESS, DO NOTHING
    else{
        printf("\nmyshell: ");
        fflush(stdout);
    }
}

// IF THERE IS ANY CHILD PROCESS OF THE KILLED PROCESS
void childProcessHandler() {
    int status;
    pid_t pid;

    while(1) {

        pid = waitpid(-1, &status, WNOHANG); // GET PID FOR TERMINATED CHILD PROCESS.
        if (pid == 0){ // IF THERE IS A NON TERMINATED CHILD, RETURN.
            return;
        }
        else if (pid == -1){ //THERE IS NO CHILD PROCESS
            return;
        }
            //  REMOVE THE TERMINATED PROCESS FROM QUEUE BY USING ITS PID.
        else
            removeFromBackgroundQueue(pid);

    }
}

// WHEN WE TAKE THE COMMAND AS INPUT, TO EXECUTE THAT IN A NEW PROCESS.
void execute(char** paths, char* args[], int* background){
    //IF THERE IS A BACKGROUND PROCESS IN THE BACKGROUND QUEUE.
    if(*background != 0){
        signal(SIGCHLD, childProcessHandler);  //SIGCHILD, THE CHILD PROCESS STATUS HAS CHANGED
    }
    //FORK A CHILD.
    pid_t childPid;
    childPid = fork();

    //IF FORK FAILED.
    if(childPid == -1) {
        fprintf(stderr, "Failed to fork.\n");
        return;
    }
    // TRY TO RUN THE CHILD PROCESS
    if(childPid == 0) {
        execv(paths[0], args); // IT TAKES THE FIRST PATH FROM THE paths AND THE ARGUMENT GIVEN BY THE USER
        fprintf(stderr, "Failed execv.\n");
        //kill(childPid, SIGKILL);
        return;
    }

    int status;

    //  IF THE COMMAND TAKEN FROM USER DOES NOT CONTAIN &, IT IS A FOREGROUND PROCESS (background = 0))
    if(*background == 0) {
        ForegroundProcessCheck = 1;
        currentForegroundProcess = childPid;
        waitpid(childPid, &status, 0); // WAITS FOR THE CHILD
        ForegroundProcessCheck = 0;
    }

    // BACKGROUND PROCESSES
    if(*background == 1){

        addBackgroundProcess(childPid, args[0]); // IF PROCESS IS A BACKGROUND PROCESS, ADD IT INTO BACKGROUND QUEUE
        backgroundProcess* temp = backgroundQueue;

        //ADD THE NEW PROCESS TO psAllArray ARRAY SO WE CAN DISPLAY THEM WHEN ps_all COMMAND IS CALLED
        for (int i=0; i<10;i++){
            if (!strcmp(psAllArray[i], "")){
                char str[40];
                snprintf(str,40, "%s (Pid=%d)",args[0],childPid);
                strcpy(psAllArray[i], str);
                break;
            }
        }
    }
    *background = 0;

}

//FOR ps_all IN THE PART B OF THE PROJECT, THIS FUNCTION WILL DISPLAY THEM
void write_ps_all(){
    for (int i=0; i<10;i++){
        char copy_str[40];
        char delim[] = "(";
        strcpy(copy_str, psAllArray[i]);
        char * token=strtok(copy_str, delim);
        token = strtok(NULL, " ");
        if(token){
            printf("Running: [%d] %s \n",i+1, psAllArray[i]);
        }

        else if(strcmp("", psAllArray[i])){
            printf("Finished: [%d] %s \n",i+1, psAllArray[i]);
            strcpy(psAllArray[i], "");
        }
    }
    return;
}

void search(char* args[],char path[500]){
    DIR * d = opendir(path); // OPEN THE PATH
    if(d==NULL) return;
    struct dirent * dir; // FOR THE DIRECTORY ENTRIES

    if(!strcmp(args[1],"-r")){


        while ((dir = readdir(d)) != NULL) //  IF WE ARE ABLE TO READ STH FROM THE DIRECTORY
        {

            if(dir-> d_type != DT_DIR) {// IF THE TYPE IS NOT DIRECTORY

                char header[2];  // TO STORE THE CHARACTERS AFTER . IN FILE NAME
                snprintf(header,2, "%c",dir->d_name[(strlen(dir->d_name)-1)]);

                //IF THE FILE IS NAME ENDS WITH .c/ .C/ .h/ .H
                if(!strcmp(header,"c") || !strcmp(header,"C") || !strcmp(header,"h") || !strcmp(header,"H")){
                    char line[200]; // TO STORE WORDS TAKEN FROM THE FILE
                    int count = 0;  // TO SHOW THE LINE NUMBER
                    char d_path[200];

                    strcpy(d_path,path);
                    strcat(d_path,"/");
                    strcat(d_path,dir->d_name); // RE-ARRANGE THE PATH

                    FILE *in_file = fopen(d_path, "r"); // OPEN THE FILE TO READ
                    if (in_file == NULL) {
                        fprintf(stderr, "Failed to open file.\n");
                        continue;
                    }
                    // READ THE FILE LINE BY LINE UNTIL THERE IS NO MORE LINE
                    while (fgets(line, 200, in_file) != NULL) {
                        count++;
                        // IF SEARCHED WORD FOUND IN LINE
                        if (strstr(line, args[2]) != 0) {
                            printf("%d ./%s --> %s\n",count,dir->d_name, line);
                        }
                    }
                    fclose(in_file);
                }
            }
            // IF IT IS A DIRECTORY AND ITS' NAME NOT "." OR ".."
            else if(dir -> d_type == DT_DIR && strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0 )
            {
                char d_path[200];

                strcpy(d_path,path);
                strcat(d_path,"/");
                strcat(d_path,dir->d_name);
                search(args,d_path); // RE-ARRANGE THE PATH
            }

        }

    }

    else{
        while ((dir = readdir(d)) != NULL) //  IF WE ARE ABLE TO READ STH FROM THE DIRECTORY
        {
            if(dir-> d_type != DT_DIR) {// IF THE TYPE IS NOT DIRECTORY
                char header[2];
                snprintf(header,2, "%c",dir->d_name[(strlen(dir->d_name)-1)]);

                if(!strcmp(header,"c") || !strcmp(header,"C") || !strcmp(header,"h") || !strcmp(header,"H")){
                    char line[200]; // TO STORE WORDS TAKEN FROM THE FILE
                    int count = 0; // TO SHOW THE LINE NUMBER

                    FILE *in_file = fopen(dir->d_name, "r"); // OPEN THE FILE TO READ
                    if (in_file == NULL) {
                        fprintf(stderr, "Failed to open file.\n");
                        continue;
                    }

                    // READ THE FILE LINE BY LINE UNTIL THERE IS NO MORE LINE
                    while (fgets(line, 200, in_file) != NULL) {
                        count++;
                        // IF SEARCHED WORD FOUND IN LINE
                        if (strstr(line, args[1]) != 0) {
                            printf("%d ./%s --> %s",count,dir->d_name, line);
                        }
                    }
                    fclose(in_file);
                }
            }
        }
        closedir(d); // CLOSE THE DIRECTORY.
    }
    return;


}

int main(void) {
    char inputBuffer[MAX_LINE];   /*buffer to hold command entered */
    int background;               /* equals 1 if a command is followed by '&' */
    char *args[MAX_LINE / 2 + 1]; /*command line arguments */
    char **paths; // TO STORE THE PATHS FOUNDED VIA findPaths FUNCTION
    int status;

    // SIGACTION
    struct sigaction signalAction;
    signalAction.sa_handler = signalHandler;  // THERE IS A ^Z SIGNAL
    signalAction.sa_flags = SA_RESTART;
    // sigaction error check
    if ((sigemptyset( &signalAction.sa_mask ) == -1) || ( sigaction(SIGTSTP, &signalAction, NULL) == -1 ) ) {
        fprintf(stderr, "Couldn't set SIGTSTP handler\n");
        return 1;
    }

    command *head;
    head = malloc(sizeof(command));


    while (1) {
        background = 0;
        printf("myshell: ");
        fflush(NULL);
        // seperates the command by spaces, adds to the args array and check if & character entered.
        setup(inputBuffer, args, &background);

        // IF USER DOES NOT ENTER ANY ARGUMENTS.
        if (args[0] == NULL)
            continue;

        int count = 0;
        while (args[count] != NULL) {
            count++;
        }


        // FOR THE PART C
        int in=0;
        int out=0;
        int both=0;
        int error=0;
        char input[128];
        char output[128];


        if(args[2] != NULL){
            // IF THERE IS A '<' or '>>' OR '2>' IN args, THEN MAKE THE args[1] = NULL

            if(strcmp(args[2],">")==0){ // CREATE / TRUNCATE (WRITE)
                args[2]=NULL;
                strcpy(output, args[3]);
                out=1;
            }

            else if(strcmp(args[2],">>")==0){ //APPEND (WRITE)
                args[2]=NULL;
                strcpy(output, args[3]);
                out=2;
            }

            else if(strcmp(args[2],"<") == 0){ //READ
                args[2]=NULL;
                strcpy(input, args[3]);
                in=1;
            }

            else if(strcmp(args[2],"2>") == 0){ // ERROR
                args[2]=NULL;
                strcpy(output,args[3]);
                error=1;
            }

            else if(strcmp(args[2],"<") == 0 && strcmp(args[4],">") == 0){ // READ AND WRITE
                args[2]=NULL;
                args[4]=NULL;
                strcpy(input,args[3]);
                strcpy(output,args[5]);
                both=1;
            }


            // IF THERE IS A '<' IN THE ARGS
            if(in){
                int fileDescriptor0;
                if ((fileDescriptor0 = open(input, O_RDONLY, 0)) < 0){
                    fprintf(stderr, "Failed to open file.\n");
                    exit(0);
                }
                // COPY THE CONTENT OF fileDesctiptor TO STDIN_FILENO
                dup2(fileDescriptor0, STDIN_FILENO);
                close(fileDescriptor0);
            }

            //IF args[2]= '>>'
            else if (out==2){
                int fileDescriptor1 ;
                if ((fileDescriptor1 = creat(output, O_APPEND)) < 0){
                    fprintf(stderr, "Failed to open file.\n");
                    exit(0);
                }

                dup2(fileDescriptor1, STDOUT_FILENO);
                close(fileDescriptor1);
            }

            //IF args[2]= '>'
            else if (out==1){
                int fileDescriptor2 ;
                if ((fileDescriptor2 = creat(output,  0644))< 0){
                    fprintf(stderr, "Failed to open file.\n");
                    exit(0);
                }

                dup2(fileDescriptor2, STDOUT_FILENO);
                close(fileDescriptor2);
            }

            //IF args[2]= '2>'
            else if(error == 1){
                int fileDescriptor2;
                if ((fileDescriptor2 = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0){
                    fprintf(stderr, "Failed to open file.\n");
                    exit(0);
                }
                dup2(fileDescriptor2, STDERR_FILENO);
                close(fileDescriptor2);
            }

            else if (both==1){
                int fileDescriptor0;
                if ((fileDescriptor0 = open(input, O_RDONLY, 0)) < 0){
                    fprintf(stderr, "Failed to open file.\n");
                    exit(0);
                }
                // COPY THE CONTENT OF fileDesctiptor TO STDIN_FILENO
                dup2(fileDescriptor0, STDIN_FILENO);
                close(fileDescriptor0);

                int fileDescriptor1 ;
                if ((fileDescriptor1 = creat(output, 0644)) < 0){
                    fprintf(stderr, "Failed to open file.\n");
                    exit(0);
                }

                dup2(fileDescriptor0, fileDescriptor0);
                close(fileDescriptor1);
            }
        }

        //IF THE COMMAND IS EXIT
        if(!strcmp(args[0], "exit")) {
            //IF THERE IS BACKGROUND PROCESSES, PRINT WARNING MESSAGE TO KILL THEM.
            if(backgroundQueue != NULL){
                printf("There is some background processes. You have to kill them before call the exit.\n");
                continue;
            }
            //IF THERE IS NO ANY BACKGROUND PROCESS, EXIT.
            else{
                exit(0);
            }
        }

            //IF THE COMMAND IS ps_all
        else if(!strcmp(args[0], "ps_all")) {
            //IF THE backgroundQueue IS NOT EMPTY
            if(backgroundQueue != NULL){
                write_ps_all();
            }
        }

            //ID THE COMMAND IS search
        else if(!strcmp(args[0], "search") || !strcmp(args[0], "search -r") ) {
            char pth[10]=".";
            search(args, pth);

        }

            //FOR A PART OF THE PROJECT
        else {
            // READ ARGUMENTS AND EXECUTE IT
            paths = findPath(args);
            if (background == 1)
                args[count - 1] = NULL;
            execute(paths, args, &background);
        }

    }
}
