#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <malloc.h>
#include <time.h>
#include "templ_script.h"

#define UNUSED_ARG(X) (void*)X

static char* strdup(const char* s)
{
	char *r = (char*)malloc(strlen(s)+1);
	strcpy(r, s);
	return r;
}




typedef struct ostream_t ostream_t;
struct ostream_t {
  ts_bool (*open)(ostream_t *ostream, const char *name);
  void (*close)(ostream_t *ostream);
  void (*put)(ostream_t *ostream, char ch);
};

static ts_bool ostream_open(ostream_t *ostream, const char *name)
{
  return ostream->open != 0 && ostream->open(ostream, name);
}

static void ostream_close(ostream_t *ostream)
{
  if (ostream->close != 0) {
    ostream->close(ostream);
  }
}

static void ostream_put(ostream_t *ostream, char ch)
{
  if (ostream->put != 0) {
    ostream->put(ostream, ch);
  }
}

static void ostream_puts(ostream_t *ostream, const char *str)
{
  if (ostream->put != 0) {
    while (*str != '\0') {
      ostream->put(ostream, *str++);
    }
  }
}

/* file output stream */ 

typedef struct {
  ostream_t ostream;
  FILE *f;
} ostream_to_files_t;
 
static ts_bool file_ostream_open(ostream_t *ostream, const char *name) 
{ 
  return (((ostream_to_files_t*)ostream)->f = fopen(name, "wt")) != 0; 
} 
 
static void file_ostream_close(ostream_t *ostream) 
{ 
  fclose(((ostream_to_files_t*)ostream)->f);
  ((ostream_to_files_t*)ostream)->f = NULL; 
} 
 
static void file_ostream_put(ostream_t *ostream, char ch) 
{ 
  if (((ostream_to_files_t*)ostream)->f != NULL) {
    fputc(ch, ((ostream_to_files_t*)ostream)->f);
  }
} 
 
static void init_ostream_to_files(ostream_to_files_t *ostream) 
{ 
  ostream->ostream.open = file_ostream_open; 
  ostream->ostream.close = file_ostream_close; 
  ostream->ostream.put = file_ostream_put;
  ostream->f = NULL;
}


/* String appender */

#define STRING_APPENDER_BUF_TEXT_SIZE 100
typedef struct string_appender_buf_t string_appender_buf_t;
struct string_appender_buf_t
{
  char text[STRING_APPENDER_BUF_TEXT_SIZE];
  string_appender_buf_t *next;
};

typedef struct {
  string_appender_buf_t *buf;
  string_appender_buf_t **ref_buf;
  unsigned int pos;
  size_t size;
} string_appender_t;

static void string_appender_init(string_appender_t *appender)
{
  appender->buf = NULL;
  appender->ref_buf = &appender->buf;
  appender->pos = 0;
  appender->size = 0;
}

static void string_appender_start(string_appender_t *appender)
{
  appender->ref_buf = &appender->buf;
  appender->pos = 0;
  appender->size = 0;
}

static void string_appender_add(string_appender_t *appender, char ch)
{
  if (*appender->ref_buf == NULL) {
    *appender->ref_buf = (string_appender_buf_t*)malloc(sizeof(string_appender_buf_t));
    if (*appender->ref_buf == NULL) {
      return; /* malloc error */
    }
    (*appender->ref_buf)->text[0] = ch;
    (*appender->ref_buf)->next = 0;
    appender->pos = 1;
  }
  else {
    (*appender->ref_buf)->text[appender->pos++] = ch;
    if (appender->pos == STRING_APPENDER_BUF_TEXT_SIZE) {
      appender->ref_buf = &(*appender->ref_buf)->next;
      appender->pos = 0;
    }
  }
  appender->size++;
}

static char *string_appender_result(string_appender_t *appender)
{
  char *result = (char*)malloc(sizeof(char)*(appender->size + 1));
  string_appender_buf_t *buf = appender->buf;
  unsigned int pos = 0;
  for (size_t i = 0; i < appender->size; i++) {
    result[i] = buf->text[pos];
    if (++pos == STRING_APPENDER_BUF_TEXT_SIZE) {
      buf = buf->next;
      pos = 0;
    }
  }
  result[appender->size] = '\0';
  appender->ref_buf = &appender->buf;
  appender->pos = 0;
  appender->size = 0;
  return result;
}

static int string_appender_compare(string_appender_t *appender, const char *s)
{
  string_appender_buf_t *buf = appender->buf;
  unsigned int pos = 0;
  for (size_t i = 0; i < appender->size; i++) {
    int cmp = buf->text[pos] - s[i];
    if (cmp != 0) {
      return cmp;
    }
    if (++pos == STRING_APPENDER_BUF_TEXT_SIZE) {
      buf = buf->next;
      pos = 0;
    }
  }
  return '\0' - s[appender->size];
}

static void string_appender_free(string_appender_t *appender)
{
  for (string_appender_buf_t *buf = appender->buf; buf != NULL;) {
    string_appender_buf_t *next = buf->next;
    free((void*)buf);
    buf = next;
  }
}


/* Scanner */

typedef struct {
  unsigned long line;
  unsigned long column;
} file_pos_t;

typedef struct {
  FILE* file;
  char symbol;
  char *text;
  char cur_ch;
  file_pos_t start;
  file_pos_t end;
  file_pos_t cur;
  string_appender_t string_appender;
} scanner_t;

#define SCANNER_SYMBOL_EOF     '\0'
#define SCANNER_SYMBOL_ERROR   'A'
#define SCANNER_SYMBOL_IDENT   'B'
#define SCANNER_SYMBOL_NUMBER  'C'
#define SCANNER_SYMBOL_STRING  '\"'
#define SCANNER_SYMBOL_IF      'D'
#define SCANNER_SYMBOL_ELSE    'E'
#define SCANNER_SYMBOL_WITH    'F'
#define SCANNER_SYMBOL_FOR     'G'
#define SCANNER_SYMBOL_IN      'H'
#define SCANNER_SYMBOL_SWITCH  'I'
#define SCANNER_SYMBOL_CASE    'J'
#define SCANNER_SYMBOL_DEFAULT 'K'
#define SCANNER_SYMBOL_AND     'L'
#define SCANNER_SYMBOL_OR      'M'
#define SCANNER_SYMBOL_EQUAL   'N'
#define SCANNER_SYMBOL_TRUE    'O'
#define SCANNER_SYMBOL_FALSE   'P'

struct {
  const char *text;
  char symbol;
} keywords[] = {
  { "if",      SCANNER_SYMBOL_IF },
  { "else",    SCANNER_SYMBOL_ELSE },
  { "with",    SCANNER_SYMBOL_WITH },
  { "for",     SCANNER_SYMBOL_FOR },
  { "in",      SCANNER_SYMBOL_IN },
  { "switch",  SCANNER_SYMBOL_SWITCH },
  { "case",    SCANNER_SYMBOL_CASE },
  { "default", SCANNER_SYMBOL_DEFAULT },
  { "true",    SCANNER_SYMBOL_TRUE },
  { "false",   SCANNER_SYMBOL_FALSE },
};

static void scanner_next_char(scanner_t *scanner)
{
  scanner->cur_ch = (char)fgetc(scanner->file);
  if (feof(scanner->file)) {
    scanner->cur_ch = '\0';
  }
  scanner->cur.column++;
}

static void scanner_skip_white_space(scanner_t *scanner)
{
  for (;; scanner_next_char(scanner)) {
    if (scanner->cur_ch == '\t') {
      scanner->cur.column = ((scanner->cur.column + 3)/4)*4;
    }
    else if (scanner->cur_ch == '\n') {
      scanner->cur.column = 0;
      scanner->cur.line++;
    }
    else if (scanner->cur_ch != ' ' && scanner->cur_ch != '\r') {
      break;
    }
  }
}

static void scanner_print_state(scanner_t *scanner)
{
  (void)scanner;/*
  printf("%lu.%lu: ", scanner->cur.line, scanner->cur.column);
  if (scanner->symbol == SCANNER_SYMBOL_EOF)
    printf("eof\n");
  else {
    printf("%c", scanner->symbol);
    if (scanner->text != NULL)
      printf(" |%s|", scanner->text);
    printf("\n");
  }*/
}

