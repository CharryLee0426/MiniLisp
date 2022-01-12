/**
 这是一个 LISP 语言的简单解释器，没有实现
 GC（垃圾回收）
 LISP 是一个非常古老的高级编程语言，因为出现时间
 早，在 AI 领域还有一点点应用，写这个解释器的原因
 只是想了解一下一个解释器是怎么写的。
 
 @author Charry Lee
 @date 2022-01-10
 */

// 解释器需要引用的头文件
#include <assert.h>     // 诊断
#include <ctype.h>      // 提供字符测试函数
#include <inttypes.h>   // 提供了各种位宽的整数类型输入输出时的转换标志宏
#include <stdarg.h>     // 可变参数表，可以遍历未知数目和类型的函数参数表的功能
#include <stdbool.h>    // 四个布尔型的预定义宏
#include <stddef.h>     // 定义常见类型与宏，比如 size_t, wchar_t...
#include <stdio.h>
#include <stdlib.h>     // 实用函数头文件，比如 malloc...
#include <string.h>     // 处理字符串的头文件

/**
 30 行至 111 行定义了 Lisp 解释器用到的几个变量的数据结构
 @author Charry Lee
 @date 2022-01-10
 */


// LISP 对象

// 首先定义 LISP 对象的类型
// 这是从用户视角可见的常规类型
enum {
    TINT = 1,
    TCELL,      // 2
    TSYMBOL,    // 3
    TPRIMITIVE, // ...
    TFUNCTION,
    TMACRO,
    TSPECIAL,
    TENV,
};

// TSPECIAL 类型下的 子类型
enum {
    TNIL = 1,
    TDOT,
    TCPAREN,
    TTRUE,
};

// 定义初始函数
typedef struct Obj *Primitive(struct Obj *env, struct Obj *args);

// 定义 Obj 对象
typedef struct Obj {
    // Obj 对象的前 32 位表示 Obj 的类型，任何操作 Obj 的代码在操作 Obj
    // 前都必须先检查它的类型，然后再去访问接下来的成员变量。
    int type;
    
    // Obj 中包含的值
    union {
        // Int
        int value;
        
        // Cell
        struct {
            struct Obj *car;    // 指向 Cell 对象的第一个变量
            struct Obj *cdr;    // 指向 Cell 对象的第二个变量
        };
        
        // Symbol
        char name[1];
        
        // Primitive
        Primitive *fn;
        
        // Function or Macro（宏）
        struct {
            struct Obj *params;     // 函数的参数
            struct Obj *body;       // 函数的函数体
            struct Obj *env;        // 函数的环境
        };
        
        // 对于特殊类型 Obj，还会有副类型
        int subtype;
        
        // 环境框架，存放了从标志到值的 map
        struct {
            struct Obj *vars;
            struct Obj *up;
        };
        
        // 指向前的指针？这里不知道这个 moved 是干什么用的
        void *moved;
    };
} Obj;

// Lisp 语言中的几个常量
static Obj *Nil;            // 相当于 NULL
static Obj *Dot;            // 相当于 "."
static Obj *Cparen;         // 相当于括号
static Obj *True;

// 这个列表包括了所有标志，传统上这种数据结构叫做 "obarray",
//但这实际上是个列表而不是数组。
static Obj *Symbols;

// 错误，__attribute((noreturn)) 会提示编译器该函数不会返回值，
//编译器会将无法执行的代码自动移除实现优化
static void error(char *fmt, ...) __attribute((noreturn));

/**
 构造方法
 120 - 184 行
 @author Charry Lee
 @date 2022-01-11
 */

// 分配函数，为 Obj 对象根据对象类型分配内存空间
static Obj *alloc(int type, size_t size) {
    // 添加类型标志位的 size，这个 value 不知道在哪里出现的全局变量？
    size += offsetof(Obj, value);
    
    // 为 Obj 对象分配内存空间
    Obj *obj = malloc(size);
    obj->type = type;
    return obj;
}

