# ATIVIDADE-LINUX

BRUNO SCUTARE BRANCO 04723086
GUSTAVO BONANI 04723012
GUILHERME RIZO 04723092


MyShell
O MyShell é um mini-shell desenvolvido do zero para fins educacionais. Ele replica funcionalidades básicas presentes em shells como bash, possibilitando entender na prática:
Criação de processos (fork)
Substituição de imagem de processo 
Comunicação entre processos 
Manipulação de descritores de arquivo 
Reaproveitamento de processos em background 
Parsing manual da linha de comando

O projeto foi desenvolvido em dois módulos que a professora pediu:
Módulo 1 
Loop principal com prompt

Execução de comandos externos

Comandos internos como cd e exit

Módulo 2
Redirecionamento de entrada e saída: >, <, >>
Suporte a pipes (múltiplos)
Execução em background com &


Uso Básico
Inicie o shell:
./myshell
O prompt aparecerá:
myshell>
Digite comandos como faria em qualquer shell.

Funcionalidades Suportadas
Execução de Programas Externos
Exemplos:
myshell> ls
myshell> ps aux
myshell> echo hello world
Comandos Internos
cd [caminho]
myshell> cd /usr/bin
myshell> cd
myshell> exit

Redirecionamento
Saída padrão (>)
myshell> echo oi > teste.txt
Append (>>)
myshell> echo linha >> teste.txt
Entrada padrão (<)
myshell> wc -l < teste.txt

Pipes (|)
Suporta múltiplos pipes:
myshell> ls -l | grep myshell
myshell> cat arquivo | tr a-z A-Z | sort

Execução em Background (&)
myshell> sleep 10 &
myshell>
O shell não espera o processo e retorna ao prompt.
Processos são reaproveitados automaticamente via SIGCHLD.

Arquitetura do Código
A implementação é dividida em etapas claras:
leitura e limpeza da linha
uso de fgets(), remoção de \n e trim().


Parsing Manual

A função:
parse_line()

Divide a linha em:
    comandos separados por |
    seus argumentos argv[]
    arquivos de redirecionamento
    modo append ou truncate
    execução background
    número de comandos na pipeline

Resulta em um array:
cmds[0], cmds[1], ..., cmds[n]

Cada comando contém:
argv[]
infile
outfile
append


Execução Sequencial com Pipes

A função:
execute_pipeline()
cria para cada comando:
    Pipe (se necessário)
    Fork
    Filho:
        dup2 para stdin/stdout apropriados
        aplica <, >, >>
        execvp()
    Pai:
        fecha pipes
        guarda PID para waitpid()

Background
Se a linha termina com &:
    o pai não chama waitpid()
    imprime aviso
    retorna ao prompt

TESTES
TESTE  — Comandos Simples
myshell> ls
myshell> pwd
myshell> echo oi
TESTE — Comando interno cd
myshell> cd /
myshell> pwd
TESTE — Redirecionamentos
myshell> echo teste > arq.txt
myshell> cat < arq.txt
myshell> echo X >> arq.txt
TESTE — Pipes
myshell> ls | wc -l
myshell> cat arq.txt | grep X | wc -l
TESTE — Background
myshell> sleep 5 &
myshell> ls
TESTE  — Comando exit
myshell> exit