static void scanner_next_token(scanner_t *scanner)
{
  if (scanner->text != NULL) {
    free((void*)scanner->text);
    scanner->text = NULL;
  }
  for (;;) {
    scanner_skip_white_space(scanner);
    if (scanner->cur_ch != '/') {
      break;
    }
    scanner_next_char(scanner);
    if (scanner->cur_ch != '*') {
      scanner->symbol = '/';
      return;
    }
    scanner_next_char(scanner);
    for (;;) {
      if (scanner->cur_ch == '\t') {
        scanner->cur.column = ((scanner->cur.column + 3)/4)*4;
        scanner_next_char(scanner);
      }
      else if (scanner->cur_ch == '\n') {
        scanner->cur.column = 0;
        scanner->cur.line++;
        scanner_next_char(scanner);
      }
      else if (scanner->cur_ch == '*') {
        scanner_next_char(scanner);
        if (scanner->cur_ch == '/') {
          scanner_next_char(scanner);
          break;
        }
      }
      else {
        scanner_next_char(scanner);
      }
    }
  }
  if (scanner->cur_ch == '\0') {
    scanner->symbol = SCANNER_SYMBOL_EOF;
    scanner_print_state(scanner);
    return;
  }
  scanner->start = scanner->cur;
  string_appender_start(&scanner->string_appender);
  if (   ('a' <= scanner->cur_ch && scanner->cur_ch <= 'z')
      || ('A' <= scanner->cur_ch && scanner->cur_ch <= 'Z')) {
    string_appender_add(&scanner->string_appender, scanner->cur_ch);
    scanner_next_char(scanner);
    while (   ('a' <= scanner->cur_ch && scanner->cur_ch <= 'z')
           || ('A' <= scanner->cur_ch && scanner->cur_ch <= 'Z')
           || ('0' <= scanner->cur_ch && scanner->cur_ch <= '9')
       || scanner->cur_ch == '_') {
      string_appender_add(&scanner->string_appender, scanner->cur_ch);
      scanner_next_char(scanner);
    }
    scanner->end = scanner->cur;
    /* compare with keywords */
    for (size_t i = 0; i < sizeof(keywords)/sizeof(*keywords); i++) {
      if (string_appender_compare(&scanner->string_appender, keywords[i].text) == 0) {
        scanner->symbol = keywords[i].symbol;
        scanner_print_state(scanner);
        return;
      }
    }
    scanner->text = string_appender_result(&scanner->string_appender);
    scanner->symbol = SCANNER_SYMBOL_IDENT;
  }
  else if ('0' <= scanner->cur_ch && scanner->cur_ch <= '9')
  {
    string_appender_add(&scanner->string_appender, scanner->cur_ch);
    scanner_next_char(scanner);
    while ('0' <= scanner->cur_ch && scanner->cur_ch <= '9') {
      string_appender_add(&scanner->string_appender, scanner->cur_ch);
      scanner_next_char(scanner);
    }
    scanner->end = scanner->cur;
    scanner->text = string_appender_result(&scanner->string_appender);
    scanner->symbol = SCANNER_SYMBOL_NUMBER;
  }
  else if (scanner->cur_ch == '\"') {
    while (scanner->cur_ch == '\"') {
      scanner_next_char(scanner);
      for (; scanner->cur_ch != '\0' && scanner->cur_ch != '\"'; scanner_next_char(scanner)) {
        if (scanner->cur_ch == '\\') {
          scanner_next_char(scanner);
          if (scanner->cur_ch == 'n') {
            string_appender_add(&scanner->string_appender, '\n');
          }
          else if (scanner->cur_ch == 't') {
            string_appender_add(&scanner->string_appender, '\t');
          }
          else if (scanner->cur_ch == '\"') {
            string_appender_add(&scanner->string_appender, '\"');
          }
          else {
	    string_appender_add(&scanner->string_appender, '\\');
            string_appender_add(&scanner->string_appender, scanner->cur_ch);
          }
        }
        else {
          string_appender_add(&scanner->string_appender, scanner->cur_ch);
        }
      }
      if (scanner->cur_ch == '\0') {
        scanner->symbol = SCANNER_SYMBOL_ERROR;
        scanner_print_state(scanner);
        return;
      }
      scanner_next_char(scanner);
      scanner->end = scanner->cur;
      scanner_skip_white_space(scanner);
    }
    scanner->text = string_appender_result(&scanner->string_appender);
    scanner->symbol = SCANNER_SYMBOL_STRING;
  }
  else if (scanner->cur_ch == '|') {
    scanner_next_char(scanner);
    if (scanner->cur_ch == '|') {
      scanner_next_char(scanner);
      scanner->symbol = SCANNER_SYMBOL_OR;
    }
    else {
      scanner->symbol = SCANNER_SYMBOL_ERROR;
    }
    scanner->end = scanner->cur;
  }
  else if (scanner->cur_ch == '&') {
    scanner_next_char(scanner);
    if (scanner->cur_ch == '&') {
      scanner_next_char(scanner);
      scanner->symbol = SCANNER_SYMBOL_AND;
    }
    else {
      scanner->symbol = SCANNER_SYMBOL_ERROR;
    }
    scanner->end = scanner->cur;
  }
  else if (scanner->cur_ch == '=') {
    scanner->symbol = scanner->cur_ch;
    scanner_next_char(scanner);
    if (scanner->cur_ch == '=') {
      scanner->symbol = SCANNER_SYMBOL_EQUAL;
      scanner_next_char(scanner);
    }
    scanner->end = scanner->cur;
  }
  else {
    scanner->symbol = scanner->cur_ch;
    scanner_next_char(scanner);
    scanner->end = scanner->cur;
  }
  scanner_print_state(scanner);
}

static void scanner_init(scanner_t *scanner, FILE *file)
{
  scanner->file = file;
  scanner->symbol = SCANNER_SYMBOL_ERROR;
  scanner->text = NULL;
  scanner->cur_ch = (char)fgetc(file);
  scanner->cur.line = 1;
  scanner->cur.column = 0;
  string_appender_init(&scanner->string_appender);
  scanner_next_token(scanner);
}

static void scanner_fini(scanner_t *scanner)
{
  string_appender_free(&scanner->string_appender);
}

/* Expressions */

typedef struct proc_definition_t proc_definition_t;

typedef ts_bool (*ts_eval_func_p)(ts_expr_t *expr, ts_scope_t *scope, ts_exec_context_t *context, ts_value_t *result);

#define ENTER_EVAL_FUNC(N) \
  call_stack_entry_t call_stack_entry;\
  exec_context_enter(context, N, &call_stack_entry, scope, expr, result);
#define LEAVE_EVAL_FUNC exec_context_leave(context, &call_stack_entry, result);
#define EVAL_FUNC_ERROR(X) \
  exec_context_break(context); \
  fprintf(stdout, "Error: %s\n", X); \
  exec_context_debug(context, &call_stack_entry);
#define EVAL_FUNC_ERROR_VALUE(X,V) \
  exec_context_break(context); \
  fprintf(stdout, "Error: %s: ", X); value_print(V, stdout); fprintf(stdout, "\n"); \
  exec_context_debug(context, &call_stack_entry);

struct ts_expr_t {
  ts_eval_func_p eval;
  const char *text;
  unsigned long long number;
  char ch;
  TS_TREE_TYPE *node;
  proc_definition_t *proc_def;
  ts_expr_t *children;
  ts_expr_t *next;
  file_pos_t pos;
};

static void value_init(ts_value_t *value)
{
  value->type = ts_value_type_none;
  value->text = NULL;
  value->number = 0;
  value->node = NULL;
  value->scope = NULL;
  value->expr = NULL;
}

static ts_bool value_valid(ts_value_t *value)
{
  switch (value->type) {
    case ts_value_type_none:
    case ts_value_type_bool:
    case ts_value_type_int:
    case ts_value_type_text:
    case ts_value_type_node:
    case ts_value_type_expr:
      return ts_true;
  }
  return ts_false;
}

static int value_comp(ts_value_t *lhs, ts_value_t *rhs)
{
  if (lhs->type < rhs->type) return -1;
  if (lhs->type > rhs->type) return 1;
  switch (lhs->type) {
    case ts_value_type_none: return 0;
    case ts_value_type_bool: return (rhs->number != 0 ? 1 : 0) - (lhs->number != 0 ? 1 : 0);
    case ts_value_type_int:
      if (lhs->number < rhs->number) return -1;
      if (lhs->number > rhs->number) return 1;
      break;
    case ts_value_type_text: return strcmp(lhs->text, rhs->text);
    case ts_value_type_node:
      if (lhs->node < rhs->node) return -1;
      if (lhs->node > rhs->node) return 1;
      break;
    case ts_value_type_expr:
      break; // should not compare
    default:
      assert(0);
      break;
  }
  return 0;
}

ts_bool value_is_true(ts_value_t *value)
{
  switch(value->type) {
    case ts_value_type_none: return ts_false;
    case ts_value_type_bool: return value->number != 0;
    case ts_value_type_int: return value->number != 0;
    case ts_value_type_text: return value->text != 0 && value->text[0] != '\0';
    case ts_value_type_node: return value->node != 0;
    case ts_value_type_expr:
      break; // should not occur
    default:
      assert(0);
      break;
  }
  return ts_false;
}

const char *node_type(TS_TREE_TYPE *node)
{
  return "<other>";
}

void value_print(ts_value_t *value, FILE *f)
{
  switch(value->type) {
    case ts_value_type_none: fprintf(f, "none"); break;
    case ts_value_type_bool: fprintf(f, "bool %llu", value->number); break;
    case ts_value_type_int: fprintf(f, "int %llu", value->number); break;
    case ts_value_type_text: fprintf(f, "text '%s'", value->text); break;
    case ts_value_type_node:
      fprintf(f, "%s node", node_type(value->node));
      break;
    case ts_value_type_expr: fprintf(f, "expr at %lu.%lu", value->expr->pos.line, value->expr->pos.column); break;
    default: fprintf(f, "unknown"); break;
  }
}

struct ts_scope_t {
  const char *var;
  ts_value_t value;
  ts_scope_t *prev;
};

EVAL_FUNC(eval_nop);

static void init_expr(ts_expr_t *expr)
{
  expr->eval = eval_nop;
  expr->text = NULL;
  expr->number = 0ULL;
  expr->ch = '\0';
  expr->node = NULL;
  expr->proc_def = NULL;
  expr->children = NULL;
  expr->next = NULL;
  expr->pos.line = 0;
  expr->pos.column = 0;
}

static ts_expr_t *create_expr()
{
  ts_expr_t *expr = (ts_expr_t*)malloc(sizeof(ts_expr_t));
  if (expr == NULL) {
    return NULL;
  }
  init_expr(expr);
  return expr;
}

static void free_expr(ts_expr_t *expr)
{
  while (expr != NULL) {
    ts_expr_t *next = expr->next;
    free_expr(expr->children);
    free((void*)expr->text);
    free((void*)expr);
    expr = next;
  }
}

typedef struct proc_parameter_t proc_parameter_t;
struct proc_parameter_t {
  const char *name;
  proc_parameter_t *next;
};
struct proc_definition_t {
  const char *name;
  ts_eval_func_p func;
  ts_eval_func_p method;
  ts_eval_func_p field;
  proc_parameter_t *proc_params;
  ts_expr_t *proc_body;
  ts_bool proc_called;
  proc_definition_t *next;
};

