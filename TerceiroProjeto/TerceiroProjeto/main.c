#define _GNU_SOURCE//habilita funções adicionais específicas do GNU
#include <stdio.h> //printf, fgets, perror
#include <unistd.h>//fork, exec, chdir, pipe
#include <sys/wait.h>//waitpid
#include <stdlib.h>// malloc, free, exit
#include <string.h>// strtok, strcmp, strdup
#include <sys/types.h>// tipos pid_t etc.
#include <fcntl.h>// open, O_CREAT, O_RDONLY
#include <errno.h>// errno para mensagens de erro
#include <signal.h>// sinais, sigaction
#define MAX_LINHA 1024//tamanho máximo da linha digitada pelo usuário
#define MAX_ARGS 128//máximo de argumentos por comando
#define MAX_CMDS 32//máximo de comandos num pipeline (cmd1 | cmd2 | cmd3 ...)

typedef struct{
    char *argv[MAX_ARGS];//lista de argumentos terminada em NULL
    char *entrada;//arquivo para redirecionamento de entrada (stdin)
    char *saida;//arquivo para redirecionamento de saída (stdout)
    int append;//se 1 → >> (append). Se 0 → > (sobrescreve)
} comando_simples;

//Handler para SIGCHLD. É chamado automaticamente quando um processo filho termina. Serve para evitar processos zumbis.
static void trata_sigchld(int sig){
    (void)sig;  //evita warning por parâmetro não usado
    while (waitpid(-1, NULL, WNOHANG) > 0) {}//waitpid com WNOHANG retira zumbis sem bloquear
}

//implementação do comando interno "cd"
static void shell_cd(char **args){
    if(args[1] == NULL) {//cd sem argumento → vai para HOME
        char *home = getenv("HOME");
        if(home == NULL) home = "/";//fallback para raiz
        if(chdir(home) != 0)
            perror("myshell: cd");
    } else{
        //cd para diretório especificado
        if(chdir(args[1]) != 0)
            perror("myshell: cd");
    }
}

//remove espaços no início/fim de uma string. Evita problemas com comandos digitados com espaços extras.
static char *trim(char *s){
    if (!s) return NULL;
    //anda até o primeiro caractere não-espaço
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    if(*s == 0) return s;//string só com espaços
    //remove espaços do final
    char *fim = s + strlen(s) - 1;
    while(fim > s && (*fim == ' ' || *fim == '\t' || *fim == '\n')){
        *fim = '\0';
        fim--;
    }
    return s;
}

