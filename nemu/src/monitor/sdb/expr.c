/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <memory/paddr.h>
#include <stdlib.h>
#include <string.h>
/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

enum {
  TK_NOTYPE = 256, TK_EQ,

  /* TODO: Add more token types */
  TK_NUM,
  TK_HEX,
  TK_REG,
  TK_DEREF,
  TK_NEG,

};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},    // spaces
  {"0[xX][0-9a-fA-F]+", TK_HEX},  // hex
  {"[0-9]+", TK_NUM},		  // number
  {"\\$[a-zA-Z0-9]+", TK_REG},	  // register
  {"\\+", '+'},         // plus
  {"==", TK_EQ},        // equal
  {"-", '-'},
  {"\\*", '*'},
  {"/", '/'},
  {"\\(", '('},
  {"\\)", ')'},

};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
           case TK_NOTYPE:
             break;

           case TK_NUM:
  	   case TK_HEX:
  	   case TK_REG:
    	     tokens[nr_token].type = rules[i].token_type;

    	   if (substr_len >= sizeof(tokens[nr_token].str)) {
             printf("Token too long: %.*s\n", substr_len, substr_start);
      	     return false;
           }

    	   strncpy(tokens[nr_token].str, substr_start, substr_len);
           tokens[nr_token].str[substr_len] = '\0';
           nr_token++;
           break;

  	   case TK_EQ:
  	   case '+':
    	   case '-':
           case '*':
           case '/':
           case '(':
           case ')':
     	     tokens[nr_token].type = rules[i].token_type;
    	     tokens[nr_token].str[0] = '\0';
    	     nr_token++;
    	     break;

	  default: TODO();
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }
for (int j = 0; j < nr_token; j++) {
    if (tokens[j].type == '*') {
      if (j == 0 ||
          tokens[j - 1].type == '+' ||
          tokens[j - 1].type == '-' ||
          tokens[j - 1].type == '*' ||
          tokens[j - 1].type == '/' ||
          tokens[j - 1].type == '(' ||
          tokens[j - 1].type == TK_EQ) {
        tokens[j].type = TK_DEREF;
      }
    }

    if (tokens[j].type == '-') {
      if (j == 0 ||
          tokens[j - 1].type == '+' ||
          tokens[j - 1].type == '-' ||
          tokens[j - 1].type == '*' ||
          tokens[j - 1].type == '/' ||
          tokens[j - 1].type == '(' ||
          tokens[j - 1].type == TK_EQ) {
        tokens[j].type = TK_NEG;
      }
    }
  }
  return true;
}

static bool check_parentheses(int p, int q) {
  if (tokens[p].type != '(' || tokens[q].type != ')') {
    return false;
  }

  int balance = 0;

  for (int i = p; i <= q; i++) {
    if (tokens[i].type == '(') {
      balance++;
    } else if (tokens[i].type == ')') {
      balance--;
      if (balance == 0 && i < q) {
        return false;
      }
      if (balance < 0) {
        return false;
      }
    }
  }

  return balance == 0;
}

static int precedence(int type) {
  switch (type) {
    case TK_EQ:
      return 1;
    case '+':
    case '-':
      return 2;
    case '*':
    case '/':
      return 3;
    case TK_DEREF:
    case TK_NEG:
      return 4;
    default:
      return 100;
  }
}

static bool is_binary_op(int type) {
  return type == TK_EQ || type == '+' || type == '-' || type == '*' || type == '/';
}

static int dominant_op(int p, int q) {
  int op = -1;
  int min_prec = 100;
  int balance = 0;

  for (int i = p; i <= q; i++) {
    int type = tokens[i].type;

    if (type == '(') {
      balance++;
      continue;
    }

    if (type == ')') {
      balance--;
      continue;
    }

    if (balance != 0) {
      continue;
    }

    if (is_binary_op(type)) {
      int prec = precedence(type);

      if (prec <= min_prec) {
        min_prec = prec;
        op = i;
      }
    }
  }

  return op;
}

static word_t eval(int p, int q, bool *success) {
  if (p > q) {
    *success = false;
    return 0;
  }

  if (p == q) {
    switch (tokens[p].type) {
      case TK_NUM:
        return strtoull(tokens[p].str, NULL, 10);

      case TK_HEX:
        return strtoull(tokens[p].str, NULL, 16);

      case TK_REG: {
        bool reg_success = false;

        word_t val = isa_reg_str2val(tokens[p].str + 1, &reg_success);

        if (!reg_success) {
          printf("Unknown register: %s\n", tokens[p].str);
          *success = false;
          return 0;
        }

        return val;
      }

      default:
        printf("Bad token: %s\n", tokens[p].str);
        *success = false;
        return 0;
    }
  }

  if (check_parentheses(p, q)) {
    return eval(p + 1, q - 1, success);
  }

  if (tokens[p].type == TK_DEREF) {
    word_t addr = eval(p + 1, q, success);
    if (!*success) {
      return 0;
    }

    return paddr_read(addr, 4);
  }

  if (tokens[p].type == TK_NEG) {
    word_t val = eval(p + 1, q, success);
    if (!*success) {
      return 0;
    }

    return -val;
  }

  int op = dominant_op(p, q);

  if (op < 0) {
    *success = false;
    return 0;
  }

  word_t val1 = eval(p, op - 1, success);
  if (!*success) {
    return 0;
  }

  word_t val2 = eval(op + 1, q, success);
  if (!*success) {
    return 0;
  }

  switch (tokens[op].type) {
    case '+':
      return val1 + val2;

    case '-':
      return val1 - val2;

    case '*':
      return val1 * val2;

    case '/':
      if (val2 == 0) {
        printf("Division by zero\n");
        *success = false;
        return 0;
      }
      return val1 / val2;

    case TK_EQ:
      return val1 == val2;

    default:
      *success = false;
      return 0;
  }
}

word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

if (nr_token == 0) {
    *success = false;
    return 0;
  }

  *success = true;
  return eval(0, nr_token - 1, success);
}