static void proc_definition_add_param(proc_definition_t *proc_def, const char *name)
{
  proc_parameter_t **ref_param = &proc_def->proc_params;
  while (*ref_param != NULL)
    ref_param = &(*ref_param)->next;
  *ref_param = (proc_parameter_t*)malloc(sizeof(proc_parameter_t));
  if (*ref_param == NULL) {
    return;
  }
  (*ref_param)->name = name;
  (*ref_param)->next = NULL;
}


/* Parser */

struct ts_executor_t {
  scanner_t scanner;
  proc_definition_t *proc_defs;
  ts_bool error;
};

static void parser_error(ts_executor_t *executor, const char *message)
{
  printf("%lu.%lu: Error: %s\n", executor->scanner.start.line, executor->scanner.start.column, message);
  executor->error = ts_true;
}

static void parser_error_ident(ts_executor_t *executor, const char *message, const char *ident)
{
  printf("%lu.%lu: Error: %s '%s'\n", executor->scanner.start.line, executor->scanner.start.column, message, ident);
  executor->error = ts_true;
}

static char *parser_claim_text(ts_executor_t *executor)
{
  char *text = executor->scanner.text;
  executor->scanner.text = NULL;
  return text;
}

static ts_bool parser_expect_sym(ts_executor_t *executor, char symbol, char *descr)
{
  if (executor->scanner.symbol != symbol) {
    parser_error(executor, descr);
    return ts_false;
  }
  scanner_next_token(&executor->scanner);
  return ts_true;
}

static ts_bool parser_accept_sym(ts_executor_t *executor, char symbol)
{
  if (executor->scanner.symbol != symbol) {
    return ts_false;
  }
  scanner_next_token(&executor->scanner);
  return ts_true;
}

static proc_definition_t *executor_add_proc(ts_executor_t *executor, const char *name)
{
  proc_definition_t **ref_proc = &executor->proc_defs;
  while (*ref_proc != NULL && strcmp((*ref_proc)->name, name) < 0)
    ref_proc = &(*ref_proc)->next;
  if (*ref_proc != NULL && strcmp((*ref_proc)->name, name) == 0) {
    return *ref_proc;
  }
  proc_definition_t *new_proc = (proc_definition_t*)malloc(sizeof(proc_definition_t));
  if (new_proc == NULL) {
    return NULL;
  }
  new_proc->name = strdup(name);
  if (new_proc->name == NULL) {
    free((void*)new_proc);
    return NULL;
  }
  new_proc->func = 0;
  new_proc->method = 0;
  new_proc->field = 0;
  new_proc->proc_params = NULL;
  new_proc->proc_body = NULL;
  new_proc->proc_called = ts_false;
  new_proc->next = *ref_proc;
  *ref_proc = new_proc;
  return new_proc;
}

/* Parsing functions */

static ts_bool parse_or_expr(ts_executor_t *executor, ts_expr_t **ref_expr);
static ts_bool parse_statements(ts_executor_t *executor, ts_expr_t **ref_expr);

EVAL_FUNC(eval_proc_call);
EVAL_FUNC(eval_var);
EVAL_FUNC(eval_number_const);
EVAL_FUNC(eval_string_const);
EVAL_FUNC(eval_bool_const);

static ts_bool parse_term(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  if (parser_accept_sym(executor, '(')) {
    return    parse_or_expr(executor, ref_expr)
           && parser_expect_sym(executor, ')', "expect ')'");
  }
  else if (executor->scanner.symbol == SCANNER_SYMBOL_IDENT) {
    ts_expr_t *expr = NULL;
    for (;;) {
      ts_expr_t *new_expr = create_expr();
      if (new_expr == NULL) {
        free_expr(expr);
        return ts_false;
      }
      new_expr->pos = executor->scanner.start;
      char *name = parser_claim_text(executor);
      scanner_next_token(&executor->scanner);
      if (parser_accept_sym(executor, '(')) {
        proc_definition_t *proc_def = executor_add_proc(executor, name);
        if (proc_def == NULL) {
          free_expr(new_expr);
          free_expr(expr);
          return ts_false;
        }
        ts_expr_t **ref_next_param;
        if (expr == NULL) {
          if (proc_def->func != 0) {
            new_expr->eval = proc_def->func;
          }
          else {
            new_expr->eval = eval_proc_call;
            new_expr->proc_def = proc_def;
            proc_def->proc_called = ts_true;
          }
          ref_next_param = &new_expr->children;
        }
        else {
          if (proc_def->method != 0) {
            new_expr->eval = proc_def->method;
          }
          else {
            parser_error_ident(executor, "unknown method", name);
          }
          new_expr->children = expr;
          ref_next_param = &expr->next;
        }
        for (;;) {
          if (parser_accept_sym(executor, '{')) {
            parse_statements(executor, ref_next_param);
            if (!parser_expect_sym(executor, '}', "expect '}'")) {
              return ts_false;
            }
          }
          else if (!parse_or_expr(executor, ref_next_param)) {
            break;
          }
          ref_next_param = &(*ref_next_param)->next;
          if (!parser_accept_sym(executor, ',')) {
            break;
          }
        }
        if (!parser_expect_sym(executor, ')', "expect ')'")) {
          return ts_false;
        }
      }
      else {
        if (expr == NULL) {
          new_expr->eval = eval_var;
          new_expr->text = name;
          name = NULL;
        }
        else {
          proc_definition_t *proc_def = executor_add_proc(executor, name);
          if (proc_def == NULL) {
            free_expr(new_expr);
            free_expr(expr);
            return ts_false;
          }
          if (proc_def->field != 0) {
            new_expr->eval = proc_def->field;
          }
          else {
            parser_error_ident(executor, "unknown field", name);
          }
          new_expr->children = expr;
        }
      }
      free((void*)name);
      expr = new_expr;
      if (!parser_accept_sym(executor, '.')) {
        break;
      }
    }
    *ref_expr = expr;
  }
  else if (executor->scanner.symbol == SCANNER_SYMBOL_NUMBER) {
    *ref_expr = create_expr();
    if (*ref_expr == NULL) {
      return ts_false;
    }
    (*ref_expr)->eval = eval_number_const;
    
    (*ref_expr)->number = 0L;	
    for (const char *s = executor->scanner.text; *s != '\0'; s++)
    	(*ref_expr)->number = 10L * (*ref_expr)->number + *s - '0';
    (*ref_expr)->pos = executor->scanner.start;
    scanner_next_token(&executor->scanner);
  }
  else if (executor->scanner.symbol == SCANNER_SYMBOL_STRING) {
    *ref_expr = create_expr();
    if (*ref_expr == NULL) {
      return ts_false;
    }
    (*ref_expr)->eval = eval_string_const;
    (*ref_expr)->text = parser_claim_text(executor);
    (*ref_expr)->pos = executor->scanner.start;
    scanner_next_token(&executor->scanner);
  }
  else if (   executor->scanner.symbol == SCANNER_SYMBOL_TRUE
           || executor->scanner.symbol == SCANNER_SYMBOL_FALSE) {
    *ref_expr = create_expr();
    if (*ref_expr == NULL) {
      return ts_false;
    }
    (*ref_expr)->eval = eval_bool_const;
    (*ref_expr)->number = executor->scanner.symbol == SCANNER_SYMBOL_TRUE ? 1 : 0;
    (*ref_expr)->pos = executor->scanner.start;
    scanner_next_token(&executor->scanner);
  }
  else {
    /*parser_error(executor, "Expect expression term");*/
    return ts_false;
  }
  return ts_true;
}

EVAL_FUNC(eval_not);

static ts_bool parse_prefix_expr(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  if (parser_accept_sym(executor, '!')) {
    *ref_expr = create_expr();
    if (*ref_expr == NULL) {
      return ts_false;
    }
    (*ref_expr)->eval = eval_not;
    (*ref_expr)->pos = executor->scanner.start;
    return parse_term(executor, &(*ref_expr)->children);
  }
  else {
    return parse_term(executor, ref_expr);
  }
}

EVAL_FUNC(eval_times);

static ts_bool parse_mul_expr(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  ts_expr_t *expr = NULL;
  if (!parse_prefix_expr(executor, &expr)) {
    return ts_false;
  }
  while (parser_accept_sym(executor, '*')) {
    ts_expr_t *new_expr = create_expr();
    if (new_expr == NULL) {
      free_expr(expr);
      return ts_false;
    }
    new_expr->eval = eval_times;
    new_expr->children = expr;
    new_expr->pos = executor->scanner.start;
    parse_prefix_expr(executor, &expr->next);
    expr = new_expr;
  }
  *ref_expr = expr;
  return ts_true;
}

EVAL_FUNC(eval_add);

static ts_bool parse_add_expr(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  ts_expr_t *expr = NULL;
  if (!parse_mul_expr(executor, &expr)) {
    return ts_false;
  }
  while (parser_accept_sym(executor, '+')) {
    ts_expr_t *new_expr = create_expr();
    if (new_expr == NULL) {
      free_expr(expr);
      return ts_false;
    }
    new_expr->eval = eval_add;
    new_expr->children = expr;
    new_expr->pos = executor->scanner.start;
    parse_mul_expr(executor, &expr->next);
    expr = new_expr;
  }
  *ref_expr = expr;
  return ts_true;
}

EVAL_FUNC(eval_equal);