//FUNÇÃO PRINCIPAL DE PARSE:
static int parse_linha(char *linha, comando_simples cmds[], int *background){
    int cmd_count = 0;//quantos comandos já formados
    int arg_idx = 0;//índice no argv de cada comando
    char *token, *saveptr;

    *background = 0;// assume não-background

    //inicializa todos os comandos com NULL
    for(int i = 0; i < MAX_CMDS; i++){
        cmds[i].entrada = NULL;
        cmds[i].saida = NULL;
        cmds[i].append = 0;
        for (int j = 0; j < MAX_ARGS; j++)
            cmds[i].argv[j] = NULL;
    }

    //tokeniza a linha em pedaços separados por espaço/tab/newline
    token = strtok_r(linha, " \t\n", &saveptr);
    while(token != NULL) {
        //pipE
        if(strcmp(token, "|") == 0){
            if (arg_idx == 0) {  //pipe sem comando antes da erro
                fprintf(stderr, "myshell: erro de sintaxe perto de '|'.\n");
                return -1;
            }
            cmds[cmd_count].argv[arg_idx] = NULL;//fecha argv
            cmd_count++;//próximo comando
            arg_idx = 0;//reinicia argumentos
        }

        //redirecionemento de entrada
        else if(strcmp(token, "<") == 0){
            token = strtok_r(NULL, " \t\n", &saveptr);
            if (!token) {
                fprintf(stderr, "myshell: falta arquivo após '<'.\n");
                return -1;
            }
            cmds[cmd_count].entrada = strdup(token);
        }

        //redirecionemento de saida
        else if(strcmp(token, ">") == 0){
            token = strtok_r(NULL, " \t\n", &saveptr);
            if (!token) {
                fprintf(stderr, "myshell: falta arquivo após '>'.\n");
                return -1;
            }
            cmds[cmd_count].saida = strdup(token);
            cmds[cmd_count].append = 0; //modo sobrescrita
        }

        //redirecionemento de append
        else if(strcmp(token, ">>") == 0){
            token = strtok_r(NULL, " \t\n", &saveptr);
            if (!token) {
                fprintf(stderr, "myshell: falta arquivo após '>>'.\n");
                return -1;
            }
            cmds[cmd_count].saida = strdup(token);
            cmds[cmd_count].append = 1; //modo append
        }

        //backgroynd
        else if(strcmp(token, "&") == 0){
            char *peek = strtok_r(NULL, " \t\n", &saveptr);
            if(peek != NULL){
                fprintf(stderr, "myshell: '&' deve estar no fim.\n");
                return -1;
            }
            *background = 1;
            break;
        }

        //argumento
        else{
            cmds[cmd_count].argv[arg_idx++] = strdup(token);
        }

        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    //se terminou com argumentos pendentes fecha comando
    if(arg_idx > 0){
        cmds[cmd_count].argv[arg_idx] = NULL;
        cmd_count++;
    }

    return cmd_count;
}

//função que executa comandos. suporta pipelines, redirecionamento, background e execvp.

static int executa_pipeline(comando_simples cmds[], int ncmds, int background){
    int in_fd = -1;//entrada do comando atual (pipe anterior)
    int pipefd[2];//descritores de pipe
    pid_t pids[MAX_CMDS];

    for(int i = 0; i < ncmds; i++){

        //se nao for o último comando, cria pipe
        if(i < ncmds - 1){
            if(pipe(pipefd) < 0){
                perror("pipe");
                return -1;
            }
        }else{
            pipefd[0] = pipefd[1] = -1;
        }

        pid_t pid = fork(); //cria processo filho

        if(pid < 0){  //erro
            perror("fork");
            return -1;
        }

        //filho
        else if(pid == 0){
            //se há entrada anterior, conecta ao stdin
            if(in_fd != -1)
                dup2(in_fd, STDIN_FILENO);
            //se não é último comando, direciona saída ao pipe
            if(i < ncmds - 1)
                dup2(pipefd[1], STDOUT_FILENO);
            //redirecionamento de entrada(< arquivo)
            if(cmds[i].entrada){
                int fd = open(cmds[i].entrada, O_RDONLY);
                if(fd < 0){
                    fprintf(stderr, "myshell: erro abrindo '%s': %s\n", cmds[i].entrada, strerror(errno));
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            //redirecionamento de saída (> e >>)
            if(cmds[i].saida){
                int fd;
                if(cmds[i].append)
                    fd = open(cmds[i].saida, O_WRONLY | O_CREAT | O_APPEND, 0644);
                else
                    fd = open(cmds[i].saida, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                if(fd < 0) {
                    fprintf(stderr, "myshell: erro abrindo '%s': %s\n", cmds[i].saida, strerror(errno));
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            //fecha descritores não usados
            if(pipefd[0] != -1) close(pipefd[0]);
            if(pipefd[1] != -1) close(pipefd[1]);
            if(in_fd != -1) close(in_fd);
            //executa comando
            execvp(cmds[i].argv[0], cmds[i].argv);
            //só chega aqui se der erro
            fprintf(stderr, "myshell: erro executando '%s': %s\n",
                    cmds[i].argv[0], strerror(errno));
            exit(1);
        }

        //pai
        else{
            pids[i] = pid;  // guarda PID

            //fecha descritores de escrita do pipe
            if(pipefd[1] != -1)
                close(pipefd[1]);

            //fecha entrada anterior
            if(in_fd != -1)
                close(in_fd);

            //atualiza entrada para próximo comando
            in_fd = pipefd[0];
        }
    }
    //se nao for background, espera processos terminarem
    if(!background){
        for (int i = 0; i < ncmds; i++)
            waitpid(pids[i], NULL, 0);
    }else{
        printf("[rodando em background]\n");
    }

    return 0;
}

//libera memória de argv, entrada/saida etc.
static void libera_cmds(comando_simples cmds[], int ncmds){
    for (int i = 0; i < ncmds; i++) {
        if (cmds[i].entrada) free(cmds[i].entrada);
        if (cmds[i].saida) free(cmds[i].saida);

        for (int j = 0; cmds[i].argv[j] != NULL; j++)
            free(cmds[i].argv[j]);
    }
}

//função principal
int main(){
    char linha[MAX_LINHA];// buffer da linha digitada
    comando_simples cmds[MAX_CMDS];//lista de comandos parseados

    //configura handler de SIGCHLD
    struct sigaction sa;
    sa.sa_handler = trata_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    //loop pruncipal
    while (1) {
        printf("myshell> ");//mostra prompt
        fflush(stdout);

        if (!fgets(linha, sizeof(linha), stdin)) {
            printf("\n");
            break;//usuário apertou Ctrl+D
        }

        char *cmdline = trim(linha);//remove espaços extras
        if (cmdline[0] == '\0') continue;//linha vazia → ignora

        int background = 0;
        int ncmds = parse_linha(cmdline, cmds, &background);

        if(ncmds <= 0)continue;

        // Comandos internos tratados sem fork:
        if(ncmds == 1 && !background){
            //comando exit
            if (strcmp(cmds[0].argv[0], "exit") == 0){
                libera_cmds(cmds, ncmds);
                printf("Saindo da shell...\n");
                break;
            }
            //comando cd
            if (strcmp(cmds[0].argv[0], "cd") == 0){
                shell_cd(cmds[0].argv);
                libera_cmds(cmds, ncmds);
                continue;
            }
        }
        //demais comandos executados normalmente
        executa_pipeline(cmds, ncmds, background);
        libera_cmds(cmds, ncmds);
    }
    return 0; //fim
}
