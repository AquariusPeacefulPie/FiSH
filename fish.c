#include <stdio.h>
#include <stdlib.h>
#include "cmdline.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <limits.h>
#include <wordexp.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>


#define BUFLEN 1024

#define YES_NO(i) ((i) ? "Y" : "N")


struct array_process{
  size_t size;
  size_t capacity;
  pid_t *data;
};

struct array_process array_pid;

void array_push(struct array_process *self, int value){
  if (self->size == self->capacity) {
    self->capacity= self->capacity*2;
    pid_t *data = calloc(self->capacity, sizeof(pid_t)); 
    memcpy(data, self->data, self->size * sizeof(pid_t)); 
    free(self->data);
    self->data = data;
  }
  self->data[self->size] = value; 
  self->size++;
}



void array_remove(struct array_process *self, size_t index) {
	if(index < self->size){
		for(size_t i = index; i < self->size - 1; ++i){
		  self->data[i] = self->data[i+1];
		}
		self->size--;
	}
}


static size_t array_empty(struct array_process *a){
  return a->size==0;
}


static void array_create(struct array_process *a, size_t size_array){
  pid_t *tab = calloc(size_array,sizeof(pid_t));
  a->size=0;
  a->capacity = size_array;
  a->data=tab;
}

static void array_destroy(struct array_process *a){
  free(a->data);
  a->data=NULL;
}

//Print the current directory to the prompt
static void printCurrentDirectory(){
  //Get the current directory
  char buf_directory[PATH_MAX];
  getcwd(buf_directory,sizeof(buf_directory));

  printf("%s$ ",buf_directory);
}

/**
 * Print the termination status of a process
 * @param int     status : status of the process returned by waitpid
 * @param pid_t   process : process's number
 */
static void printStatus(int status,pid_t process){
  //Get the termination status
  if(WIFEXITED(status)){
    fprintf(stderr,"Process %i finished normally code=",process);
    WEXITSTATUS(status);
    fprintf(stderr,"%d\n",status);
  }

  if(WIFSIGNALED(status)){
    fprintf(stderr,"Process %i killed by signal : ",process);
    WTERMSIG(status);
    fprintf(stderr,"%d\n",status); 
  }
}

/**
 * Check if a command is internal and execute it if it's the case
 * @param line    *li : command line entered by user
 * @param int     nbCmd : number of command considered  
 * @return        0 if the command is internal, 1 else 
 */
static int checkInternalCommand(struct line *li, int nbCmd, struct array_process *array){
  //Check if command entered is exit
  if(strcmp(li->cmds[nbCmd].args[0],"exit")==0){
    array_destroy(&array_pid);
    array_destroy(array);
    line_reset(li);
    exit(0);
  }

  //Check if the command is cd
  if(strcmp(li->cmds[nbCmd].args[0],"cd")==0){
    if(li->cmds[nbCmd].n_args==2){
      //Using shell-like expansions
      wordexp_t exp;
      wordexp(li->cmds[nbCmd].args[1],&exp,0);
      if(chdir(exp.we_wordv[0])==-1){
        perror("cd");
      }
      wordfree(&exp);
    }
    else if(li->cmds[nbCmd].n_args==1){
      if(chdir(getenv("HOME"))==-1){
        perror("cd");
      }
    }
    else{
      fprintf(stderr,"cd : too many arguments\n");
    }
    return 0;
  }
  return 1;
}

//Handle SIGCHLD's signal behavior
static void handSIGCHLD(int sig){
  pid_t pid;
  int son;
  
  for(int i = 0; i<array_pid.size; ++i){
    pid = waitpid(array_pid.data[i],&son,WNOHANG);

    if(pid>0){
      printStatus(son,pid);
    }

    if(pid != 0){
      array_remove(&array_pid, i);
      return;
    }
  }
  
}



void redirectInput(char *file){
    //Open new input
    int input = open(file, O_RDONLY);
    if(input == -1){
        fprintf(stderr, "Error while opening %s!\n", file);
        perror("open");
        exit(EXIT_FAILURE);
    }

    //duplicate stdin with new input
    int ret = dup2(input, 0);
    if(ret == -1){
        perror("dup2");
        exit(EXIT_FAILURE);
    }

    ret = close(input);
    if(ret != 0){
        perror("close");
        exit(EXIT_FAILURE);
    }
}

void redirectOutput(char *file){
    //Open new output
    int output = open(file, O_CREAT | O_WRONLY | O_TRUNC);
    if(output == -1){
        fprintf(stderr, "Error while opening %s!\n", file);
        perror("open");
        exit(EXIT_FAILURE);
    }

    //duplicate stdout with new output
    int ret = dup2(output, 1);
    if(ret == -1){
        perror("dup2");
        exit(EXIT_FAILURE);
    }

    ret = close(output);
    if(ret != 0){
        perror("close");
        exit(EXIT_FAILURE);
    }
}