static ts_bool parse_compare_expr(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  ts_expr_t *expr = NULL;
  if (!parse_add_expr(executor, &expr)) {
    return ts_false;
  }
  if (parser_accept_sym(executor, SCANNER_SYMBOL_EQUAL)) {
    ts_expr_t *new_expr = create_expr();
    if (new_expr == NULL) {
      free_expr(expr);
      return ts_false;
    }
    new_expr->eval = eval_equal;
    new_expr->children = expr;
    new_expr->pos = executor->scanner.start;
    parse_add_expr(executor, &expr->next);
    expr = new_expr;
  }
  *ref_expr = expr;
  return ts_true;
}

EVAL_FUNC(eval_and);

static ts_bool parse_and_expr(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  ts_expr_t *expr = NULL;
  if (!parse_compare_expr(executor, &expr)) {
    return ts_false;
  }
  while (parser_accept_sym(executor, SCANNER_SYMBOL_AND)) {
    ts_expr_t *new_expr = create_expr();
    if (new_expr == NULL) {
      free_expr(expr);
      return ts_false;
    }
    new_expr->eval = eval_and;
    new_expr->children = expr;
    new_expr->pos = executor->scanner.start;
    parse_compare_expr(executor, &expr->next);
    expr = new_expr;
  }
  *ref_expr = expr;
  return ts_true;
}

EVAL_FUNC(eval_or);

static ts_bool parse_or_expr(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  ts_expr_t *expr = NULL;
  if (!parse_and_expr(executor, &expr)) {
    return ts_false;
  }
  while (parser_accept_sym(executor, SCANNER_SYMBOL_OR)) {
    ts_expr_t *new_expr = create_expr();
    if (new_expr == NULL) {
      free_expr(expr);
      return ts_false;
    }
    new_expr->eval = eval_or;
    new_expr->children = expr;
    new_expr->pos = executor->scanner.start;
    parse_and_expr(executor, &expr->next);
  }
  *ref_expr = expr;
  return ts_true;
}

EVAL_FUNC(eval_if);
EVAL_FUNC(eval_for);
EVAL_FUNC(eval_switch);
EVAL_FUNC(eval_emit);

static ts_bool parse_statement(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  if (parser_accept_sym(executor, '{')) {
    parse_statements(executor, ref_expr);
    if (!parser_expect_sym(executor, '}', "expect '}'")) {
      return ts_false;
    }
  }
  else if (parser_accept_sym(executor, SCANNER_SYMBOL_IF)) {
    *ref_expr = create_expr();
    if (*ref_expr == NULL) {
      return ts_false;
    }
    (*ref_expr)->eval = eval_if;
    (*ref_expr)->pos = executor->scanner.start;
    if (   !parser_expect_sym(executor, '(', "expect '('")
        || !parse_or_expr(executor, &(*ref_expr)->children)
        || !parser_expect_sym(executor, ')', "expect ')'")
        || !parse_statement(executor, &(*ref_expr)->children->next)) {
      return ts_false;
    }
    if (parser_accept_sym(executor, SCANNER_SYMBOL_ELSE)) {
      parse_statement(executor, &(*ref_expr)->children->next->next);
    }
  }
  else if (parser_accept_sym(executor, SCANNER_SYMBOL_FOR)) {
    *ref_expr = create_expr();
    if (*ref_expr == NULL) {
      return ts_false;
    }
    (*ref_expr)->eval = eval_for;
    (*ref_expr)->pos = executor->scanner.start;
    if (executor->scanner.symbol != SCANNER_SYMBOL_IDENT) {
      parser_error(executor, "expect variable");
      return ts_false;
    }
    (*ref_expr)->text = parser_claim_text(executor);
    scanner_next_token(&executor->scanner);
    if (   !parser_expect_sym(executor, SCANNER_SYMBOL_IN, "expect 'in'")
        || !parse_or_expr(executor, &(*ref_expr)->children)) {
      return ts_false;
    }
    parse_statement(executor, &(*ref_expr)->children->next);
  }
  else if (parser_accept_sym(executor, SCANNER_SYMBOL_SWITCH)) {
    *ref_expr = create_expr();
    if (*ref_expr == NULL) {
      return ts_false;
    }
    (*ref_expr)->eval = eval_switch;
    (*ref_expr)->pos = executor->scanner.start;
    if (   !parser_expect_sym(executor, '(', "expect '('")
        || !parse_or_expr(executor, &(*ref_expr)->children)
        || !parser_expect_sym(executor, ')', "expect ')'")
        || !parser_expect_sym(executor, '{', "expect '{'")) {
      return ts_false;
    }
    ts_expr_t **ref_next = &(*ref_expr)->children->next;
    while (parser_accept_sym(executor, SCANNER_SYMBOL_CASE)) {
      *ref_next = create_expr();
      if (*ref_next == NULL) {
        return ts_false;
      }
      (*ref_next)->ch = 'c';
      (*ref_expr)->pos = executor->scanner.start;
      if (   !parse_or_expr(executor, &(*ref_next)->children)
          || !parser_expect_sym(executor, ':', "expect ':'")) {
        return ts_false;
      }
      parse_statements(executor, &(*ref_next)->children->next);
      ref_next = &(*ref_next)->next;
    }
    if (parser_accept_sym(executor, SCANNER_SYMBOL_DEFAULT)) {
      *ref_next = create_expr();
      if (*ref_next == NULL) {
        return ts_false;
      }
      (*ref_next)->ch = 'd';
      (*ref_expr)->pos = executor->scanner.start;
      if (!parser_expect_sym(executor, ':', "expect ':'")) {
        return ts_false;
      }
      parse_statements(executor, &(*ref_next)->children);
      ref_next = &(*ref_next)->next;
    }
    if (!parser_expect_sym(executor, '}', "expect '}'")) {
      return ts_false;
    }
  }
  else if (executor->scanner.symbol == SCANNER_SYMBOL_STRING)
  {
    *ref_expr = create_expr();
    if (*ref_expr == NULL) {
      return ts_false;
    }
    (*ref_expr)->eval = eval_emit;
    (*ref_expr)->text = parser_claim_text(executor);
    (*ref_expr)->pos = executor->scanner.start;
    scanner_next_token(&executor->scanner);
    if (parser_accept_sym(executor, SCANNER_SYMBOL_WITH)) {
      ts_expr_t **ref_next = &(*ref_expr)->children;
      for (; executor->scanner.symbol == SCANNER_SYMBOL_IDENT && strlen(executor->scanner.text) == 1;) {
        *ref_next = create_expr();
        if (*ref_expr == NULL) {
          return ts_false;
        }
        (*ref_next)->ch = executor->scanner.text[0];
        (*ref_expr)->pos = executor->scanner.start;
        scanner_next_token(&executor->scanner);
        if (!parser_expect_sym(executor, '=', "expect '='")) {
          return ts_false;
        }
        if (parser_accept_sym(executor, '{')) {
          parse_statements(executor, &(*ref_next)->children);
          if (!parser_expect_sym(executor, '}', "expect '}'")) {
            return ts_false;
          }
        }
        else
          parse_or_expr(executor, &(*ref_next)->children);
        if (!parser_accept_sym(executor, ',')) {
          break;
        }
        ref_next = &(*ref_next)->next;
      }
    }
    if (!parser_expect_sym(executor, ';', "expect ';'")) {
      return ts_false;
    }
  }
  else {
    return    parse_or_expr(executor, ref_expr)
           && parser_expect_sym(executor, ';', "expect ':'");
  }
  return ts_true;
}

EVAL_FUNC(eval_statements);

static ts_bool parse_statements(ts_executor_t *executor, ts_expr_t **ref_expr)
{
  *ref_expr = create_expr();
  if (*ref_expr == NULL) {
    return ts_false;
  }
  (*ref_expr)->eval = eval_statements;
  (*ref_expr)->pos = executor->scanner.start;
  ts_expr_t **ref_next = &(*ref_expr)->children;
  while (parse_statement(executor, ref_next)) {
    ref_next = &(*ref_next)->next;
  }
  return ts_true;
}

static ts_bool parse_definitions(ts_executor_t *executor)
{
  while (executor->scanner.symbol == SCANNER_SYMBOL_IDENT) {
    fprintf(stderr, "parse_definition %s\n", executor->scanner.text);
    proc_definition_t *proc = executor_add_proc(executor, executor->scanner.text);
    if (proc == NULL) {
      return ts_false;
    }
    if (proc->proc_body != NULL) {
      fprintf(stderr, "proc already defined\n");
      return ts_false;
    }
    scanner_next_token(&executor->scanner);
    if (!parser_expect_sym(executor, '(', "expect '('")) {
      return ts_false;
    }
    for (; executor->scanner.symbol == SCANNER_SYMBOL_IDENT;) {
      char *param = parser_claim_text(executor);
      proc_definition_add_param(proc, param);
      scanner_next_token(&executor->scanner);
      if (!parser_accept_sym(executor, ',')) {
        break;
      }
    }
    if (   !parser_expect_sym(executor, ')', "expect parameter or ')'")
        || !parser_expect_sym(executor, '{', "expect '{'")) {
      return ts_false;
    }
    parse_statements(executor, &proc->proc_body);
    if (!parser_expect_sym(executor, '}', "expect '}'")) {
      return ts_false;
    }
  }
  if (executor->scanner.symbol != SCANNER_SYMBOL_EOF) {
    parser_error(executor, "Unexpected input");
    printf("|%c|\n", executor->scanner.symbol);
  }
  return ts_true;
}

void ts_executor_add_func_func(ts_executor_t *executor, const char *name, ts_eval_func_p func)
{
  proc_definition_t *proc = executor_add_proc(executor, name);
  if (proc != NULL) {
    proc->func = func;
  }
}

void ts_executor_add_method_func(ts_executor_t *executor, const char *name, ts_eval_func_p method)
{
  proc_definition_t *proc = executor_add_proc(executor, name);
  if (proc != NULL) {
    proc->method = method;
  }
}

