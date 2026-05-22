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
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <memory/paddr.h>
#include <stdlib.h>
#include "sdb.h"

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}


static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  nemu_state.halt_ret = 0;
  return -1;
}

static int cmd_si(char *args) {
  int n = 1;
  if (args != NULL) {
    sscanf(args, "%d", &n);
  }

  if (n <= 0) {
    printf("Usage: si [N], N should be a positive integer\n");
    return 0;
  }

  cpu_exec(n);
  return 0;
}

static int cmd_info(char *args) {
  if (args == NULL) {
    printf("Usage: info r\n");
    return 0;
  }

  if (strcmp(args, "r") == 0) {
    isa_reg_display();
  } else if (strcmp(args, "w") == 0) {
    display_watchpoints();
  } 
  else {
    printf("Unknown subcommand '%s'\n", args);
  }

  return 0;
}

static int cmd_x(char *args) {
  if (args == NULL) {
    printf("Usage: x N EXPR\n");
    return 0;
  }

  char *n_str = strtok(args, " ");
  char *expr_str = strtok(NULL, " ");

  if (n_str == NULL || expr_str == NULL) {
    printf("Usage: x N EXPR\n");
    return 0;
  }

  int n = atoi(n_str);
  if (n <= 0) {
    printf("N should be a positive integer\n");
    return 0;
  }

  paddr_t addr = strtoull(expr_str, NULL, 16);

  for (int i = 0; i < n; i++) {
    if (i % 4 == 0) {
      printf("0x%016lx:", (unsigned long)(addr + i * 4));
    }
    word_t data = paddr_read(addr + i * 4, 4);

    printf(" 0x%08x", (uint32_t)data);
    if (i % 4 == 3 || i == n - 1) {
      printf("\n");
    }
  }
  return 0;
}

static int cmd_w(char *args) {
  if (args == NULL) {
    printf("Usage: w EXPR\n");
    return 0;
  }

  new_wp(args);
  return 0;
}

static int cmd_p(char *args) {
  if (args == NULL) {
    printf("Usage: p EXPR\n");
    return 0;
  }

  bool success = false;
  word_t val = expr(args, &success);

  if (!success) {
    printf("Bad expression: %s\n", args);
    return 0;
  }

  printf(FMT_WORD "\n", val);
  return 0;
}

static int cmd_d(char *args) {
  if (args == NULL) {
    printf("Usage: d N\n");
    return 0;
  }

  int no = atoi(args);
  free_wp(no);
  return 0;
}

static int cmd_help(char *args);

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },

  /* TODO: Add more commands */
  { "si", "Step N instructions, default N = 1", cmd_si },
  { "info", "Print program status", cmd_info },
  { "x", "Examine memory", cmd_x },
  { "w", "Set a watchpoint", cmd_w },
  { "d", "Delete a watchpoint", cmd_d },
  { "p", "Evaluate expression", cmd_p },
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