int main() {
  //Ignore SIGINT signal
  struct sigaction action_sigint;
  sigemptyset(&action_sigint.sa_mask);
  action_sigint.sa_handler = SIG_IGN;
  action_sigint.sa_flags = 0;
  sigaction(SIGINT, &action_sigint, NULL);

  //Changing SIGCHLD's behavior
  struct sigaction action_sigchld;
  sigemptyset(&action_sigchld.sa_mask);
  action_sigchld.sa_handler = handSIGCHLD;
  action_sigchld.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &action_sigchld, NULL);
  
  //Get the command line
  struct line li;
  char buf[BUFLEN];

  line_init(&li);

  array_create(&array_pid,32);


  for (;;) {
    //Print the current directory to the prompt and asks for a command
    printCurrentDirectory();
    fgets(buf, BUFLEN, stdin);
    
    //Case no command entered --> next iteration of the loop
    

    int err = line_parse(&li, buf);
    if (err) { 
      //the command line entered by the user isn't valid
      line_reset(&li);
      continue;
    }


    /* do something with li */
    if(li.n_cmds==0){
      continue;
    }

    pid_t pid = -1;

    struct array_process array_pid_foreground;
    if(!li.background){
      array_create(&array_pid_foreground,32);
    }

    if(li.n_cmds>1){
      //Check if multiple commands are entered
      int pipes[li.n_cmds-1][2];
      for(int i = 0; i<li.n_cmds; ++i){        
        
        //Check if command is internal
        int check = checkInternalCommand(&li, i, &array_pid_foreground);
        if(!check){
          line_reset(&li);
          continue;
        }

        pipe(pipes[i]);


        //Fork and execution of the command  
        pid=fork();
        //Check if fork has succeed
        if(pid==-1){
          perror("fork\n");
          exit(EXIT_FAILURE);
        } 
        if(pid==0){
            if(i!=0&&i<li.n_cmds-1){
              dup2(pipes[i][1], STDOUT_FILENO);
              close(pipes[i][1]);    
              dup2(pipes[i-1][0], STDIN_FILENO);
              close(pipes[i-1][0]);       
            }
            else{
              if(i==0){
                close(pipes[i][0]);
                if(li.redirect_input){
                  redirectInput(li.file_input);
                }
                else if(!li.redirect_input&&li.background){
                  redirectInput("/dev/null");
                }
                dup2(pipes[i][1], STDOUT_FILENO);
                close(pipes[i][1]);     
              }
              else{
                dup2(pipes[i-1][0], STDIN_FILENO);
                  close(pipes[i-1][0]);
                if(li.redirect_output){
                  int file = open(li.file_output,O_CREAT|O_WRONLY);
                  if(file==-1){
                    perror("open");
                    exit(EXIT_FAILURE);
                  }
                  dup2(pipes[i][1], file);
                  close(pipes[i][1]);
                }
              }
            }
          
          //Resetting SIGINT signal as original
          if(!li.background){
            action_sigint.sa_handler = SIG_DFL;
            sigaction(SIGINT,&action_sigint,NULL);
          }

          execvp(li.cmds[i].args[0],li.cmds[i].args);
          //Execution failed
          fprintf(stderr,"Command entered doesn't exists\n");
          exit(EXIT_FAILURE);
        }

        close(pipes[i][1]);
        if(i!=0){
          close(pipes[i-1][0]);
        }

        if(li.background){
          array_push(&array_pid,pid);
        }
        else{
          array_push(&array_pid_foreground,pid);  
        }
        
      }
    }
    else{
      //Check if command is internal
        int check = checkInternalCommand(&li, 0, &array_pid_foreground);
        if(!check){
          line_reset(&li);
          continue;
        }
        //Fork and execution of the command  
        pid=fork();
        //Check if fork has succeed
        if(pid==-1){
          perror("fork\n");
          exit(EXIT_FAILURE);
        } 
        if(pid==0){
          if(li.redirect_output){
                 redirectOutput(li.file_output);
                }
           if(li.redirect_input){
              redirectInput(li.file_input);
            }
            else if(!li.redirect_input&&li.background){
              redirectInput("/dev/null");
            }
          //Resetting SIGINT signal as original
          if(!li.background){
            action_sigint.sa_handler = SIG_DFL;
            sigaction(SIGINT,&action_sigint,NULL);
          }

          execvp(li.cmds[0].args[0],li.cmds[0].args);
          //Execution failed
          fprintf(stderr,"Command entered doesn't exists\n");
          exit(EXIT_FAILURE);
        }
      if(li.background){
        array_push(&array_pid,pid);
      }
      else{
        array_push(&array_pid_foreground,pid);  
      }
    }

    if(!li.background){
      pid_t pid;
      int son;
      int i = 0;

      while(!array_empty(&array_pid_foreground)){
        pid = waitpid(array_pid_foreground.data[i],&son,0);

        if(pid>0){
          printStatus(son,pid);
        }
        if(pid != 0){
          array_remove(&array_pid_foreground, i);
        }
        if(array_pid_foreground.size==i){
          i=0;
        }
        else{
          i++;
        }
      }  
    }

    array_destroy(&array_pid_foreground);

    line_reset(&li);
  }

  array_destroy(&array_pid);


  return 0;
}