void ts_executor_add_field_func(ts_executor_t *executor, const char *name, ts_eval_func_p field)
{
  proc_definition_t *proc = executor_add_proc(executor, name);
  if (proc != NULL) {
    proc->field = field;
  }
}

EVAL_FUNC(eval_field_tree_parent);
EVAL_FUNC(eval_method_hasProperty);
EVAL_FUNC(eval_method_getProperty);
EVAL_FUNC(eval_method_setProperty);
EVAL_FUNC(eval_method_removeProperties);
EVAL_FUNC(eval_func_toStr);
EVAL_FUNC(eval_func_toUpper);
EVAL_FUNC(eval_func_root);
EVAL_FUNC(eval_func_now);
EVAL_FUNC(eval_func_streamToFile);

static void parser_init(ts_executor_t *executor, FILE *file)
{
  scanner_init(&executor->scanner, file);
  executor->proc_defs = NULL;
  executor->error = ts_false;
  ts_executor_add_field_func(executor, "tree_parent", eval_field_tree_parent);
  ts_executor_add_method_func(executor, "hasProperty", eval_method_hasProperty);
  ts_executor_add_method_func(executor, "getProperty", eval_method_getProperty);
  ts_executor_add_method_func(executor, "setProperty", eval_method_setProperty);
  ts_executor_add_method_func(executor, "removeProperties", eval_method_removeProperties);
  ts_executor_add_func_func(executor, "toStr", eval_func_toStr);
  ts_executor_add_func_func(executor, "toUpper", eval_func_toUpper);
  ts_executor_add_func_func(executor, "root", eval_func_root);
  ts_executor_add_func_func(executor, "now", eval_func_now);
  ts_executor_add_func_func(executor, "streamToFile", eval_func_streamToFile);
}

static void parser_fini(ts_executor_t *executor)
{
  scanner_fini(&executor->scanner);
}

/* Script execution */

typedef struct property_t property_t;
typedef struct call_stack_entry_t call_stack_entry_t;
typedef struct break_point_t break_point_t;
typedef struct debug_ostream_t debug_ostream_t;

struct call_stack_entry_t {
  const char *name;
  ts_expr_t *expr;
  ts_scope_t *scope;
  call_stack_entry_t *up;
  call_stack_entry_t *down;
};

struct ts_exec_context_t {
  ostream_t *ostream;
  TS_TREE_TYPE *root;
  TS_CONTEXT* context_data;
  property_t *properties;
/* For debugging: */
  debug_ostream_t *debug_ostream;
  call_stack_entry_t *bottom;
  call_stack_entry_t top;
  break_point_t *break_points;
  ts_bool one_step;
  ts_expr_t *next_stop;
};

struct property_t {
  ts_value_t key1;
  ts_value_t key2;
  ts_value_t value;
  property_t *next;
};

struct break_point_t {
  char *file_name;
  unsigned long line;
  unsigned long column;
  break_point_t *next;
};

break_point_t *create_break_point(char *file_name, unsigned long line, unsigned long column)
{
  break_point_t *break_point = (break_point_t*)malloc(sizeof(break_point));
  if (break_point == NULL) {
    return NULL;
  }
  break_point->file_name = file_name != NULL ? strdup(file_name) : NULL;
  break_point->line = line;
  break_point->column = column;
  return break_point;
}

void free_break_point(break_point_t *break_point)
{
  free((void*)break_point->file_name);
  free((void*)break_point);
}


static void exec_context_init(ts_exec_context_t *context, ostream_t *ostream, TS_TREE_TYPE *root, TS_CONTEXT* context_data, ts_bool debugging, debug_ostream_t *debug_ostream) {
  context->ostream = ostream;
  context->root = root;
  context->context_data = context_data;
  context->properties = NULL;
  context->debug_ostream = debug_ostream;
  context->bottom = &context->top;
  context->top.name = "<top>";
  context->top.expr = NULL;
  context->top.scope = NULL;
  context->top.up = NULL;
  context->top.down = NULL;
  context->break_points = NULL;
  context->one_step = debugging;
  context->next_stop = NULL;
}

static void exec_context_fini(ts_exec_context_t *context)
{
  property_t *property = context->properties;
  while (property != NULL) {
    property_t *next = property->next;
    free((void*)property);
    property = next;
  }
}

static ts_bool exec_context_get_property(ts_exec_context_t *context, ts_value_t *key1, ts_value_t *key2, ts_value_t *value)
{
  property_t *prop = context->properties;
  for (; prop != NULL; prop = prop->next) {
    int comp = value_comp(&prop->key1, key1);
    if (comp == 0) {
      comp = value_comp(&prop->key2, key2);
    }
    if (comp == 0) {
      *value = prop->value;
      return ts_true;
    }
    if (comp > 0) {
      break;
    }
  }
  return ts_false;
}

static void exec_context_set_property(ts_exec_context_t *context, ts_value_t *key1, ts_value_t *key2, ts_value_t *value)
{
  property_t **ref_prop = &context->properties;
  for (; *ref_prop != NULL; ref_prop = &(*ref_prop)->next) {
    int comp = value_comp(&(*ref_prop)->key1, key1);
    if (comp == 0) {
      comp = value_comp(&(*ref_prop)->key2, key2);
    }
    if (comp == 0) {
      (*ref_prop)->value = *value;
      return;
    }
    if (comp > 0) {
      break;
    }
  }
  property_t *new_prop = (property_t*)malloc(sizeof(property_t));
  if (new_prop == NULL) {
    return;
  }
  new_prop->key1 = *key1;
  new_prop->key2 = *key2;
  new_prop->value = *value;
  new_prop->next = *ref_prop;
  *ref_prop = new_prop;
}

static void exec_context_remove_properties(ts_exec_context_t *context, ts_value_t *key1, ts_value_t *key2)
{
  property_t **ref_prop = &context->properties;
  while (*ref_prop != NULL) {
    ts_bool match = ts_false;
    if (value_comp(&(*ref_prop)->key2, key2) == 0) {
      if (key2->type != ts_value_type_node || key1->type != ts_value_type_node) {
        match = ts_true;
      }
      else {
        for (TS_TREE_TYPE *node = (*ref_prop)->key1.node; node != NULL; node = TS_PARENT_OF_TREE_TYPE(node)) {
          if (node == key1->node) {
            match = ts_true;
            break;
          }
        }
      }
    }
    if (match) {
      property_t *prop = *ref_prop;
      *ref_prop = (*ref_prop)->next;
      free((void*)prop);
    }
    else {
      ref_prop = &(*ref_prop)->next;
    }
  }
}

struct debug_ostream_t {
  ostream_t ostream;
  ts_bool debugging;
  ts_bool emitted;
  char *output_file_name;
  file_pos_t output_pos;
  ostream_t *output;
};

ts_bool debug_ostream_open(ostream_t *ostream, const char *name)
{
  debug_ostream_t* debug_ostream = (debug_ostream_t*)ostream;
  if (debug_ostream->debugging) {
    if (debug_ostream->emitted) {
      fprintf(stdout, "]\n");
      debug_ostream->emitted = ts_false;
    }
    free((void*)debug_ostream->output_file_name);
    debug_ostream->output_file_name = strdup(name);
    debug_ostream->output_pos.line = 1UL;
    debug_ostream->output_pos.column = 1UL;
    fprintf(stdout, "Open file '%s'\n", name);
  }
  return ostream_open(debug_ostream->output, name);
}

void debug_ostream_close(ostream_t *ostream)
{
  debug_ostream_t* debug_ostream = (debug_ostream_t*)ostream;
  if (debug_ostream->debugging) {
    if (debug_ostream->emitted) {
      fprintf(stdout, "]\n");
      debug_ostream->emitted = ts_false;
    }
    fprintf(stdout,"Close file '%s'\n", debug_ostream->output_file_name);
    free((void*)debug_ostream->output_file_name);
    debug_ostream->output_file_name = NULL;
    debug_ostream->output_pos.line = 0UL;
    debug_ostream->output_pos.column = 0UL;
  }
  ostream_close(((debug_ostream_t*)ostream)->output);
}

void debug_ostream_put(ostream_t *ostream, char ch)
{
  debug_ostream_t* debug_ostream = (debug_ostream_t*)ostream;
  if (debug_ostream->debugging) {
    if (!debug_ostream->emitted) {
      fprintf(stdout, "[");
      debug_ostream->emitted = ts_true;
    }
    fprintf(stdout, "%c", ch);
    if (ch == '\n') {
      debug_ostream->output_pos.line++;
      debug_ostream->output_pos.column = 1UL;
    }
    else {
      debug_ostream->output_pos.column++;
    }
  }
  ostream_put(debug_ostream->output, ch);
}

void debug_ostream_init(debug_ostream_t *ostream, ts_bool debugging, ostream_t *output)
{
  ostream->ostream.open = debug_ostream_open;
  ostream->ostream.close = debug_ostream_close;
  ostream->ostream.put = debug_ostream_put;
  ostream->emitted = ts_false;
  ostream->debugging = debugging;
  ostream->output_file_name = NULL;
  ostream->output_pos.line = 0UL;
  ostream->output_pos.column = 0UL;
  ostream->output = output;
}

void debug_ostream_fini(debug_ostream_t *ostream)
{
  free((void*)ostream->output_file_name);
}


void exec_context_break(ts_exec_context_t *exec_context)
{
  if (exec_context->debug_ostream->emitted) {
    fprintf(stdout, "]\n");
    exec_context->debug_ostream->emitted = ts_false;
  }
}

