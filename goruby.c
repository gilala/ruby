void Init_golf(void);
#define ruby_vm_run goruby_vm_run
#include "main.c"
#undef ruby_vm_run

RUBY_EXTERN int ruby_vm_run(rb_vm_t *);
RUBY_EXTERN void ruby_init_ext(const char *name, void (*init)(void));

static VALUE
init_golf(VALUE arg)
{
    ruby_init_ext("golf", Init_golf);
    return arg;
}

int
goruby_vm_run(ruby_vm_t *vm)
{
    int state;
    if (NIL_P(rb_protect(init_golf, Qtrue, &state))) {
	return state == EXIT_SUCCESS ? EXIT_FAILURE : state;
    }
    return ruby_vm_run(vm);
}