// 各种对象的生成函数
static Obj *make_int(int value) {
    Obj *r = alloc(TINT, sizeof(int));
    r->value = value;
    return r;
}

static Obj *make_symbol(char *name) {
    Obj *sym = alloc(TSYMBOL, strlen(name) + 1);
    strcpy(sym->name, name);
    return sym;
}

// 现在还不知道这个 Primitive 有什么用
static Obj *make_primitive(Primitive *fn) {
    Obj *r = alloc(TPRIMITIVE, sizeof(Primitive *));
    r->fn = fn;
    return r;
}

static Obj *make_function(int type, Obj *params, Obj *body, Obj *env) {
    assert(type == TFUNCTION || type == TMACRO);
    Obj *r = alloc(type, sizeof(Obj *) * 3);
    r->params = params;
    r->body = body;
    r->env = env;
    return r;
}

static Obj *make_special(int subtype) {
    Obj *r = malloc(sizeof(void*) * 2);
    r->type = TSPECIAL;
    r->subtype = subtype;
    return r;
}

static Obj *make_env(Obj *vars, Obj *up) {
    Obj *r = alloc(TENV, sizeof(Obj *) * 2);
    r->vars = vars;
    r->up = up;
    return r;
}

static Obj *cons(Obj *car, Obj *cdr) {
    Obj *cell = alloc(TCELL, sizeof(Obj *) * 2);
    cell->car = car;
    cell->cdr = cdr;
    return cell;
}

// acon 是复合类型，返回的是 ((x . y) . a)
static Obj *acon(Obj *x, Obj *y, Obj *a) {
    return cons(cons(x, y), a);
}

/**
 语法分析器
 这部分是一个简易的向下递归的语法分析程序。
 在阅读这部分之前，有必要简单的了解一下 LISP 的语法。
 @author Charry Lee
 @date 2022-01-12
 */

// 读取
static Obj *read(void);

// 将错误信息输出到 stderr 流
static void error(char *fmt, ...) {
    va_list ap;                     // 定义一个可变参数表指针 ap（args_pointer）
    va_start(ap, fmt);              // 从 fmt 的第一个参数开始，初始化 ap
    vfprintf(stderr, fmt, ap);      // 将 ap 按照 fmt 的格式输入到 stderr 流
    fprintf(stderr, "\n");          // 添加一个换行符
    va_end(ap);                     // 使用完 ap 指针以后必须用 va_end 结束 ap 指针
    exit(1);                        // 发生错误，异常退出（返回 1）
}

static int peek(void) {
    int c = getchar();
    ungetc(c, stdin);       // 将字符 c 退回到 stdin 中
    return c;
}

/**
 直到输入新行前一直跳过解释。
 根据操作系统的实现不同，换行可能为 '\r', "\r\n" or "\n"
 */
static void skip_line(void) {
    for (;;) {
        int c = getchar();
        if (EOF == c || '\n' == c) {
            return;
        }
        if ('r' == c) {
            if ('\n' == peek()) {
                getchar();
            }
            return;
        }
    }
}

// 读取列表，要注意此时列表的 '(' 已经被读取到
static Obj *read_list(void) {
    // 读取第二个 Obj，并对其中几种错误进行规避。
    Obj *obj = read();
    if (!obj) {                             // 未封闭的括号
        error("Unclosed parenthesis");
    }
    if (obj == Dot) {                       // 只有一个点
        error("Stray Dot");
    }
    if (obj == Cparen) {                    // () == Nil
        return Nil;
    }
    
    // 上面的几种判断错误或判空流程过去后，开始正式的读取列表
    Obj *head, *tail;
    head = tail = cons(obj, Nil);     // 初始化为 (head, tail)
    for (;;) {
        Obj *obj = read();
        if (!obj) {
            error("Unclosed parenthesis");
        }
        if (Cparen == obj) {
            return head;
        }
        if (Dot == obj) {
            tail->cdr = read();
            if (read() != Cparen) {
                error("Closed parenthesis excepted after dot");
            }
            return head;
        }
        tail->cdr = cons(obj, Nil);
        tail = tail->cdr;
    }
    return Nil;     // 理论上来说应该这一部分永远也不会执行，但是不这么写 Xcode 会报错
}