void exec_context_debug(ts_exec_context_t *exec_context, call_stack_entry_t *call_stack_entry)
{
  exec_context_break(exec_context);
  exec_context->one_step = ts_false;
  exec_context->next_stop = NULL;
  call_stack_entry_t *here = call_stack_entry;
  for (;;) {
    fprintf(stdout, "At %lu.%lu at %s expression ", here->expr->pos.line, here->expr->pos.column, here->name);
    if (here->expr->text != NULL) {
      fprintf(stdout, " text='%s'", here->expr->text);
    }
    if (here->expr->ch != '\0') {
      fprintf(stdout, " ch='%c'", here->expr->ch);
    }
    if (here->expr->proc_def != NULL) {
      fprintf(stdout, " proc=%s", here->expr->proc_def->name);
    }
    if (here->expr->children != NULL) {
      fprintf(stdout, " children");
    }
    fprintf(stdout, "\n> ");
    char command[100];
    fgets(command, 99, stdin);
    for (char *s = command; *s != '\0'; s++) {
      if (*s == '\n') {
        *s = '\0';
      }
    }
    if (strcmp(command, "") == 0) {
      exec_context->one_step = ts_true;
      break;
    }
    else if (strcmp(command, "skip") == 0) {
      for (call_stack_entry_t *entry = call_stack_entry; entry != NULL; entry = entry->up) {
        if (entry->expr != NULL && entry->expr->next != NULL) {
          exec_context->next_stop = entry->expr->next;
          break;
        }
      }
      break;
    }
    else if (strcmp(command, "cont") == 0) {
      break;
    }
    else if (strcmp(command, "up") == 0) {
      if (here->up == NULL || here->up->expr == NULL) {
        fprintf(stdout, "At top\n");
      }
      else {
        here = here->up;
      }
    }
    else if (strcmp(command, "down") == 0) {
      if (here->down == NULL) {
        fprintf(stdout, "At bottom\n");
      }
      else {
        here = here->down;
      }
    }
    else if (strcmp(command, "p") == 0) {
      for (ts_scope_t *scope = here->scope; scope != NULL; scope = scope->prev) {
        fprintf(stdout, " %s = ", scope->var);
        value_print(&scope->value, stdout);
        fprintf(stdout, "\n");
      }
    }
    else if (   strncmp(command, "break ", 6) == 0
             || strncmp(command, "clear ", 6) == 0)
    {
      char *s = command + 6;
      while (*s == ' ') {
        s++;
      }
      char *file_name = NULL;
      if (!('0' <= *s && *s <= '9')) {
        file_name = s;
        while (*s != '\0' && *s != ':') {
          s++;
        }
        if (*s == ':') {
          *s = '\0';
          s++;
        }
        fprintf(stdout, "File '%s'\n", file_name);
      }
      fprintf(stdout, " at '%s' \n", s);
      unsigned long line = 0UL;
      for (; '0' <= *s && *s <= '9'; s++) {
        line = 10UL * line + (unsigned long)(*s - '0');
      }
      unsigned long column = 0UL;
      if (*s == '.') {
        for (s++ ; '0' <= *s && *s <= '9'; s++) {
          column = 10UL * column + (unsigned long)(*s - '0');
        }
      }
      if (line == 0UL) {
        fprintf(stdout, "Do not understand '%s'\n", command);
      }
      else if (*command == 'b') {
        break_point_t *new_break_point = create_break_point(file_name, line, column);
        if (new_break_point != NULL) {
          new_break_point->next = exec_context->break_points;
          exec_context->break_points = new_break_point;
        }
      }
      else {
        break_point_t **ref_break_point = &exec_context->break_points;
        while ((*ref_break_point) != NULL) {
          if (   (*ref_break_point)->line == line
              && (   (*ref_break_point)->column == 0
                  || column == 0
                  || (*ref_break_point)->column == column)
              && (   ((*ref_break_point)->file_name == NULL && file_name == NULL)
                  || (   (*ref_break_point)->file_name != NULL && file_name != NULL
                      && strcmp((*ref_break_point)->file_name, file_name) == 0))) {
             break_point_t *old_break_point = *ref_break_point;
             *ref_break_point = (*ref_break_point)->next;
             free_break_point(old_break_point);
          }
          else {
            ref_break_point = &(*ref_break_point)->next;
          }
        }
      }
    }
    else {
      fprintf(stdout, "Do not understand '%s'\n", command);
    }
  }
}
 
void exec_context_enter(ts_exec_context_t *exec_context, const char* name, call_stack_entry_t *call_stack_entry, ts_scope_t *scope, ts_expr_t *expr, ts_value_t *result)
{
  call_stack_entry->name = name;
  call_stack_entry->expr = expr;
  call_stack_entry->scope = scope;
  call_stack_entry->up = exec_context->bottom;
  exec_context->bottom->down = call_stack_entry;
  exec_context->bottom = call_stack_entry;
  ts_bool stop = exec_context->one_step || exec_context->next_stop == expr;
  if (!stop) {
    for (break_point_t *break_point = exec_context->break_points; break_point != NULL; break_point = break_point->next) {
      if (  break_point->file_name != NULL
          ?    break_point->line == exec_context->debug_ostream->output_pos.line
            && (break_point->column == 0 || break_point->column == exec_context->debug_ostream->output_pos.column)
            && strcmp(break_point->file_name, exec_context->debug_ostream->output_file_name) == 0
          :    break_point->line == expr->pos.line
            && (break_point->column == 0 || break_point->column == expr->pos.column)) {
        stop = ts_true;
        break;
      }
    }
  }
  if (!value_valid(result)) {
    fprintf(stdout, "Error: result invalid\n");
    stop = ts_true;
  }
  if (stop) {
    exec_context_debug(exec_context, call_stack_entry);
  }
}

void exec_context_leave(ts_exec_context_t *exec_context, call_stack_entry_t *call_stack_entry, ts_value_t *result)
{
  if (value_valid(result)) {
    if (exec_context->one_step) {
      fprintf(stdout, "Result: ");
      value_print(result, stdout);
      fprintf(stdout, "\n");
    }
  }
  else {
    exec_context_break(exec_context);
    fprintf(stdout, "Result invalid on exit\n");
    exec_context_debug(exec_context, call_stack_entry);
  }
  exec_context->bottom = exec_context->bottom->up;
  exec_context->bottom->down = NULL;
}

static ts_bool check_script(ts_executor_t *executor);

extern void exec_templ_script(TS_CONTEXT* context_data, TS_TREE_TYPE *root_node, FILE* tsf, ts_bool debugging)
{
  printf("exec_templ_script\n");

  ts_executor_t executor;
  parser_init(&executor, tsf);

  if (!parse_definitions(&executor) || executor.error) {
    fprintf(stderr, "Parse errors\n");
    return;
  }

  if (!check_script(&executor)) {
    return;
  }

  ostream_to_files_t ostream_to_files;
  init_ostream_to_files(&ostream_to_files);
  debug_ostream_t debug_ostream;
  debug_ostream_init(&debug_ostream, debugging, (ostream_t*)&ostream_to_files);
  ts_exec_context_t context;
  exec_context_init(&context, (ostream_t*)&debug_ostream, root_node, context_data, debugging, &debug_ostream);

  proc_definition_t *main_proc = executor_add_proc(&executor, "main");
  if (main_proc == NULL || main_proc->proc_body == NULL) {
    fprintf(stderr, "Missing 'main' proc\n");
  }
  else {
    ts_scope_t scope;
    scope.var = main_proc->proc_params != NULL ? main_proc->proc_params->name : "root";
    scope.value.type = ts_value_type_node;
    scope.value.node = root_node;
    scope.prev = NULL;
    ts_value_t result;
    value_init(&result);
    main_proc->proc_body->eval(main_proc->proc_body, &scope, &context, &result);
  }
  exec_context_fini(&context);
  debug_ostream_fini(&debug_ostream);
  parser_fini(&executor);
}

/* ostream to string */

typedef struct {
  ostream_t ostream;
  string_appender_t string_appender;
} ostream_to_string_t;

void ostream_to_string_put(ostream_t *ostream, char ch)
{
  string_appender_add(&((ostream_to_string_t*)ostream)->string_appender, ch);
}

void ostream_to_string_init(ostream_to_string_t *ostream)
{
  ostream->ostream.open = NULL;
  ostream->ostream.close = NULL;
  ostream->ostream.put = ostream_to_string_put;
  string_appender_init(&ostream->string_appender);
}

char *ostream_to_string_take_string(ostream_to_string_t *ostream)
{
  char *result = string_appender_result(&ostream->string_appender);
  string_appender_free(&ostream->string_appender);
  return result;
}

/* Eval functions */

