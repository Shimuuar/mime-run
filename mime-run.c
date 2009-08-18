

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Macro for handling failure */
#define FAILIF(x) do{ if(x) { perror("mime-run"); exit(1); } }while(0)
#define CHUNK 4096


/* Determine MIME type of file `filename' using file util.
 * pipe & fork are used instead of popen because I didn't want
 * to escape characters which needs escaping.
 */
void get_mime_type(const char* filename, char* mime, int n)
{
    pid_t pid;
    int pipefd[2], status, i;
    
    /* Create pipe and fork */
    FAILIF( pipe(pipefd)   != 0 );
    FAILIF( (pid = fork()) == -1 );
    
    if( pid == 0 ) {
        /* Connect stdout to write end of pipe */
        FAILIF( dup2(pipefd[1],STDOUT_FILENO) == -1 );
        close(pipefd[1]);
        /* Execute file command */
        execlp("file","--dereference","--brief","--mime-type","--", filename, (char*)NULL);
        perror("mime-type(child)");
        exit(1);
    } else {
        /* Read mime data type */
        FAILIF( read(pipefd[0], mime, n-1) == -1);
        wait(&status);
        if( !WIFEXITED(status) || WEXITSTATUS(status) != 0 ) {
            fprintf(stderr,"mime-run: could not determine mime type.\n");
            exit(1);
        }
        /* Terminate string with zero */
        for(i = 0; i < n && mime[i] != '\n'; i++);
        mime[i] = '\0';
    }
}


/* ================================================================ */


/* String node. */
struct str_node {
    char* str;
    struct str_node* next;
};

/* Data about mime type. */
typedef struct mime_command {
    char*  mime;                /* mime type */
    struct str_node* command;   /* Command to run  */
    struct mime_command* next;  /* Next rule */
} mime_command;


/* ================================================================ */


/* Check if character whitespace (not newline) */
int isws(char c)
{
    return c == ' ' || c == '\t';
}

/* Skip whitespaces */
void parse_skip_ws(char** data)
{
    while( isws(**data) )
        (*data)++;
}

/* Parse word. It may be placed inside quotes.
 * Return malloced word */
char* parse_word(char** data)
{
    char* start = *data;
    if( **data == '"' ) {
        /* This is word inside a quote */
        while( **data != '\0' && **data != '\n' && **data != '"' )
            (*data)++;
    } else if( **data == '\'' ) {
        /* This is word inside a quote */
        while( **data != '\0' && **data != '\n' && **data != '\'' )
            (*data)++;
    } else {
        /* This is unquoted word */
        while( **data != '\0' && **data != '\n' && !isws(**data) )
            (*data)++;
    }
    /* Word must be succeded by whitespace newline or end of string */
    if( !( **data == '\0' || **data == '\n' ||  isws(**data) ) ) {
        fprintf(stderr, "mime-run: unexpected char in config '%c'\n", **data);
        exit(1);
    }
    return strndup(start, *data - start);
}

/* Parse one command */
struct str_node* parse_command(char** data)
{
    struct str_node* node;
    
    parse_skip_ws(data);
    /* End of line reached. */
    if( **data == '\n' || **data == '\0' ) 
        return 0;
    node       = (struct str_node*)malloc( sizeof(struct str_node) );
    node->str  = parse_word(data);
    node->next = parse_command(data);
    return node;
}

struct mime_command* parse_lines(char** data)
{
    mime_command* mime;
    if( **data == '\n' )
        (*data)++;
    if( **data == '\0' )
        return 0;

    mime = (struct mime_command*)malloc( sizeof(struct mime_command) );
    /* Parse config line. */
    parse_skip_ws(data);
    mime->mime = parse_word(data);
    parse_skip_ws(data);
    mime->command = parse_command(data);
    mime->next = parse_lines(data);
    /* Done */
    return mime;
}


struct mime_command* parse_config(const char* fname)
{
    FILE* f;
    char *data, *data_tmp;
    int   n_alloc, total;
    size_t nread;
    struct mime_command* mime_data;
    
    FAILIF( (f = fopen(fname,"r")) == NULL );
    /* Read whole file in memory */
    n_alloc = 0;
    data = (char*)malloc(CHUNK);
    while( (nread = fread(data, 1, CHUNK, f)) == CHUNK ) {
        data = realloc( data, CHUNK * (++n_alloc) );
    }
    total = n_alloc * CHUNK + nread;
    /* Number of allocated bytes always bigger that number of bytes in use */
    data[total] = '\0';

    data_tmp = data;
    mime_data = parse_lines(&data_tmp);
    free(data);
    return mime_data;
}


void run_command(struct str_node* command, char* fname)
{
    struct str_node* node;
    char** cmds;
    int n,i;

    if( command == 0 ) 
        exit(0);

    for(n = 1, node = command; node; node = node->next )
        n++;
    cmds = (char**)calloc(n+1, sizeof(char*));
    /* Fill commands */
    for(i = 0, node = command; node; node = node->next)
        cmds[i++] = node->str;
    cmds[n-1] = fname;
    cmds[n]   = 0;
    /* Run command */
    printf("%i\n",n);
    for(i = 0; i<n; i++ ) {
        printf("'%s'\n",cmds[i]);
    }
    execvp(cmds[0], cmds);
    fprintf(stderr, "mime-run: Failed to execute command");
    exit(1);        
}


int main(int argc, char** argv)
{
    char mime[128];
    struct mime_command* mime_data;

    if( argc == 1 ) {
        fprintf(stderr, "mime-run: Filename is not given.");
        return 1;
    }
    /* Parse config */
    mime_data = parse_config("config");
    /* Determine mime type of file */
    get_mime_type(argv[1], mime, 128);
    for(; mime_data; mime_data = mime_data->next ) {
        printf("'%s' - '%s'\n", mime, mime_data->mime);
        if( strcmp(mime, mime_data->mime) == 0 )
            run_command(mime_data->command, argv[1]);
    }
    fprintf(stderr, "mime-run: No appropriate mime rule found\n");
    return 1;
}