// 如果存在同名的标志，则返回已经存在的那个，否则创建一个新的标志。
static Obj *intern(char *name) {
    for (Obj *p = Symbols; p != Nil; p = p->cdr) {
        if (0 == strcmp(name, p->car->name)) {
            return p->car;
        }
    }
    Obj *sym = make_symbol(name);
    Symbols = cons(sym, Symbols);
    return sym;
}

// 读取巨集 '(...)。读取一个表达式然后返回 (quote <expr>)
static Obj *read_quote(void) {
    Obj *sym = intern("quote");
    return cons(sym, cons(read(), Nil));
}

static int read_number(int val) {
    while (isdigit(peek())) {
        val = val * 10 + (getchar() - '0');
    }
    return val;
}

#define SYMBOL_MAX_LEN 200      // 定义标志最大长度为 200

static Obj *read_symbol(char c) {
    char buf[SYMBOL_MAX_LEN + 1];
    int len = 1;
    buf[0] = c;
    while (isalnum(peek()) || '-' == peek()) {
        if (SYMBOL_MAX_LEN <= len) {
            error("Symbol name too long");
        }
        buf[len++] = getchar();
    }
    buf[len] = '\0';
    return intern(buf);
}

// read 函数的具体实现就，这个应该是一个很重要的函数。
static Obj *read(void) {
    for (; ; ) {
        int c = getchar();
        if (' ' == c || '\n' == c || '\r' == c || '\t' == c) {
            continue;
        }
        if (EOF == c) {
            return NULL;
        }
        if (';' == c) {
            skip_line();
            continue;
        }
        if ('(' == c) {
            return read_list();
        }
        if (')' == c) {
            return Cparen;
        }
        if ('.' == c) {
            return Dot;
        }
        if (isdigit(c)) {
            return make_int(read_number(c - '0'));
        }
        if ('-' == c) {
            return make_int(-read_number(c - '0'));
        }
        if (isalpha(c) || strchr("+=!@#$%^&*", c)) {
            return read_symbol(c);
        }
        error("Don't know how to handle %c", c);
    }
}

// 将给定的 Obj 打印到控制台
static void print(Obj *obj) {
    switch (obj->type) {
        case TINT:
            printf("%d", obj->value);
            break;
        case TCELL:
            printf("(");
            for (; ; ) {
                print(obj->car);
                if (Nil == obj->cdr) {
                    break;
                }
                if (TCELL != obj->cdr->type) {
                    printf(" . ");
                    print(obj->cdr);
                    break;
                }
                obj = obj->cdr;
            }
            printf(")");
            break;
        case TSYMBOL:
            printf("%s", obj->name);
            break;
        case TPRIMITIVE:
            printf("<primitive>");
            break;
        case TFUNCTION:
            printf("<function>");
            break;
        case TMACRO:
            printf("<marcro>");
            break;
        case TSPECIAL:
            if (Nil == obj) {
                printf("()");
            } else if (True == obj) {
                printf("t");
            } else {
                error("Bug: print: Unknown subtype: %d", obj->subtype);
                return;
            }
            
        default:
            error("Bug: print: Unknown tag type: %d", obj->type);
    }
}

//取得列表的长度
static int list_length(Obj *list) {
    int len = 0;
    for (; ; ) {
        if (Nil == list) {
            return len;
        }
        if (TCELL != list->type) {
            error("length: cannot handle dotted list");
        }
        list = list->cdr;
        len++;
    }
}









/**
 入口点
 LISP 解释器从这里开始运行。
 开发初期只做占位使用，如果不这样 IDE 会一直报错。
 
 @author Charyy Lee
 @date 2022-01-10
 */
int main(int argc, char **argv) {
    // 在这里最后插入解释器业务逻辑，现在用于测试
    printf("offsetof is %lu\n", offsetof(Obj, value));
    return 0;
}