EVAL_FUNC(eval_nop)
{
  ENTER_EVAL_FUNC("nop")
  UNUSED_ARG(expr);
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  UNUSED_ARG(result);
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_field_tree_parent)
{
  ENTER_EVAL_FUNC("field 'tree_parent'")
  ts_value_t value;
  value_init(&value);
  if (!expr->children->eval(expr->children, scope, context, &value)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (value.node == NULL) {
    EVAL_FUNC_ERROR_VALUE("field 'tree_parent': is not a node, but ", &value);
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (TS_PARENT_OF_TREE_TYPE(value.node) != NULL && TS_PARENT_OF_TREE_TYPE(TS_PARENT_OF_TREE_TYPE(value.node)) != NULL) {
    result->type = ts_value_type_node;
    result->node = TS_PARENT_OF_TREE_TYPE(value.node);
  }
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_method_hasProperty)
{
  ENTER_EVAL_FUNC("method 'hasProperty'")
  if (expr->children->next == NULL || expr->children->next->next != NULL) {
    EVAL_FUNC_ERROR("wrong number of arguments");
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t key1;
  value_init(&key1);
  if (!expr->children->eval(expr->children, scope, context, &key1)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t key2;
  value_init(&key2);
  if (!expr->children->next->eval(expr->children->next, scope, context, &key2)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t dummy_value;
  result->type = ts_value_type_bool;
  result->number = exec_context_get_property(context, &key1, &key2, &dummy_value);
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_method_getProperty)
{
  ENTER_EVAL_FUNC("method 'getProperty'")
  if (expr->children->next == NULL || expr->children->next->next != NULL) {
    EVAL_FUNC_ERROR("wrong number of arguments");
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t key1;
  value_init(&key1);
  if (!expr->children->eval(expr->children, scope, context, &key1)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t key2;
  value_init(&key2);
  if (!expr->children->next->eval(expr->children->next, scope, context, &key2)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  exec_context_get_property(context, &key1, &key2, result);
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_method_setProperty)
{
  ENTER_EVAL_FUNC("method 'setProperty'")
  if (expr->children->next == NULL || expr->children->next->next == NULL || expr->children->next->next->next != NULL) {
    EVAL_FUNC_ERROR("wrong number of arguments");
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t key1;
  value_init(&key1);
  if (!expr->children->eval(expr->children, scope, context, &key1)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t key2;
  value_init(&key2);
  if (!expr->children->next->eval(expr->children->next, scope, context, &key2)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t value;
  value_init(&value);
  if (!expr->children->next->next->eval(expr->children->next->next, scope, context, &value)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  result->type = ts_value_type_bool;
  exec_context_set_property(context, &key1, &key2, &value);
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_method_removeProperties)
{
  ENTER_EVAL_FUNC("method 'setProperty'")
  if (expr->children->next == NULL || expr->children->next->next != NULL) {
    EVAL_FUNC_ERROR("wrong number of arguments");
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t key1;
  value_init(&key1);
  if (!expr->children->eval(expr->children, scope, context, &key1)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t key2;
  value_init(&key2);
  if (!expr->children->next->eval(expr->children->next, scope, context, &key2)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  result->type = ts_value_type_bool;
  exec_context_remove_properties(context, &key1, &key2);
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_func_toStr)
{
  ENTER_EVAL_FUNC("function 'toStr'")
  UNUSED_ARG(result);
  if (expr->children == NULL || expr->children->next != NULL) {
    EVAL_FUNC_ERROR("Expect one argument");
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t number;
  value_init(&number);
  if (!expr->children->eval(expr->children, scope, context, &number)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (number.type != ts_value_type_int) {
    EVAL_FUNC_ERROR_VALUE("expect int value", &number);
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  char buffer[30];
  snprintf(buffer, 29, "%ulld", number.number);
  ostream_puts(context->ostream, buffer);
  LEAVE_EVAL_FUNC
  return ts_true;
}

typedef struct {
  ostream_t ostream;
  ostream_t *output;
} ostream_to_upper_t;

void ostream_to_upper_put(ostream_t *ostream, char ch)
{
  if ('a' <= ch && ch <= 'z') {
    ch = (char)(ch - 'a' + 'A');
  }
  ostream_put(((ostream_to_upper_t*)ostream)->output, ch);
}

void ostream_to_upper_init(ostream_to_upper_t *ostream, ostream_t *output)
{
  ostream->ostream.open = NULL;
  ostream->ostream.close = NULL;
  ostream->ostream.put = ostream_to_upper_put;
  ostream->output = output;
}

EVAL_FUNC(eval_func_toUpper)
{
  ENTER_EVAL_FUNC("function 'toUpper'")
  UNUSED_ARG(result);
  if (expr->children == NULL || expr->children->next != NULL) {
    EVAL_FUNC_ERROR("Expect one argument");
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ostream_to_upper_t ostream_to_upper;
  ostream_to_upper_init(&ostream_to_upper, context->ostream);
  ostream_t *cur_ostream = context->ostream;
  context->ostream = &ostream_to_upper.ostream;
  ts_value_t text;
  value_init(&text);
  if (!expr->children->eval(expr->children, scope, context, &text)) {
    context->ostream = cur_ostream;
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  context->ostream = cur_ostream;
  if (text.type == ts_value_type_text) {
    for (const char *s = text.text; *s != '\0'; s++) {
      if ('a' <= *s && *s <= 'z') {
        ostream_put(context->ostream, (char)(*s - 'a' + 'A'));
      }
      else {
        ostream_put(context->ostream, *s);
      }
    }
  }
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_func_root)
{
  ENTER_EVAL_FUNC("function 'root'")
  UNUSED_ARG(expr);
  UNUSED_ARG(scope);
  result->type = ts_value_type_node;
  result->node = context->root;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_func_now)
{
  ENTER_EVAL_FUNC("function 'now'")
  UNUSED_ARG(expr);
  UNUSED_ARG(scope);
  UNUSED_ARG(result);
  time_t time_now = time(NULL);
  ostream_puts(context->ostream, ctime(&time_now));
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_func_streamToFile)
{
  ENTER_EVAL_FUNC("function 'streamToFile'")
  if (expr->children == NULL || expr->children->next == NULL || expr->children->next->next != NULL) {
    EVAL_FUNC_ERROR("Expect two arguments");
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ostream_to_string_t ostream_to_string;
  ostream_to_string_init(&ostream_to_string);
  ostream_t *cur_ostream = context->ostream;
  context->ostream = &ostream_to_string.ostream;
  ts_value_t dummy;
  value_init(&dummy);
  if (!expr->children->eval(expr->children, scope, context, &dummy)) {
    context->ostream = cur_ostream;
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  /* Check on dummy being not used? */
  context->ostream = cur_ostream;
  char *file_name = ostream_to_string_take_string(&ostream_to_string);
  if (file_name == NULL) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ostream_open(context->ostream, file_name);
  free((void*)file_name);
  ts_bool eval_result = expr->children->next->eval(expr->children->next, scope, context, result);
  ostream_close(context->ostream);
  LEAVE_EVAL_FUNC
  return eval_result;
}

EVAL_FUNC(eval_proc_call)
{
  ENTER_EVAL_FUNC("proc_call")
  if (expr->proc_def == NULL || expr->proc_def->proc_body == NULL) {
    EVAL_FUNC_ERROR("proc not defined");
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_scope_t *new_scope = NULL;
  ts_expr_t *child_expr = expr->children;
  proc_parameter_t *param = expr->proc_def->proc_params;
  for (; child_expr != NULL && param != NULL; child_expr = child_expr->next, param = param->next) {
    ts_scope_t *n_scope = (ts_scope_t*)malloc(sizeof(ts_scope_t));
    if (n_scope == NULL) {
    LEAVE_EVAL_FUNC
      return ts_false;
    }
    n_scope->var = param->name;
    value_init(&n_scope->value);
    n_scope->value.type = ts_value_type_expr;
    n_scope->value.scope = scope;
    n_scope->value.expr = child_expr;
    n_scope->prev = new_scope;
    new_scope = n_scope;
  }
  if (child_expr != NULL || param != NULL) {
    EVAL_FUNC_ERROR("number parameters does not match");
  }
  else {
    expr->proc_def->proc_body->eval(expr->proc_def->proc_body, new_scope, context, result);
  }

  while (new_scope != NULL) {
    ts_scope_t *prev = new_scope->prev;
    free((void*)new_scope);
    new_scope = prev;
  }

  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_var)
{
  ENTER_EVAL_FUNC("var")
  UNUSED_ARG(expr);
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  UNUSED_ARG(result);
  for (; scope != NULL; scope = scope->prev) {
    /*
    fprintf(stderr, "scope->var = '%s'\n", scope->var == NULL ? "(null)" : scope->var);
    fprintf(stderr, "expr->text = '%s'\n", expr->text == NULL ? "(null)" : expr->text);
    */
    if (strcmp(scope->var, expr->text) == 0) {
      if (scope->value.type == ts_value_type_expr) {
        LEAVE_EVAL_FUNC
        return scope->value.expr->eval(scope->value.expr, scope->value.scope, context, result);
      }
      *result = scope->value;
      LEAVE_EVAL_FUNC
      return ts_true;
    }
  }
  EVAL_FUNC_ERROR("Undefined variable");
  LEAVE_EVAL_FUNC
  return ts_false;
}

EVAL_FUNC(eval_number_const)
{
  ENTER_EVAL_FUNC("number_const")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  result->type = ts_value_type_int;
  result->number = expr->number;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_string_const)
{
  ENTER_EVAL_FUNC("string_const")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  result->type = ts_value_type_text;
  result->text = expr->text;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_bool_const)
{
  ENTER_EVAL_FUNC("bool_const")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  result->type = ts_value_type_bool;
  result->number = expr->number;
  LEAVE_EVAL_FUNC
  return ts_true;
}


EVAL_FUNC(eval_not)
{
  ENTER_EVAL_FUNC("not")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  if (!expr->children->eval(expr->children, scope, context, result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  result->type = ts_value_type_bool;
  result->number = result->number != 0 ? 0 : 1;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_times)
{
  ENTER_EVAL_FUNC("times")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  if (!expr->children->eval(expr->children, scope, context, result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (result->type != ts_value_type_int) {
    EVAL_FUNC_ERROR_VALUE("expect integer for lhs", result);
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t rhs;
  value_init(&rhs);
  if (!expr->children->next->eval(expr->children->next, scope, context, &rhs)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (rhs.type != ts_value_type_int) {
    EVAL_FUNC_ERROR_VALUE("expect integer for rhs", &rhs);
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  result->number *= rhs.number;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_add)
{
  ENTER_EVAL_FUNC("add")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  if (!expr->children->eval(expr->children, scope, context, result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (result->type != ts_value_type_int) {
    EVAL_FUNC_ERROR_VALUE("expect integer for lhs", result);
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t rhs;
  value_init(&rhs);
  if (!expr->children->next->eval(expr->children->next, scope, context, &rhs)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (rhs.type != ts_value_type_int) {
    EVAL_FUNC_ERROR_VALUE("expect integer for rhs", &rhs);
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  result->number += rhs.number;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_equal)
{
  ENTER_EVAL_FUNC("equal")
  ts_value_t lhs;
  value_init(&lhs);
  if (!expr->children->eval(expr->children, scope, context, &lhs)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_value_t rhs;
  value_init(&rhs);
  if (!expr->children->next->eval(expr->children->next, scope, context, &rhs)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  result->type = ts_value_type_bool;
  result->number = value_comp(&lhs, &rhs) == 0;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_and)
{
  ENTER_EVAL_FUNC("and")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  if (!expr->children->eval(expr->children, scope, context, result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (!value_is_true(result)) {
    result->type = ts_value_type_bool;
    result->number = 0;
    LEAVE_EVAL_FUNC
    return ts_true;
  }
  if (!expr->children->next->eval(expr->children->next, scope, context, result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  result->number = value_is_true(result) ? 1 : 0;
  result->type = ts_value_type_bool;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_or)
{
  ENTER_EVAL_FUNC("or")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  if (!expr->children->eval(expr->children, scope, context, result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (value_is_true(result)) {
    result->type = ts_value_type_bool;
    result->number = 1;
    LEAVE_EVAL_FUNC
    return ts_true;
  }
  if (!expr->children->next->eval(expr->children->next, scope, context, result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  result->number = value_is_true(result) ? 1 : 0;
  result->type = ts_value_type_bool;
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_if)
{
  ENTER_EVAL_FUNC("if")
  UNUSED_ARG(scope);
  UNUSED_ARG(context);
  if (!expr->children->eval(expr->children, scope, context, result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (value_is_true(result)) {
    if (!expr->children->next->eval(expr->children->next, scope, context, result)) {
      LEAVE_EVAL_FUNC
      return ts_false;
    }
  }
  else if (expr->children->next->next != NULL) {
    if (!expr->children->next->next->eval(expr->children->next->next, scope, context, result)) {
      LEAVE_EVAL_FUNC
      return ts_false;
    }
  }

  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_for)
{
  ENTER_EVAL_FUNC("for")
  /* fprintf(stderr, "Execute for for '%s'\n", expr->text == NULL ? "(null)" : expr->text); */
  UNUSED_ARG(result);
  ts_value_t node_result;
  value_init(&node_result);
  if (!expr->children->eval(expr->children, scope, context, &node_result)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  if (node_result.type != ts_value_type_node) {
    EVAL_FUNC_ERROR_VALUE("Expect node, got: ", &node_result);
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  ts_scope_t for_scope;
  for_scope.var = expr->text;
  for_scope.prev = scope;
  value_init(&for_scope.value);
  for_scope.value.type = ts_value_type_node;
  for (TS_TREE_TYPE *child = TS_FIRST_CHILD(node_result.node); child != NULL; child = TS_NEXT_SIBLING(child)) {
    for_scope.value.node = child;
    ts_value_t loop_result;
    value_init(&loop_result);
    if (!expr->children->next->eval(expr->children->next, &for_scope, context, &loop_result)) {
      LEAVE_EVAL_FUNC
      return ts_false;
    }
  }
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_switch)
{
  ENTER_EVAL_FUNC("switch")
  ts_value_t switch_value;
  value_init(&switch_value);
  if (!expr->children->eval(expr->children, scope, context, &switch_value)) {
    LEAVE_EVAL_FUNC
    return ts_false;
  }
  /* fprintf(stderr, "switch value: "); value_print(&switch_value, stderr); fprintf(stderr, "\n"); */
  ts_expr_t *switch_case = expr->children->next;
  for (; switch_case != NULL; switch_case = switch_case->next) {
    if (switch_case->ch == 'c') {
      ts_value_t case_value;
      value_init(&case_value);
      if (!switch_case->children->eval(switch_case->children, scope, context, &case_value)) {
        LEAVE_EVAL_FUNC
        return ts_false;
      }
      /* fprintf(stderr, "case value: "); value_print(&case_value, stderr); fprintf(stderr, "\n"); */
      if (value_comp(&switch_value, &case_value) == 0) {
        LEAVE_EVAL_FUNC
        return switch_case->children->next->eval(switch_case->children->next, scope, context, result);
      }
    }
    else {
      LEAVE_EVAL_FUNC
      return switch_case->children->eval(switch_case->children, scope, context, result);
    }
  }
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_emit)
{
  ENTER_EVAL_FUNC("emit")
  for (const char *s = expr->text; *s != '\0'; s++) {
    if (*s == '\\') {
      s++;
      if (*s == '\0') {
        break;
      }
      else if (*s == 'n') {
        ostream_put(context->ostream, '\n');
      }
      else if (*s == 't') {
        ostream_put(context->ostream, '\t');
      }
      else {
        ostream_put(context->ostream, *s);
      }
    }
    else if (*s == '$') {
      s++;
      if (*s == '\0') {
        break;
      }
      else if (*s == '$') {
        ostream_put(context->ostream, '$');
      }
      else {
        ts_expr_t *letter = expr->children;
        for (; letter != NULL; letter = letter->next) {
          if (letter->ch == *s) {
            break;
          }
        }
        if (letter != NULL) {
          value_init(result);
          if (!letter->children->eval(letter->children, scope, context, result)) {
            LEAVE_EVAL_FUNC
            return ts_false;
          }
          if (result->type == ts_value_type_text) {
            ostream_puts(context->ostream, result->text);
            value_init(result);
          }
        }
        else {
          ostream_put(context->ostream, *s);
        }
      }
    }
    else {
      ostream_put(context->ostream, *s);
    }
  }
  LEAVE_EVAL_FUNC
  return ts_true;
}

EVAL_FUNC(eval_statements)
{
  ENTER_EVAL_FUNC("statements")
  for (ts_expr_t *child = expr->children; child != NULL; child = child->next) {
    value_init(result);
    if (!child->eval(child, scope, context, result)) {
      LEAVE_EVAL_FUNC
      return ts_false;
    }
    if (result->type == ts_value_type_text) {
      ostream_puts(context->ostream, result->text);
      value_init(result);
    }
  }
  LEAVE_EVAL_FUNC
  return ts_true;
}

static void check_expr(ts_expr_t *expr, ts_scope_t *scope, ts_bool *errors)
{
  if (expr->eval == eval_proc_call) {
    if (expr->proc_def == NULL) {
      fprintf(stdout, "Error (%lu.%lu): proc not defined.\n", expr->pos.line, expr->pos.column);
      *errors = ts_true;
    }
    else if (expr->proc_def->proc_body == NULL) {
      fprintf(stdout, "Error (%lu.%lu): proc '%s' not defined.", expr->pos.line, expr->pos.column, expr->proc_def->name);
      if (expr->proc_def->method != NULL) {
        fprintf(stdout, " Did you mean method?");
      }
      fprintf(stdout, "\n");
      *errors = ts_true;
    }
    for (ts_expr_t *child = expr->children; child != NULL; child = child->next) {
      check_expr(child, scope, errors);
    }
  }
  else if (expr->eval == eval_for) {
    check_expr(expr->children, scope, errors);
    ts_scope_t for_var;
    for_var.var = expr->text;
    for_var.prev = scope;
    check_expr(expr->children->next, &for_var, errors);
  }
  else if (expr->eval == eval_var) {
    ts_bool found = ts_false;
    for (ts_scope_t *s = scope; s != NULL; s = s->prev) {
      if (strcmp(s->var, expr->text) == 0) {
        found = ts_true;
        break;
      }
    }
    if (!found) {
      fprintf(stdout, "Error (%lu.%lu): var '%s' not defined.\n", expr->pos.line, expr->pos.column, expr->text);
      *errors = ts_true;
    }
  }
  else if (expr->eval == eval_switch) {
    for (ts_expr_t *switch_case = expr->children; switch_case != NULL; switch_case = switch_case->next) {
      if (switch_case->ch == 'c') {
        check_expr(switch_case->children, scope, errors);
        check_expr(switch_case->children->next, scope, errors);
      }
      else {
        check_expr(switch_case->children, scope, errors);
      }
    }
  }
  else {
    for (ts_expr_t *child = expr->children; child != NULL; child = child->next) {
      check_expr(child, scope, errors);
    }
  }
}

static ts_bool check_script(ts_executor_t *executor)
{
  ts_bool errors = ts_false;
  for (proc_definition_t *proc_def = executor->proc_defs; proc_def != NULL; proc_def = proc_def->next) {
    if (proc_def->func != 0) {
      if (proc_def->proc_body != NULL) {
        fprintf(stdout, "Error: proc '%s' also defined as build-in func.\n", proc_def->name);
      }
    }
    else if (proc_def->method != 0 || proc_def->field != 0) {
    }
    else if (proc_def->proc_body == NULL) {
      fprintf(stdout, "Error: proc '%s' not defined.\n", proc_def->name);
    }
    else {
      if (!proc_def->proc_called && strcmp(proc_def->name, "main") != 0) {
        fprintf(stdout, "Warning: proc '%s' never called.\n", proc_def->name);
      }
      ts_scope_t *scope = NULL;
      for (proc_parameter_t *param = proc_def->proc_params; param != NULL; param = param->next) {
        ts_scope_t *scope_param = (ts_scope_t*)malloc(sizeof(ts_scope_t));
        if (scope_param == NULL) {
          return ts_false;
        }
        scope_param->var = param->name;
        scope_param->prev = scope;
        scope = scope_param;
      }
      check_expr(proc_def->proc_body, scope, &errors);

      while (scope != NULL) {
        ts_scope_t *prev = scope->prev;
        free((void*)scope);
        scope = prev;
      }
    }
  }
  return !errors;
}

