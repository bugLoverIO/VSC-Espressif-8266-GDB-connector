#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

#define BUFF_SIZE 4096

const char* break_key   = "break-insert -f main\n";
const char* connect_key = "^connected";

int aStdinPipe[2];
int aStdoutPipe[2];

int runGDB(const char* szCommand, char*  aArguments[])
{
    int nResult = 0;
    
    // redirect stdin
    if (dup2(aStdinPipe[PIPE_READ], STDIN_FILENO) == -1) {
        exit(errno);
    }
    
    // redirect stdout
    if (dup2(aStdoutPipe[PIPE_WRITE], STDOUT_FILENO) == -1) {
        exit(errno);
    }
    
    // redirect stderr
    if (dup2(aStdoutPipe[PIPE_WRITE], STDERR_FILENO) == -1) {
        exit(errno);
    }
    
    // all these are for use by parent only
    close(aStdinPipe[PIPE_READ]);
    close(aStdinPipe[PIPE_WRITE]);
    close(aStdoutPipe[PIPE_READ]);
    close(aStdoutPipe[PIPE_WRITE]);
    
    printf (">>>");
    while (aArguments[nResult]) printf("%s ",aArguments[nResult++]);
    printf("\n");
 
     
    // run GDB process
    nResult = execv(szCommand, aArguments);
    
    // if we get here, an error occurred
    printf("GDB EXIT %d\n",nResult);
    exit(nResult);
}


int pipeGDB(int pid)
{

    char* buffer;
    struct timeval timeout;
    fd_set readFD,writeFD;
    
    // close unused file descriptors, these are for child only
    close(aStdinPipe[PIPE_READ]);
    close(aStdoutPipe[PIPE_WRITE]);
    
    buffer = malloc(BUFF_SIZE*sizeof(char));
    
    // Set timeout for select()
    timeout.tv_sec = 0;
    timeout.tv_usec = 100;
    
    
    while (waitpid (pid, NULL, WNOHANG) != pid) {
        int nbRead = 0;
        
        // Set on wich file descriptor listen
        FD_ZERO(&readFD);
        FD_SET(aStdoutPipe[PIPE_READ],&readFD);
        
        if (select(aStdoutPipe[PIPE_READ]+1,&readFD, NULL, NULL,&timeout)>0) {
            nbRead = read(aStdoutPipe[PIPE_READ],buffer,BUFF_SIZE);
            if (nbRead > 0) {
                
                // once connected to the ESP8266, force GDB to continue to allow inseting next break-points
                if (strnstr(buffer,connect_key,nbRead)) {
                    write(aStdinPipe[PIPE_WRITE],"cont\n",strlen("cont\n"));
                }
                write(STDOUT_FILENO, buffer, nbRead);
            }
        }
        
        FD_ZERO(&writeFD);
        FD_SET(STDIN_FILENO,&writeFD);
        
        if (select(STDIN_FILENO+1,&writeFD, NULL, NULL,&timeout)>0) {
            nbRead = read(STDIN_FILENO,buffer,BUFF_SIZE);
            if (nbRead<0)
                continue;
            
            // if "break-insert -f main" is added, replaces by break-insert -f loop
            if (strnstr(buffer,break_key,nbRead)) {
                char* pos = strnstr(buffer,"main",nbRead);
                strcpy(pos,"loop\n")  ;
            }
            
            if (NULL != buffer) {
                write(aStdinPipe[PIPE_WRITE], buffer, nbRead);
            }
        }
    }
    // done with these in this example program, you would normally keep these
    // open of course as long as you want to talk to the child
    close(aStdinPipe[PIPE_WRITE]);
    close(aStdoutPipe[PIPE_READ]);
    
    return 0;
}


int createGDB(const char* szCommand, char* const aArguments[]) {
    int nChild = 0;
    
    if (pipe(aStdinPipe) < 0) {
        perror("allocating pipe for child input redirect");
        return -1;
    }
    if (pipe(aStdoutPipe) < 0) {
        close(aStdinPipe[PIPE_READ]);
        close(aStdinPipe[PIPE_WRITE]);
        perror("allocating pipe for child output redirect");
        return -1;
    }
    
    nChild = fork();
    if (0 == nChild)        return runGDB(szCommand, aArguments);
    else if (nChild > 0)    return pipeGDB(nChild);
    
    // failed to create child
    close(aStdinPipe[PIPE_READ]);
    close(aStdinPipe[PIPE_WRITE]);
    close(aStdoutPipe[PIPE_READ]);
    close(aStdoutPipe[PIPE_WRITE]);

    printf("ERROR creating GDB process \n");
    return (-1);
}


void usage() {
    printf ("usage: gdbpipe [--help] --gdb path [up to 20 extra GDB parametes] \n");
    printf ("options\n");
    printf ("  -h, --help                : This message\n");
    printf ("  -g, --gdb <path>          : GDB path\n");
    printf("\n");
    abort ();
}


int parseParam (int argc, char **argv, char **strGDB, char **params)
{
    int c;
    char  nbParams = 0;
    
    opterr = 0;
    while (1)
    {
        
        const struct option long_options[] =
        {
            {"help",   	    no_argument, 0, 'h'},
            {"gdb",         required_argument, 0, 'g'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;
        
        c = getopt_long (argc, argv, "hg:", long_options, &option_index);
        
        /* Detect the end of the options. */
        if (c == -1)
            break;
        
        switch (c)
        {
            case 'h':
                usage();
                break;
                                
            case 'g':
                *strGDB = optarg;
                break;
                    
            case '?':
                params[nbParams++] = argv[optind-1];
                if ((!argv[optind]) || (*(argv[optind]) == '-')) break;
                params[nbParams++] = argv[optind];
                break;
                
            default:
                abort ();
        }
    }
    
    return 0;
}


int main (int argc, char **argv) {
    char* strGDB = NULL;

    const char MAX_PARAMS = 20;
    char* params[MAX_PARAMS] ;
    
    memset(params, 0, sizeof(char*)*MAX_PARAMS);
    parseParam (argc, argv, &strGDB, &params[1]);
    
    if (!strGDB) usage();
    
    params[0] = strGDB;
    createGDB(strGDB, params);
}
