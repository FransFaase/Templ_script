
// replace this with your own type, if needed
#define TS_CONTEXT void
#define TS_TREE_TYPE void
#define TS_PARENT_OF_TREE_TYPE(X) 0
#define TS_FIRST_CHILD(X) 0
#define TS_NEXT_SIBLING(X) 0

typedef int ts_bool;
const ts_bool ts_false = 0;
const ts_bool ts_true = 1;

typedef struct ts_executor_t ts_executor_t;

ts_executor_t *ts_new_executor(FILE* f);
ts_bool ts_executor_execute(ts_executor_t* executor, TS_CONTEXT* context_data, TS_TREE_TYPE *root_node, ts_bool debugging, ts_bool debug_or_error);
void ts_executor_free(ts_executor_t* executor);

typedef struct ts_expr_t ts_expr_t;
typedef struct ts_scope_t ts_scope_t;
typedef struct ts_exec_context_t ts_exec_context_t;
typedef struct ts_value_t ts_value_t;

typedef ts_bool (*ts_eval_func_p)(ts_expr_t *expr, ts_scope_t *scope, ts_exec_context_t *context, ts_value_t *result);
void ts_executor_add_func_func(ts_executor_t *executor, const char *name, ts_eval_func_p func);
void ts_executor_add_method_func(ts_executor_t *executor, const char *name, ts_eval_func_p method);
void ts_executor_add_field_func(ts_executor_t *executor, const char *name, ts_eval_func_p field);

enum ts_value_type_t { ts_value_type_none, ts_value_type_bool, ts_value_type_int, ts_value_type_text, ts_value_type_node, ts_value_type_expr };
struct ts_value_t {
  enum ts_value_type_t type;
  const char *text;
  unsigned long long number;
  TS_TREE_TYPE *node;
  ts_scope_t *scope;
  ts_expr_t *expr;
};

#define EVAL_FUNC(X) static ts_bool X(ts_expr_t *expr, ts_scope_t *scope, ts_exec_context_t *context, ts_value_t *result)

void exec_templ_script(TS_CONTEXT* context_data, TS_TREE_TYPE *root_node, FILE* tsf, ts_bool debugging);

